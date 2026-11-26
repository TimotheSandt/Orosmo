//! Paquet du moteur : somme SHA-256 (`SHA256SUMS` de la release) et extraction sûre et atomique.

use std::fs;
use std::io::{self, Cursor, Read};
use std::path::{Path, PathBuf};
use zip::ZipArchive;

pub fn sha256_hex(bytes: &[u8]) -> String {
    let d = ring::digest::digest(&ring::digest::SHA256, bytes);
    d.as_ref().iter().map(|b| format!("{b:02x}")).collect()
}

/// Somme attendue pour `name` dans un fichier au format de `sha256sum` (`<hex>  <nom>` ou `<hex> *<nom>`).
pub fn expected_sha256(sums: &str, name: &str) -> Option<String> {
    sums.lines().find_map(|line| {
        let (hash, file) = line.trim().split_once(char::is_whitespace)?;
        let file = file.trim_start().trim_start_matches('*');
        let file = file.rsplit(['/', '\\']).next().unwrap_or(file);
        (file == name && hash.len() == 64 && hash.chars().all(|c| c.is_ascii_hexdigit())).then(|| hash.to_ascii_lowercase())
    })
}

#[derive(Debug, PartialEq, Eq)]
pub enum Checksum {
    Verified,
    /// Release sans `SHA256SUMS` (versions antérieures au contrat) : avertissement, pas d'échec.
    NoSumsFile,
}

/// Vérifie `bytes` contre `SHA256SUMS`. Un fichier présent mais sans ligne pour `name`, ou une somme différente : erreur.
pub fn verify_checksum(bytes: &[u8], name: &str, sums: Option<&str>) -> Result<Checksum, String> {
    let Some(sums) = sums else { return Ok(Checksum::NoSumsFile) };
    let expected = expected_sha256(sums, name).ok_or_else(|| format!("SHA256SUMS has no entry for {name}"))?;
    let actual = sha256_hex(bytes);
    if actual == expected {
        Ok(Checksum::Verified)
    } else {
        Err(format!("checksum mismatch for {name}: expected {expected}, got {actual} (corrupted or tampered download)"))
    }
}

/// Relative path made only of normal components, or None if `path` could leave the destination: a `..` component, an
/// absolute path (leading `/` or `\\`), or a drive / stream prefix (`C:`, `file:stream`). `.` and empty components are dropped.
pub fn safe_relative(path: &str) -> Option<PathBuf> {
    if path.starts_with('/') || path.starts_with('\\') {
        return None;
    }
    let mut out = PathBuf::new();
    for part in path.split(['/', '\\']) {
        match part {
            "" | "." => continue,
            ".." => return None,
            p if p.contains(':') => return None,
            p => out.push(p),
        }
    }
    if out.as_os_str().is_empty() { None } else { Some(out) }
}

/// Le paquet a un dossier de tête (`pkg-<plateforme>/…`) : il est retiré.
pub fn strip_top_dir(path: &str) -> &str {
    match path.find('/') {
        Some(i) => &path[i + 1..],
        None => "",
    }
}

/// Extrait l'archive dans `dest_root` (créé au besoin). Chaque entrée doit rester dans `dest_root` (paquet non fiable).
pub fn extract_zip_into(bytes: &[u8], dest_root: &Path) -> Result<usize, String> {
    fs::create_dir_all(dest_root).map_err(|e| format!("failed to create {}: {e}", dest_root.display()))?;
    let mut archive = ZipArchive::new(Cursor::new(bytes)).map_err(|e| format!("failed to open engine package zip: {e}"))?;
    let mut count = 0;
    for i in 0..archive.len() {
        let mut entry = archive.by_index(i).map_err(|e| format!("failed to read zip entry {i}: {e}"))?;
        let raw = entry.name().to_string();
        let rel = strip_top_dir(&raw);
        if rel.is_empty() {
            continue;
        }
        let safe = safe_relative(rel).ok_or_else(|| format!("unsafe path in the engine package: {raw}"))?;
        let dest = dest_root.join(safe);
        if entry.is_dir() {
            fs::create_dir_all(&dest).map_err(|e| format!("failed to create {}: {e}", dest.display()))?;
            continue;
        }
        if let Some(p) = dest.parent() {
            fs::create_dir_all(p).map_err(|e| format!("failed to create {}: {e}", p.display()))?;
        }
        let mut out = fs::File::create(&dest).map_err(|e| format!("failed to create {}: {e}", dest.display()))?;
        io::copy(&mut entry, &mut out).map_err(|e| format!("failed to write {}: {e}", dest.display()))?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            if let Some(mode) = entry.unix_mode() {
                let _ = fs::set_permissions(&dest, fs::Permissions::from_mode(mode & 0o777));
            }
        }
        count += 1;
    }
    Ok(count)
}

fn unique_sibling(dest: &Path, tag: &str) -> PathBuf {
    let stamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    let name = dest.file_name().and_then(|n| n.to_str()).unwrap_or("engine");
    dest.with_file_name(format!(".{tag}-{name}-{}-{stamp}", std::process::id()))
}

/// Extrait dans un dossier temporaire voisin, puis le renomme en `dest` : `dest` est complet ou absent, jamais à moitié.
/// `replace` : un `dest` existant est remplacé (mis de côté, puis supprimé après le renommage).
pub fn install_tree_atomic(bytes: &[u8], dest: &Path, replace: bool) -> Result<usize, String> {
    if dest.exists() && !replace {
        return Err(format!("{} already exists", dest.display()));
    }
    let tmp = unique_sibling(dest, "tmp");
    let count = match extract_zip_into(bytes, &tmp) {
        Ok(n) => n,
        Err(e) => {
            let _ = fs::remove_dir_all(&tmp);
            return Err(e);
        }
    };
    let old = if dest.exists() {
        let old = unique_sibling(dest, "old");
        if let Err(e) = fs::rename(dest, &old) {
            let _ = fs::remove_dir_all(&tmp);
            return Err(format!("failed to move aside {}: {e}", dest.display()));
        }
        Some(old)
    } else {
        None
    };
    if let Err(e) = fs::rename(&tmp, dest) {
        let _ = fs::remove_dir_all(&tmp);
        if let Some(old) = &old {
            let _ = fs::rename(old, dest);
        }
        return Err(format!("failed to move the engine into {}: {e}", dest.display()));
    }
    if let Some(old) = old {
        let _ = fs::remove_dir_all(old);
    }
    Ok(count)
}

/// Contenu du fichier `VERSION` à la racine du paquet, s'il existe.
pub fn zip_version(bytes: &[u8]) -> Option<String> {
    let mut archive = ZipArchive::new(Cursor::new(bytes)).ok()?;
    for i in 0..archive.len() {
        let mut entry = archive.by_index(i).ok()?;
        if strip_top_dir(entry.name()) == "VERSION" {
            let mut s = String::new();
            entry.read_to_string(&mut s).ok()?;
            let v = s.trim().to_string();
            return (!v.is_empty()).then_some(v);
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::home::tests::temp_dir;
    use std::io::Write;
    use zip::write::SimpleFileOptions;

    fn make_zip(entries: &[(&str, &[u8])]) -> Vec<u8> {
        let mut w = zip::ZipWriter::new(Cursor::new(Vec::new()));
        for (name, data) in entries {
            w.start_file(*name, SimpleFileOptions::default()).unwrap();
            w.write_all(data).unwrap();
        }
        w.finish().unwrap().into_inner()
    }

    #[test]
    fn package_paths_cannot_leave_the_destination() {
        assert_eq!(safe_relative("includes/orosmo/core.hpp"), Some(PathBuf::from("includes/orosmo/core.hpp")));
        assert_eq!(safe_relative("./lib//release/x.a"), Some(PathBuf::from("lib/release/x.a")));
        assert_eq!(safe_relative(strip_top_dir("pkg/../Makefile")), None);
        assert_eq!(safe_relative("a/../../b"), None);
        assert_eq!(safe_relative("/etc/passwd"), None);
        assert_eq!(safe_relative("\\server\\share"), None);
        assert_eq!(safe_relative("C:/Windows/x"), None);
        assert_eq!(safe_relative("a\\..\\..\\b"), None);
    }

    #[test]
    fn sha256_known_vector_and_sums_file() {
        assert_eq!(sha256_hex(b"abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        let good = sha256_hex(b"payload");
        let sums = format!("{}  orosmo-linux-x64\n{good}  orosmo-pkg-linux-x64.zip\n", "0".repeat(64));
        assert_eq!(verify_checksum(b"payload", "orosmo-pkg-linux-x64.zip", Some(&sums)), Ok(Checksum::Verified));
        let star = format!("{}  *orosmo-pkg-linux-x64.zip\n", good.to_uppercase());
        assert_eq!(verify_checksum(b"payload", "orosmo-pkg-linux-x64.zip", Some(&star)), Ok(Checksum::Verified));
        // Mauvaise somme, entrée absente : refus. Pas de SHA256SUMS (ancienne release) : accepté avec avertissement.
        assert!(verify_checksum(b"tampered", "orosmo-pkg-linux-x64.zip", Some(&sums)).is_err());
        assert!(verify_checksum(b"payload", "orosmo-pkg-macos-arm64.zip", Some(&sums)).is_err());
        assert_eq!(verify_checksum(b"payload", "x.zip", None), Ok(Checksum::NoSumsFile));
    }

    #[test]
    fn atomic_install_and_refused_archive() {
        let root = temp_dir("install");
        let dest = root.join("engines").join("0.6.2");
        let good = make_zip(&[("pkg-linux-x64/VERSION", b"0.6.2\n"), ("pkg-linux-x64/make/engine.mk", b"# mk\n")]);
        assert_eq!(zip_version(&good).as_deref(), Some("0.6.2"));
        assert_eq!(install_tree_atomic(&good, &dest, false), Ok(2));
        assert!(dest.join("make/engine.mk").is_file());
        assert!(install_tree_atomic(&good, &dest, false).is_err());

        // Une entrée qui sort de la destination : refus, et ni la destination ni un dossier temporaire ne restent.
        let evil = make_zip(&[("pkg/ok.txt", b"x"), ("pkg/../../escape.txt", b"x")]);
        let dest2 = root.join("engines").join("0.7.0");
        assert!(install_tree_atomic(&evil, &dest2, false).unwrap_err().contains("unsafe path"));
        assert!(!dest2.exists());
        assert!(!root.join("escape.txt").exists());
        let leftovers: Vec<_> = fs::read_dir(root.join("engines")).unwrap().flatten().map(|e| e.file_name()).collect();
        assert_eq!(leftovers, vec![std::ffi::OsString::from("0.6.2")]);

        // Remplacement (--force) : l'ancien contenu disparaît.
        let newer = make_zip(&[("pkg/VERSION", b"0.6.2\n")]);
        assert_eq!(install_tree_atomic(&newer, &dest, true), Ok(1));
        assert!(!dest.join("make/engine.mk").exists());
        let _ = fs::remove_dir_all(&root);
    }
}
