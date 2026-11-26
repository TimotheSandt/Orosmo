//! `orosmo build|run|clean` : lance make avec `ORS_PROGRESS=1` et lit lui-même les marqueurs de tools/build_progress.py :
//! `@@ors begin <cible absolue> <affiché> <étiquette>` et `@@ors end <cible>` (écrits par engine.mk, éventuellement entre
//! apostrophes). Sortie humaine (une ligne par étape) ou `--json` (une ligne JSON par événement).

use serde_json::{Value, json};
use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::mpsc;
use std::time::Instant;

#[derive(Debug, PartialEq, Eq)]
pub enum Line {
    Begin { target: String, shown: String, label: String },
    End { target: String },
    /// Bruit de make (entrée / sortie de répertoire, « rien à faire », commande d'un sous-make) : non affiché.
    Noise,
    Text(String),
}

pub fn parse_line(raw: &str) -> Line {
    let line = raw.trim_end_matches(['\r', '\n']);
    let unquoted = line.trim_start_matches('\'');
    if let Some(rest) = unquoted.strip_prefix("@@ors ") {
        let rest = rest.strip_suffix('\'').unwrap_or(rest);
        let mut it = rest.splitn(4, ' ');
        let kind = it.next().unwrap_or("");
        let target = it.next().unwrap_or("").to_string();
        if !target.is_empty() {
            match kind {
                "begin" => {
                    let shown = it.next().map(str::to_string).unwrap_or_else(|| target.clone());
                    let label = it.next().unwrap_or("").trim().to_string();
                    return Line::Begin { target, shown, label };
                }
                "end" => return Line::End { target },
                _ => {}
            }
        }
    }
    if is_make_noise(line) {
        return Line::Noise;
    }
    Line::Text(line.to_string())
}

fn is_make_noise(line: &str) -> bool {
    let first = line.split_whitespace().next().unwrap_or("");
    let prog = first.rsplit(['/', '\\']).next().unwrap_or(first);
    let prog = prog.split('[').next().unwrap_or(prog).trim_end_matches(':');
    let prog = prog.strip_suffix(".exe").unwrap_or(prog);
    if !matches!(prog, "make" | "gmake" | "mingw32-make") {
        return false;
    }
    const NOISE: [&str; 6] = ["Entering directory", "Leaving directory", "Nothing to be done", "on entre", "on quitte", "Rien à faire"];
    NOISE.iter().any(|n| line.contains(n)) || line.split_whitespace().nth(1).is_some_and(|a| a.starts_with('-') && !first.ends_with(':'))
}

/// Diagnostic de compilateur `fichier:ligne[:colonne]: (fatal error|error|warning|note): message`, ou erreur de make.
pub fn diagnostic_json(text: &str) -> Value {
    const KINDS: [(&str, &str); 4] = [(": fatal error: ", "error"), (": error: ", "error"), (": warning: ", "warning"), (": note: ", "note")];
    // Un chemin Windows commence par `C:` : la recherche du lieu commence après.
    let skip = if text.as_bytes().get(1) == Some(&b':') { 2 } else { 0 };
    // Le niveau est lu à la fin de l'emplacement : le PREMIER marqueur. Un `error:` dans le message d'un avertissement ne compte pas.
    let found = KINDS.iter()
        .filter_map(|(kind, level)| text.get(skip..)?.find(kind).map(|p| (p + skip, *kind, *level)))
        .min_by_key(|(pos, _, _)| *pos);
    if let Some((pos, kind, level)) = found {
        let loc = &text[..pos];
        let message = &text[pos + kind.len()..];
        let mut parts = loc.rsplitn(3, ':');
        let (a, b, c) = (parts.next(), parts.next(), parts.next());
        let num = |s: Option<&str>| s.and_then(|s| s.trim().parse::<u64>().ok());
        let (file, line, column) = match (num(a), num(b)) {
            (Some(col), Some(line)) => (c.map(str::to_string), Some(line), Some(col)),
            // `fichier:ligne` ; sans deux-points (`123: error: …`), l'emplacement n'est pas exploitable.
            (Some(line), None) => match loc.rsplit_once(':') {
                Some((f, _)) => (Some(f.to_string()), Some(line), None),
                None => (None, None, None),
            },
            _ => (Some(loc.to_string()), None, None),
        };
        let mut v = json!({"event": "diagnostic", "level": level, "text": text, "message": message});
        if let Some(f) = file.filter(|f| !f.is_empty()) {
            v["file"] = json!(f);
        }
        if let Some(l) = line {
            v["line"] = json!(l);
        }
        if let Some(c) = column {
            v["column"] = json!(c);
        }
        return v;
    }
    let level = if text.contains("*** ") || text.contains("undefined reference") { "error" } else { "output" };
    json!({"event": "diagnostic", "level": level, "text": text})
}

pub fn begin_json(target: &str, shown: &str, label: &str) -> Value {
    json!({"event": "begin", "target": target, "shown": shown, "label": label})
}

pub fn end_json(target: &str) -> Value {
    json!({"event": "end", "target": target})
}

pub fn done_json(code: i32, compiled: usize, failed: &[String], elapsed_ms: u128) -> Value {
    json!({"event": "done", "code": code, "compiled": compiled, "failed": failed, "elapsed_ms": elapsed_ms})
}

/// État d'une exécution : sert aux deux sorties.
#[derive(Default)]
pub struct Progress {
    running: Vec<(String, String)>,
    /// Cibles en échec lues avant leur marqueur `begin` (stdout et stderr sont lus par deux fils) : rapprochées à leur `begin`.
    pending_failures: Vec<String>,
    pub compiled: usize,
    pub failed: Vec<String>,
}

impl Progress {
    /// Traite une ligne ; renvoie l'événement JSON correspondant (None pour le bruit) et le texte humain.
    pub fn feed(&mut self, raw: &str) -> Option<(Value, String)> {
        match parse_line(raw) {
            Line::Begin { target, shown, label } => {
                let human = format!("  {:<6}{shown}", label.split_whitespace().next().unwrap_or(""));
                let ev = begin_json(&target, &shown, &label);
                self.running.retain(|(t, _)| *t != target);
                if let Some(i) = self.pending_failures.iter().position(|f| same_target(&target, f)) {
                    self.pending_failures.remove(i);
                    self.failed.push(shown);
                } else {
                    self.running.push((target, shown));
                }
                Some((ev, human))
            }
            Line::End { target } => {
                self.running.retain(|(t, _)| *t != target);
                self.compiled += 1;
                Some((end_json(&target), String::new()))
            }
            Line::Noise => None,
            Line::Text(text) => {
                // `make: *** [Makefile:12: obj/x.o] Error 1` : l'étape en cours qui a échoué.
                // Rattachée seulement si elle désigne une seule étape en cours ; aucune : gardée jusqu'à son `begin`.
                if let Some(t) = failed_target(&text) {
                    let hits: Vec<usize> = self.running.iter().enumerate()
                        .filter(|(_, (full, _))| same_target(full, &t))
                        .map(|(i, _)| i)
                        .collect();
                    match hits[..] {
                        [i] => {
                            let (_, shown) = self.running.remove(i);
                            self.failed.push(shown);
                        }
                        [] => self.pending_failures.push(t),
                        _ => {}
                    }
                }
                Some((diagnostic_json(&text), text))
            }
        }
    }

    pub fn finish(&mut self, code: i32) {
        if code != 0 && self.failed.is_empty() {
            self.failed = self.running.drain(..).map(|(_, s)| s).collect();
        }
    }
}

/// `full` (cible absolue d'un marqueur) et `t` (cible nommée par make, souvent relative au répertoire de make) désignent le même
/// fichier : égalité, ou `t` est une fin de `full` à une limite de composant (`/p/obj/foo.o` et `obj/foo.o`, pas `myfoo.o`).
fn same_target(full: &str, t: &str) -> bool {
    let full = full.replace('\\', "/");
    let t = t.replace('\\', "/");
    let t = t.trim_start_matches("./");
    !t.is_empty() && (full == t || full.ends_with(&format!("/{t}")))
}

fn failed_target(line: &str) -> Option<String> {
    let start = line.find("*** [")? + 5;
    let inner = &line[start..start + line[start..].find(']')?];
    let target = inner.rsplit(": ").next().unwrap_or(inner).trim();
    (line.contains("Error") || line.contains("Erreur")).then(|| target.to_string())
}

/// PATH pour make sous Windows hors d'un shell MSYS2 (`MSYSTEM` absent) : `mingw64\bin` et `usr\bin` de MSYS2 en tête, quand
/// `usr\bin\sh.exe` existe, pour que le `SHELL := sh` du Makefile et les outils POSIX (`find`, `uname`…) passent avant leurs
/// homonymes de Windows. None : rien à changer.
pub fn msys_path(msys_root: &Path, msystem: Option<&str>, path: &str) -> Option<String> {
    if msystem.is_some_and(|m| !m.trim().is_empty()) {
        return None;
    }
    let usr_bin = msys_root.join("usr").join("bin");
    if !usr_bin.join("sh.exe").is_file() {
        return None;
    }
    let mut dirs: Vec<PathBuf> = Vec::new();
    let mingw = msys_root.join("mingw64").join("bin");
    if mingw.is_dir() {
        dirs.push(mingw);
    }
    dirs.push(usr_bin);
    let mut out: Vec<String> = dirs.iter().map(|d| d.display().to_string()).collect();
    if !path.is_empty() {
        out.push(path.to_string());
    }
    Some(out.join(";"))
}

fn windows_msys_path() -> Option<String> {
    if !cfg!(windows) {
        return None;
    }
    let root = std::env::var("MSYS2_ROOT").ok().filter(|r| !r.trim().is_empty()).unwrap_or_else(|| "C:\\msys64".to_string());
    msys_path(Path::new(&root), std::env::var("MSYSTEM").ok().as_deref(), &std::env::var("PATH").unwrap_or_default())
}

/// make à utiliser : `MAKE` s'il est défini ; sous Windows, `mingw32-make` puis `make` trouvés dans le PATH.
pub fn make_program() -> String {
    if let Ok(m) = std::env::var("MAKE")
        && !m.trim().is_empty()
    {
        return m;
    }
    if cfg!(windows) {
        // Chemin complet : `doctor` relance ce programme avec le PATH d'origine, qui n'a peut-être pas le dossier de MSYS2.
        let path = windows_msys_path().unwrap_or_else(|| std::env::var("PATH").unwrap_or_default());
        for cand in ["mingw32-make", "make"] {
            let Some(full) = find_program(&path, ';', cand) else { continue };
            let mut probe = Command::new(&full);
            probe.env("PATH", &path);
            if probe.arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok_and(|s| s.success()) {
                return full.display().to_string();
            }
        }
    }
    "make".to_string()
}

/// Premier `<dossier>/<name>` (ou `<name>.exe`) qui existe dans la liste `path` (séparateur `sep`).
pub fn find_program(path: &str, sep: char, name: &str) -> Option<PathBuf> {
    path.split(sep).filter(|d| !d.is_empty()).find_map(|d| {
        [name.to_string(), format!("{name}.exe")].into_iter().map(|n| Path::new(d).join(n)).find(|p| p.is_file())
    })
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Output {
    Human,
    Json,
}

/// Lance `make <args>` dans `dir` et renvoie son code de sortie.
pub fn run_make(dir: &Path, args: &[String], output: Output) -> Result<i32, String> {
    let make = make_program();
    let start = Instant::now();
    let mut cmd = Command::new(&make);
    if let Some(p) = windows_msys_path() {
        cmd.env("PATH", p);
    }
    let mut child = cmd
        .arg("--no-print-directory")
        .args(args)
        .current_dir(dir)
        .env("ORS_PROGRESS", "1")
        .env_remove("ORS_NO_PROGRESS")
        .env_remove("V")
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| format!("cannot run '{make}': {e} (install GNU Make, see `orosmo doctor`)"))?;

    // stdout et stderr fusionnés ligne par ligne (deux lecteurs, un canal).
    let (tx, rx) = mpsc::channel::<String>();
    let readers: Vec<_> = [child.stdout.take().map(|s| Box::new(s) as Box<dyn io::Read + Send>),
                           child.stderr.take().map(|s| Box::new(s) as Box<dyn io::Read + Send>)]
        .into_iter()
        .flatten()
        .map(|r| {
            let tx = tx.clone();
            std::thread::spawn(move || {
                for line in BufReader::new(r).split(b'\n').map_while(Result::ok) {
                    if tx.send(String::from_utf8_lossy(&line).into_owned()).is_err() {
                        break;
                    }
                }
            })
        })
        .collect();
    drop(tx);

    let mut progress = Progress::default();
    let stdout = io::stdout();
    for line in rx {
        if let Some((ev, human)) = progress.feed(&line) {
            let mut out = stdout.lock();
            match output {
                Output::Json => {
                    let _ = writeln!(out, "{ev}");
                }
                Output::Human if !human.is_empty() => {
                    let _ = writeln!(out, "{human}");
                }
                Output::Human => {}
            }
            let _ = out.flush();
        }
    }
    for r in readers {
        let _ = r.join();
    }
    let status = child.wait().map_err(|e| format!("make did not finish: {e}"))?;
    let code = status.code().unwrap_or(1);
    progress.finish(code);
    let elapsed = start.elapsed();
    match output {
        Output::Json => println!("{}", done_json(code, progress.compiled, &progress.failed, elapsed.as_millis())),
        Output::Human if code == 0 => println!("ok: {} step(s) built, {:.1} s", progress.compiled, elapsed.as_secs_f64()),
        Output::Human => {
            let failed = if progress.failed.is_empty() { String::new() } else { format!("; failed: {}", progress.failed.join(", ")) };
            println!("FAILED (make exit code {code}) after {} step(s){failed}, {:.1} s", progress.compiled, elapsed.as_secs_f64());
        }
    }
    Ok(code)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_ors_markers() {
        assert_eq!(
            parse_line("@@ors begin /p/obj/debug/main.o src/main.cpp CXX\n"),
            Line::Begin { target: "/p/obj/debug/main.o".into(), shown: "src/main.cpp".into(), label: "CXX".into() }
        );
        // `make -n` / printf entre apostrophes ; étiquette en plusieurs mots.
        assert_eq!(
            parse_line("'@@ors begin /p/bin/debug/main bin/debug/main LD game'"),
            Line::Begin { target: "/p/bin/debug/main".into(), shown: "bin/debug/main".into(), label: "LD game".into() }
        );
        assert_eq!(parse_line("@@ors end /p/obj/debug/main.o\r"), Line::End { target: "/p/obj/debug/main.o".into() });
        assert_eq!(parse_line("make[1]: Entering directory '/p'"), Line::Noise);
        assert_eq!(parse_line("mingw32-make.exe: Nothing to be done for 'debug'."), Line::Noise);
        assert_eq!(parse_line("make: *** [Makefile:3: x] Error 1"), Line::Text("make: *** [Makefile:3: x] Error 1".into()));
        assert_eq!(parse_line("src/a.cpp:1:2: error: x"), Line::Text("src/a.cpp:1:2: error: x".into()));
    }

    #[test]
    fn json_events() {
        let mut p = Progress::default();
        let lines = [
            "@@ors begin /p/obj/main.o src/main.cpp CXX",
            "src/main.cpp:12:5: error: 'foo' was not declared in this scope",
            "make: *** [Makefile:40: /p/obj/main.o] Error 1",
            "@@ors begin /p/obj/ok.o src/ok.cpp CXX",
            "@@ors end /p/obj/ok.o",
            "make: Leaving directory '/p'",
        ];
        let events: Vec<String> = lines.iter().filter_map(|l| p.feed(l)).map(|(ev, _)| ev.to_string()).collect();
        p.finish(2);
        assert_eq!(events, [
            r#"{"event":"begin","label":"CXX","shown":"src/main.cpp","target":"/p/obj/main.o"}"#,
            r#"{"column":5,"event":"diagnostic","file":"src/main.cpp","level":"error","line":12,"message":"'foo' was not declared in this scope","text":"src/main.cpp:12:5: error: 'foo' was not declared in this scope"}"#,
            r#"{"event":"diagnostic","level":"error","text":"make: *** [Makefile:40: /p/obj/main.o] Error 1"}"#,
            r#"{"event":"begin","label":"CXX","shown":"src/ok.cpp","target":"/p/obj/ok.o"}"#,
            r#"{"event":"end","target":"/p/obj/ok.o"}"#,
        ]);
        assert_eq!(p.compiled, 1);
        assert_eq!(p.failed, ["src/main.cpp"]);
        assert_eq!(done_json(2, 1, &p.failed, 1500).to_string(),
                   r#"{"code":2,"compiled":1,"elapsed_ms":1500,"event":"done","failed":["src/main.cpp"]}"#);

        let w = diagnostic_json("C:\\game\\src\\a.cpp:3: warning: unused");
        assert_eq!((w["file"].as_str(), w["line"].as_u64(), w["level"].as_str()), (Some("C:\\game\\src\\a.cpp"), Some(3), Some("warning")));
        assert_eq!(diagnostic_json("linking...")["level"], "output");
    }

    #[test]
    fn diagnostic_level_is_read_at_the_location() {
        let w = diagnostic_json("src/a.cpp:3: warning: message: error: detail");
        assert_eq!((w["level"].as_str(), w["file"].as_str(), w["line"].as_u64()), (Some("warning"), Some("src/a.cpp"), Some(3)));
        assert_eq!(w["message"], "message: error: detail");
        let f = diagnostic_json("src/a.cpp:1:2: fatal error: x.hpp: No such file");
        assert_eq!((f["level"].as_str(), f["column"].as_u64()), (Some("error"), Some(2)));
        // Pas de deux-points avant le numéro : pas de panique, pas d'emplacement.
        let n = diagnostic_json("123: error: arrêt");
        assert_eq!((n["level"].as_str(), n.get("file"), n.get("line")), (Some("error"), None, None));
        let e = diagnostic_json("src/a.cpp:123: error: arrêt");
        assert_eq!((e["file"].as_str(), e["line"].as_u64()), (Some("src/a.cpp"), Some(123)));
        let d = diagnostic_json("C:\\g\\a.cpp:7:1: error: x");
        assert_eq!((d["file"].as_str(), d["line"].as_u64()), (Some("C:\\g\\a.cpp"), Some(7)));
    }

    #[test]
    fn failures_match_whole_path_components() {
        let mut p = Progress::default();
        p.feed("@@ors begin /p/obj/myfoo.o src/myfoo.cpp CXX");
        p.feed("@@ors begin /p/obj/foo.o src/foo.cpp CXX");
        p.feed("make: *** [Makefile:40: obj/foo.o] Error 1");
        assert_eq!(p.failed, ["src/foo.cpp"]);
        // Échec lu avant son `begin` (deux lecteurs) : rapproché à l'arrivée du marqueur, même si une autre étape échoue.
        let mut p = Progress::default();
        p.feed("make: *** [Makefile:40: obj/a.o] Error 1");
        p.feed("@@ors begin /p/obj/b.o src/b.cpp CXX");
        p.feed("make: *** [Makefile:40: /p/obj/b.o] Error 1");
        p.feed("@@ors begin /p/obj/a.o src/a.cpp CXX");
        p.finish(2);
        assert_eq!(p.failed, ["src/b.cpp", "src/a.cpp"]);
    }

    #[test]
    fn msys2_tools_come_first_outside_an_msys_shell() {
        let root = std::env::temp_dir().join(format!("orosmo_msys_{}", std::process::id()));
        assert_eq!(msys_path(&root, None, "C:\\Windows"), None); // pas de MSYS2
        std::fs::create_dir_all(root.join("usr").join("bin")).unwrap();
        std::fs::create_dir_all(root.join("mingw64").join("bin")).unwrap();
        std::fs::write(root.join("usr").join("bin").join("sh.exe"), b"").unwrap();
        let p = msys_path(&root, None, "C:\\Windows\\system32").unwrap();
        let parts: Vec<&str> = p.split(';').collect();
        assert_eq!(parts.len(), 3);
        assert!(parts[0].ends_with("bin") && parts[0].contains("mingw64"));
        assert!(parts[1].contains("usr"));
        assert_eq!(parts[2], "C:\\Windows\\system32");
        assert_eq!(msys_path(&root, Some("MINGW64"), "x"), None); // déjà dans un shell MSYS2
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn make_is_resolved_to_a_full_path() {
        let root = std::env::temp_dir().join(format!("orosmo_findprog_{}", std::process::id()));
        let (a, b) = (root.join("a"), root.join("b"));
        std::fs::create_dir_all(&a).unwrap();
        std::fs::create_dir_all(&b).unwrap();
        std::fs::write(b.join("mingw32-make.exe"), b"").unwrap();
        let path = format!("{};{}", a.display(), b.display());
        assert_eq!(find_program(&path, ';', "mingw32-make"), Some(b.join("mingw32-make.exe")));
        assert_eq!(find_program(&path, ';', "make"), None);
        let _ = std::fs::remove_dir_all(&root);
    }
}
