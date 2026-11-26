//! Dossier utilisateur des moteurs installés (`OROSMO_HOME`, contrat SDK A76).
//!
//! ```text
//! $OROSMO_HOME/bin/orosmo[.exe]
//! $OROSMO_HOME/engines/<x.y.z>/   contenu du paquet de release, extrait tel quel
//! ```
//! `OROSMO_HOME` (variable d'environnement non vide) est prioritaire ; sinon `~/.orosmo` (Linux, macOS) ou
//! `%LOCALAPPDATA%\Orosmo` (Windows).

use std::cmp::Ordering;
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

/// Préversion (`rc1` dans `1.0.0-rc1+build`), None pour une version finale.
fn pre_release(v: &str) -> Option<&str> {
    let v = v.trim().trim_start_matches('v');
    let v = v.split('+').next().unwrap_or(v);
    v.split_once('-').map(|(_, pre)| pre)
}

/// Préséance des versions : `x.y.z` comparés comme nombres (`0.6.10` après `0.6.2`), puis une préversion avant sa version
/// finale (`1.0.0-rc1` avant `1.0.0`) ; à égalité, ordre lexical. Sert à `list_versions` (donc au repli hors ligne) et à `is_newer`.
pub fn compare_versions(a: &str, b: &str) -> Ordering {
    let key = |v: &str| (parse_version(v), pre_release(v).is_none(), pre_release(v).map(str::to_string));
    key(a).cmp(&key(b)).then_with(|| a.cmp(b))
}

pub fn is_newer(remote_tag: &str, local: &str) -> bool {
    parse_version(remote_tag).is_some()
        && parse_version(local).is_some()
        && compare_versions(remote_tag.trim_start_matches('v'), local.trim_start_matches('v')) == Ordering::Greater
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
    out.sort_by(|a, b| compare_versions(a, b));
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
/// GNU Make découpe les mots sur les espaces : sous Windows, un chemin qui en contient (`C:\Users\Prénom Nom\…`) est donné sous
/// sa forme courte 8.3 quand le volume en a une (G214) ; sinon le Makefile du gabarit refuse le chemin avec un message clair.
pub fn path_for_make(p: &Path) -> String {
    #[cfg(windows)]
    {
        let s = p.display().to_string();
        let s = if s.contains(' ') { short_path(p).unwrap_or(s) } else { s };
        s.replace('\\', "/")
    }
    #[cfg(not(windows))]
    {
        p.display().to_string()
    }
}

/// Forme courte (8.3) d'un chemin existant, par `GetShortPathNameW` ; None si le volume n'en a pas ou en cas d'échec.
#[cfg(windows)]
fn short_path(p: &Path) -> Option<String> {
    use std::os::windows::ffi::{OsStrExt, OsStringExt};
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetShortPathNameW(long_path: *const u16, short_path: *mut u16, len: u32) -> u32;
    }
    let wide: Vec<u16> = p.as_os_str().encode_wide().chain(std::iter::once(0)).collect();
    // SAFETY : `wide` est terminé par un zéro ; un tampon nul de longueur 0 demande la taille nécessaire.
    let needed = unsafe { GetShortPathNameW(wide.as_ptr(), std::ptr::null_mut(), 0) };
    if needed == 0 {
        return None;
    }
    let mut buf = vec![0u16; needed as usize];
    // SAFETY : `buf` a `needed` éléments ; la fonction écrit au plus `needed` caractères, zéro final compris.
    let written = unsafe { GetShortPathNameW(wide.as_ptr(), buf.as_mut_ptr(), needed) };
    if written == 0 || written >= needed {
        return None;
    }
    buf.truncate(written as usize);
    let short = std::ffi::OsString::from_wide(&buf).to_string_lossy().into_owned();
    (!short.contains(' ')).then_some(short)
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
    fn version_precedence() {
        assert!(is_newer("v1.0.0", "1.0.0-rc1"));
        assert!(!is_newer("v1.0.0-rc1", "1.0.0"));
        assert!(!is_newer("v1.0.0", "1.0.0"));
        assert!(is_newer("v0.6.10", "0.6.2"));
        assert!(!is_newer("v0.6.2", "0.6.10"));
        assert!(!is_newer("latest", "0.6.2"));
        assert_eq!(compare_versions("1.0.0+b2", "1.0.0-rc1"), Ordering::Greater);
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
        // Une version finale passe après sa préversion (repli hors ligne : la finale est la plus récente).
        for v in ["1.0.0", "1.0.0-rc1", "1.0.0-rc2"] {
            fs::create_dir_all(engines_dir(&home).join(v)).unwrap();
        }
        assert_eq!(list_versions(&home), ["0.6.2", "0.6.10", "0.10.0", "1.0.0-rc1", "1.0.0-rc2", "1.0.0"]);
        for v in ["1.0.0", "1.0.0-rc1", "1.0.0-rc2"] {
            fs::remove_dir_all(engines_dir(&home).join(v)).unwrap();
        }
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
