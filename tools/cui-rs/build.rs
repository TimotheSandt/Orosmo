use std::{env, fs, path::PathBuf};

/// Computes a deterministic 64-bit FNV-1a fingerprint of the compiler sources
/// (excluding unit tests) so that any change to the `cui` generator invalidates
/// cached `.gen.h` / `.gen.cpp` files automatically (G113).
fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let src_dir = manifest_dir.join("src");
    let mut files: Vec<PathBuf> = fs::read_dir(&src_dir)
        .expect("failed to read tools/cui-rs/src")
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| {
            path.extension().and_then(|e| e.to_str()) == Some("rs")
                && path.file_name().and_then(|n| n.to_str()) != Some("tests.rs")
        })
        .collect();
    files.sort();

    let mut hash: u64 = 0xcbf29ce484222325;
    let mut mix = |bytes: &[u8]| {
        for b in bytes {
            hash ^= u64::from(*b);
            hash = hash.wrapping_mul(0x100000001b3);
        }
    };

    mix(env!("CARGO_PKG_VERSION").as_bytes());
    mix(b"\n");

    for path in &files {
        println!("cargo:rerun-if-changed={}", path.display());
        if let Some(name) = path.file_name().and_then(|n| n.to_str()) {
            mix(name.as_bytes());
            mix(b"\0");
        }
        let data = fs::read(path)
            .unwrap_or_else(|e| panic!("failed to read {}: {e}", path.display()));
        mix(&data);
        mix(b"\n");
    }

    println!("cargo:rustc-env=CUI_BUILD_FINGERPRINT={hash:016x}");
}
