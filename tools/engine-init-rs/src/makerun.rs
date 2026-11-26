//! `orosmo build|run|clean` : lance make avec `ORS_PROGRESS=1` et lit lui-même les marqueurs de tools/build_progress.py :
//! `@@ors begin <cible absolue> <affiché> <étiquette>` et `@@ors end <cible>` (écrits par engine.mk, éventuellement entre
//! apostrophes). Sortie humaine (une ligne par étape) ou `--json` (une ligne JSON par événement).

use serde_json::{Value, json};
use std::io::{self, BufRead, BufReader, Write};
use std::path::Path;
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
    for (kind, level) in [(": fatal error: ", "error"), (": error: ", "error"), (": warning: ", "warning"), (": note: ", "note")] {
        // Un chemin Windows commence par `C:` : la recherche du lieu commence après.
        let skip = if text.as_bytes().get(1) == Some(&b':') { 2 } else { 0 };
        if let Some(pos) = text[skip..].find(kind).map(|p| p + skip) {
            let loc = &text[..pos];
            let message = &text[pos + kind.len()..];
            let mut parts = loc.rsplitn(3, ':');
            let (a, b, c) = (parts.next(), parts.next(), parts.next());
            let num = |s: Option<&str>| s.and_then(|s| s.trim().parse::<u64>().ok());
            let (file, line, column) = match (num(a), num(b)) {
                (Some(col), Some(line)) => (c.map(str::to_string), Some(line), Some(col)),
                (Some(line), None) => (Some(loc[..loc.len() - a.unwrap_or("").len() - 1].to_string()), Some(line), None),
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
                self.running.push((target, shown));
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
                if let Some(t) = failed_target(&text)
                    && let Some(i) = self.running.iter().position(|(full, _)| full.ends_with(&t))
                {
                    let (_, shown) = self.running.remove(i);
                    self.failed.push(shown);
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

fn failed_target(line: &str) -> Option<String> {
    let start = line.find("*** [")? + 5;
    let inner = &line[start..start + line[start..].find(']')?];
    let target = inner.rsplit(": ").next().unwrap_or(inner).trim();
    (line.contains("Error") || line.contains("Erreur")).then(|| target.to_string())
}

/// make à utiliser : `MAKE` s'il est défini ; sous Windows, `mingw32-make` puis `make` trouvés dans le PATH.
pub fn make_program() -> String {
    if let Ok(m) = std::env::var("MAKE")
        && !m.trim().is_empty()
    {
        return m;
    }
    if cfg!(windows) {
        for cand in ["mingw32-make", "make"] {
            if Command::new(cand).arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok_and(|s| s.success()) {
                return cand.to_string();
            }
        }
    }
    "make".to_string()
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
    let mut child = Command::new(&make)
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
}
