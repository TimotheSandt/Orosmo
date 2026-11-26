//! Dossier utilisateur des moteurs installés (`OROSMO_HOME`, contrat SDK A76).
//!
//! ```text
//! $OROSMO_HOME/bin/orosmo[.exe]
//! $OROSMO_HOME/engines/<x.y.z>/   contenu du paquet de release, extrait tel quel
//! ```
//! `OROSMO_HOME` (variable d'environnement non vide) est prioritaire ; sinon `~/.orosmo` (Linux, macOS) ou
//! `%LOCALAPPDATA%\Orosmo` (Windows).

use std::fs;
use std::path::{Path, PathBuf};

/// Racine d'après les variables données par `get` ; `windows` choisit la valeur par défaut. None : aucune variable utilisable.
pub fn home_from(get: impl Fn(&str) -> Option<String>, windows: bool) -> Option<PathBuf> {
    let non_empty = |k: &str| get(k).filter(|v| !v.trim().is_empty());
    if let Some(h) = non_empty("OROSMO_HOME") {
        return Some(PathBuf::from(h));
    }
    if windows {
        non_empty("LOCALAPPDATA").map(|d| PathBuf::from(d).join("Orosmo"))
    } else {
        non_empty("HOME").map(|d| PathBuf::from(d).join(".orosmo"))
    }
}

/// Racine absolue pour ce processus.
pub fn orosmo_home() -> Result<PathBuf, String> {
    let home = home_from(|k| std::env::var(k).ok(), cfg!(windows))
        .ok_or("cannot locate the Orosmo home: set OROSMO_HOME (or HOME / LOCALAPPDATA)")?;
    std::path::absolute(&home).map_err(|e| format!("invalid OROSMO_HOME {}: {e}", home.display()))
}

pub fn engines_dir(home: &Path) -> PathBuf {
    home.join("engines")
}

/// `v0.6.2` -> `0.6.2`. Refuse tout ce qui n'est pas un nom de version simple (sert de nom de dossier, et `remove` l'efface).
pub fn normalize_version(v: &str) -> Result<String, String> {
    let v = v.trim();
    let v = v.strip_prefix('v').unwrap_or(v);
    let ok = v.chars().next().is_some_and(|c| c.is_ascii_digit())
        && v.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '+'))
        && !v.contains("..");
    if ok { Ok(v.to_string()) } else { Err(format!("invalid engine version: '{v}' (expected x.y.z)")) }
}

pub fn parse_version(v: &str) -> Option<(u32, u32, u32)> {
    let v = v.trim_start_matches('v');
    let mut parts = v.splitn(3, '.');
    let major: u32 = parts.next()?.parse().ok()?;
    let minor: u32 = parts.next()?.parse().ok()?;
    let patch: u32 = parts.next()
        .and_then(|p| p.split(['-', '+']).next())
        .and_then(|p| p.parse().ok())
        .unwrap_or(0);
    Some((major, minor, patch))
}

pub fn is_newer(remote_tag: &str, local: &str) -> bool {
    match (parse_version(remote_tag), parse_version(local)) {
        (Some(r), Some(l)) => r > l,
        _ => false,
    }
}

/// Dossier du moteur `version` s'il est installé.
pub fn engine_path(home: &Path, version: &str) -> Option<PathBuf> {
    let v = normalize_version(version).ok()?;
    let p = engines_dir(home).join(v);
    p.is_dir().then_some(p)
}

/// Versions installées, de la plus ancienne à la plus récente. Les dossiers cachés (extractions en cours) sont ignorés.
pub fn list_versions(home: &Path) -> Vec<String> {
    let mut out: Vec<String> = fs::read_dir(engines_dir(home))
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.path().is_dir())
        .filter_map(|e| e.file_name().to_str().map(str::to_string))
        .filter(|n| !n.starts_with('.') && normalize_version(n).is_ok())
        .collect();
    out.sort_by(|a, b| parse_version(a).cmp(&parse_version(b)).then_with(|| a.cmp(b)));
    out
}

pub fn remove_version(home: &Path, version: &str) -> Result<PathBuf, String> {
    let v = normalize_version(version)?;
    let p = engines_dir(home).join(&v);
    if !p.is_dir() {
        return Err(format!("engine {v} is not installed ({})", p.display()));
    }
    fs::remove_dir_all(&p).map_err(|e| format!("failed to remove {}: {e}", p.display()))?;
    Ok(p)
}

/// Chemin affiché pour make : séparateurs `/` (le make de MinGW les accepte, et `$(ENGINE_DIR)/make/engine.mk` reste cohérent).
pub fn path_for_make(p: &Path) -> String {
    let s = p.display().to_string();
    if cfg!(windows) { s.replace('\\', "/") } else { s }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::time::{SystemTime, UNIX_EPOCH};

    pub(crate) fn temp_dir(tag: &str) -> PathBuf {
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let d = std::env::temp_dir().join(format!("orosmo_{tag}_{}_{stamp}", std::process::id()));
        fs::create_dir_all(&d).unwrap();
        d
    }

    fn env(pairs: &[(&str, &str)]) -> impl Fn(&str) -> Option<String> {
        let m: HashMap<String, String> = pairs.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect();
        move |k| m.get(k).cloned()
    }

    #[test]
    fn orosmo_home_priority() {
        let e = env(&[("OROSMO_HOME", "/opt/oro"), ("HOME", "/home/u"), ("LOCALAPPDATA", "C:\\Users\\u\\AppData\\Local")]);
        assert_eq!(home_from(&e, false), Some(PathBuf::from("/opt/oro")));
        assert_eq!(home_from(&e, true), Some(PathBuf::from("/opt/oro")));
        let e = env(&[("OROSMO_HOME", ""), ("HOME", "/home/u"), ("LOCALAPPDATA", "C:\\L")]);
        assert_eq!(home_from(&e, false), Some(PathBuf::from("/home/u").join(".orosmo")));
        assert_eq!(home_from(&e, true), Some(PathBuf::from("C:\\L").join("Orosmo")));
        assert_eq!(home_from(env(&[]), false), None);
    }

    #[test]
    fn versions_are_validated() {
        assert_eq!(normalize_version("v0.6.2").unwrap(), "0.6.2");
        assert_eq!(normalize_version("1.0.0-rc1").unwrap(), "1.0.0-rc1");
        for bad in ["", "latest", "../x", "0.6/../..", "0..6", "/etc", "0.6 2"] {
            assert!(normalize_version(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn path_list_remove_on_fake_home() {
        let home = temp_dir("home");
        for v in ["0.10.0", "0.6.2", "0.6.10"] {
            fs::create_dir_all(engines_dir(&home).join(v)).unwrap();
        }
        fs::create_dir_all(engines_dir(&home).join(".tmp-0.7.0-1")).unwrap();
        fs::write(engines_dir(&home).join("0.9.0"), b"not a dir").unwrap();

        assert_eq!(list_versions(&home), ["0.6.2", "0.6.10", "0.10.0"]);
        assert_eq!(engine_path(&home, "v0.6.2"), Some(engines_dir(&home).join("0.6.2")));
        assert_eq!(engine_path(&home, "0.7.0"), None);
        assert_eq!(engine_path(&home, "0.9.0"), None);
        assert_eq!(engine_path(&home, "../engines/0.6.2"), None);

        assert!(remove_version(&home, "0.6.10").is_ok());
        assert!(remove_version(&home, "0.6.10").is_err());
        assert!(remove_version(&home, "..").is_err());
        assert_eq!(list_versions(&home), ["0.6.2", "0.10.0"]);
        let _ = fs::remove_dir_all(&home);
    }
}
