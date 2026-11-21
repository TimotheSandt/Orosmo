use serde_json::json;
use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Severity {
    Ok,
    Warn,
    Error,
}

impl Severity {
    fn tag(self) -> &'static str {
        match self {
            Severity::Ok => "[ok]",
            Severity::Warn => "[warn]",
            Severity::Error => "[!!]",
        }
    }

    fn as_str(self) -> &'static str {
        match self {
            Severity::Ok => "ok",
            Severity::Warn => "warn",
            Severity::Error => "error",
        }
    }
}

#[derive(Debug, Clone)]
pub struct CheckResult {
    pub name: String,
    pub required: bool,
    pub severity: Severity,
    pub detail: String,
    pub hint: Option<String>,
}

impl CheckResult {
    fn ok(name: impl Into<String>, required: bool, detail: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            required,
            severity: Severity::Ok,
            detail: detail.into(),
            hint: None,
        }
    }

    fn warn(
        name: impl Into<String>,
        required: bool,
        detail: impl Into<String>,
        hint: impl Into<String>,
    ) -> Self {
        Self {
            name: name.into(),
            required,
            severity: Severity::Warn,
            detail: detail.into(),
            hint: Some(hint.into()),
        }
    }

    fn error(
        name: impl Into<String>,
        detail: impl Into<String>,
        hint: impl Into<String>,
    ) -> Self {
        Self {
            name: name.into(),
            required: true,
            severity: Severity::Error,
            detail: detail.into(),
            hint: Some(hint.into()),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CompilerInfo {
    pub family: &'static str,
    pub major: u32,
    pub minor: u32,
    pub raw_first_line: String,
}

pub fn parse_compiler_version(output: &str) -> Option<CompilerInfo> {
    let first = output.lines().find(|l| !l.trim().is_empty())?.trim();
    let lower = first.to_ascii_lowercase();
    let family = if lower.contains("apple clang") {
        "apple-clang"
    } else if lower.contains("clang") {
        "clang"
    } else if lower.contains("g++") || lower.contains("gcc") || lower.contains("free software foundation") {
        "gcc"
    } else {
        "unknown"
    };

    // Find the first token containing a major.minor version pattern (e.g. 13.3.0, 17.0.6, 14.2.0-1).
    for token in first.split_whitespace() {
        let cleaned = token.trim_matches(|c: char| !c.is_ascii_alphanumeric() && c != '.');
        let mut parts = cleaned.split('.');
        if let (Some(a), Some(b)) = (parts.next(), parts.next()) {
            let b_digits: String = b.chars().take_while(|c| c.is_ascii_digit()).collect();
            if !a.is_empty()
                && !b_digits.is_empty()
                && a.chars().all(|c| c.is_ascii_digit())
                && let (Ok(major), Ok(minor)) = (a.parse::<u32>(), b_digits.parse::<u32>())
            {
                return Some(CompilerInfo {
                    family,
                    major,
                    minor,
                    raw_first_line: first.to_string(),
                });
            }
        }
    }
    None
}

pub fn compiler_supports_cpp23(info: &CompilerInfo) -> bool {
    match info.family {
        "gcc" => info.major >= 13,
        "clang" => info.major >= 17,
        "apple-clang" => info.major >= 15,
        _ => true,
    }
}

fn default_vcpkg_triplet() -> &'static str {
    if cfg!(target_os = "windows") {
        "x64-mingw-static"
    } else if cfg!(target_os = "macos") && cfg!(target_arch = "aarch64") {
        "arm64-osx"
    } else if cfg!(target_os = "macos") {
        "x64-osx"
    } else if cfg!(target_arch = "aarch64") {
        "arm64-linux"
    } else {
        "x64-linux"
    }
}

fn run_capture(program: &str, args: &[&str]) -> Option<(bool, String)> {
    let out = Command::new(program).args(args).output().ok()?;
    let mut text = String::from_utf8_lossy(&out.stdout).into_owned();
    if text.trim().is_empty() {
        text = String::from_utf8_lossy(&out.stderr).into_owned();
    }
    Some((out.status.success(), text))
}

fn first_line(text: &str) -> String {
    text.lines()
        .find(|l| !l.trim().is_empty())
        .unwrap_or("")
        .trim()
        .to_string()
}

fn os_install_hint(linux: &str, macos: &str, windows: &str) -> String {
    if cfg!(target_os = "windows") {
        windows.to_string()
    } else if cfg!(target_os = "macos") {
        macos.to_string()
    } else {
        linux.to_string()
    }
}

fn check_cpp_compiler() -> CheckResult {
    let cxx = env::var("CXX").ok().filter(|v| !v.is_empty());
    let candidates: Vec<String> = match cxx {
        Some(c) => vec![c],
        None => vec!["g++".to_string(), "clang++".to_string()],
    };

    for cand in &candidates {
        if let Some((true, out)) = run_capture(cand, &["--version"]) {
            if let Some(info) = parse_compiler_version(&out) {
                if compiler_supports_cpp23(&info) {
                    return CheckResult::ok(
                        "C++23 compiler",
                        true,
                        format!("{cand} ({} {}.{}, {})", info.family, info.major, info.minor, info.raw_first_line),
                    );
                }
                return CheckResult::error(
                    "C++23 compiler",
                    format!("{cand} is too old ({} {}.{}; need GCC >= 13 or Clang >= 17)", info.family, info.major, info.minor),
                    os_install_hint(
                        "sudo apt install g++-13 (or set CXX=g++-13)",
                        "brew install gcc (or llvm) and set CXX",
                        "Install MSYS2 MinGW-w64 GCC 13+ (pacman -S mingw-w64-x86_64-gcc)",
                    ),
                );
            }
            return CheckResult::ok("C++23 compiler", true, format!("{cand} ({})", first_line(&out)));
        }
    }

    CheckResult::error(
        "C++23 compiler",
        format!("not found (tried: {})", candidates.join(", ")),
        os_install_hint(
            "sudo apt install g++",
            "xcode-select --install  (or brew install gcc)",
            "Install MSYS2 MinGW-w64 GCC 13+ or scoop install mingw",
        ),
    )
}

fn check_tool(
    name: &str,
    program: &str,
    args: &[&str],
    required: bool,
    hint: String,
) -> CheckResult {
    match run_capture(program, args) {
        Some((true, out)) => CheckResult::ok(name, required, first_line(&out)),
        _ if required => CheckResult::error(name, format!("'{program}' not found in PATH"), hint),
        _ => CheckResult::warn(name, false, format!("'{program}' not found (optional)"), hint),
    }
}

fn check_vcpkg() -> CheckResult {
    let mut candidates = Vec::new();
    if let Ok(v) = env::var("VCPKG")
        && !v.is_empty()
    {
        candidates.push(v);
    }
    candidates.push("vcpkg".to_string());
    if let Some(home) = env::var_os("HOME").or_else(|| env::var_os("USERPROFILE")) {
        let exe = if cfg!(windows) { "vcpkg.exe" } else { "vcpkg" };
        let home_vcpkg = PathBuf::from(home).join("vcpkg").join(exe);
        if home_vcpkg.is_file() {
            candidates.push(home_vcpkg.display().to_string());
        }
    }

    for cand in &candidates {
        if let Some((true, out)) = run_capture(cand, &["version"]) {
            return CheckResult::ok("vcpkg", false, format!("{cand}: {}", first_line(&out)));
        }
    }

    CheckResult::warn(
        "vcpkg",
        false,
        "not found (only needed for `make install` to fetch third-party deps)",
        "Install from https://vcpkg.io/en/getting-started or set VCPKG=/path/to/vcpkg",
    )
}

pub fn check_project_layout(project_dir: &Path, triplet: &str) -> Vec<CheckResult> {
    let mut results = Vec::new();
    let sdk_dir = project_dir.join("orosmo");
    let repo_mk = project_dir.join("mk").join("engine.mk");
    let cui_name = if cfg!(windows) { "cui.exe" } else { "cui" };

    if sdk_dir.is_dir() {
        let inc_ok = sdk_dir.join("includes").join("orosmo").is_dir();
        let lib_dbg_ok = sdk_dir.join("lib").join("debug").join("libcore.a").is_file();
        let lib_rel_ok = sdk_dir.join("lib").join("release").join("libcore.a").is_file();
        if inc_ok && (lib_dbg_ok || lib_rel_ok) {
            results.push(CheckResult::ok(
                "Orosmo SDK",
                true,
                format!(
                    "{} (debug: {}, release: {})",
                    sdk_dir.display(),
                    if lib_dbg_ok { "yes" } else { "no" },
                    if lib_rel_ok { "yes" } else { "no" }
                ),
            ));
        } else {
            results.push(CheckResult::error(
                "Orosmo SDK",
                format!("incomplete SDK in {}", sdk_dir.display()),
                "Run `orosmo update` to restore the engine package",
            ));
        }

        let cui_path = sdk_dir.join("tools").join(cui_name);
        if cui_path.is_file() {
            let ver = run_capture(&cui_path.display().to_string(), &["--version"])
                .filter(|(ok, _)| *ok)
                .map(|(_, out)| first_line(&out))
                .unwrap_or_else(|| cui_path.display().to_string());
            results.push(CheckResult::ok("CUI compiler", true, ver));
        } else {
            results.push(CheckResult::error(
                "CUI compiler",
                format!("missing {}", cui_path.display()),
                "Run `orosmo update` to reinstall tools/cui",
            ));
        }

        let vcpkg_dir = sdk_dir.join("vcpkg_installed").join(triplet);
        if vcpkg_dir.is_dir() {
            results.push(CheckResult::ok(
                "SDK dependencies",
                false,
                format!("found {}", vcpkg_dir.display()),
            ));
        } else {
            results.push(CheckResult::warn(
                "SDK dependencies",
                false,
                format!("missing {}", vcpkg_dir.display()),
                "Run `make install` in the project directory",
            ));
        }

        let cmake_cfg = sdk_dir.join("cmake").join("OrosmoConfig.cmake");
        if cmake_cfg.is_file() {
            results.push(CheckResult::ok(
                "CMake package",
                false,
                cmake_cfg.display().to_string(),
            ));
        }
    } else if repo_mk.is_file() {
        let vcpkg_dir = project_dir.join("vcpkg_installed").join(triplet);
        if vcpkg_dir.is_dir() {
            results.push(CheckResult::ok(
                "Repo dependencies",
                false,
                format!("found {}", vcpkg_dir.display()),
            ));
        } else {
            results.push(CheckResult::warn(
                "Repo dependencies",
                false,
                format!("missing {}", vcpkg_dir.display()),
                "Run `make install-deps` (and `sh tools/setup-linux-deps.sh` on Linux)",
            ));
        }

        let cui_path = project_dir.join("tools").join(cui_name);
        if cui_path.is_file() {
            let ver = run_capture(&cui_path.display().to_string(), &["--version"])
                .filter(|(ok, _)| *ok)
                .map(|(_, out)| first_line(&out))
                .unwrap_or_else(|| cui_path.display().to_string());
            results.push(CheckResult::ok("CUI compiler", false, ver));
        } else {
            results.push(CheckResult::warn(
                "CUI compiler",
                false,
                format!("not built yet ({})", cui_path.display()),
                "Run `make -f mk/engine.mk cui-tool`",
            ));
        }
    }

    results
}

fn check_graphics_env() -> Vec<CheckResult> {
    let mut results = Vec::new();
    #[cfg(target_os = "linux")]
    {
        let has_display = env::var("DISPLAY").ok().filter(|v| !v.is_empty()).is_some()
            || env::var("WAYLAND_DISPLAY").ok().filter(|v| !v.is_empty()).is_some();
        if has_display {
            results.push(CheckResult::ok("Display server", false, "DISPLAY / WAYLAND_DISPLAY set"));
        } else {
            results.push(CheckResult::warn(
                "Display server",
                false,
                "neither DISPLAY nor WAYLAND_DISPLAY is set",
                "Use `xvfb-run -a` for headless OpenGL captures or Vulkan headless mode",
            ));
        }

        if let Some((true, out)) = run_capture("vulkaninfo", &["--summary"]) {
            let api = out
                .lines()
                .find(|l| l.contains("Vulkan Instance Version"))
                .map(|l| l.trim().to_string())
                .unwrap_or_else(|| "vulkaninfo available".to_string());
            results.push(CheckResult::ok("Vulkan runtime", false, api));
        } else {
            let has_so = Path::new("/usr/lib/x86_64-linux-gnu/libvulkan.so.1").exists()
                || Path::new("/usr/lib/aarch64-linux-gnu/libvulkan.so.1").exists()
                || Path::new("/usr/lib/libvulkan.so.1").exists();
            if has_so {
                results.push(CheckResult::ok("Vulkan runtime", false, "libvulkan.so.1 found"));
            } else {
                results.push(CheckResult::warn(
                    "Vulkan runtime",
                    false,
                    "libvulkan.so.1 / vulkaninfo not detected (OpenGL remains available)",
                    "sudo apt install libvulkan-dev vulkan-tools vulkan-validationlayers",
                ));
            }
        }
    }
    results
}

pub fn format_json_report(
    engine_version: &str,
    platform_name: &str,
    triplet: &str,
    checks: &[CheckResult],
) -> String {
    let ok = !checks.iter().any(|c| c.severity == Severity::Error);
    let items: Vec<_> = checks
        .iter()
        .map(|c| {
            json!({
                "name": c.name,
                "required": c.required,
                "status": c.severity.as_str(),
                "detail": c.detail,
                "hint": c.hint,
            })
        })
        .collect();
    serde_json::to_string_pretty(&json!({
        "engineVersion": engine_version,
        "platform": platform_name,
        "vcpkgTriplet": triplet,
        "ok": ok,
        "checks": items,
    }))
    .unwrap_or_else(|_| "{}".to_string())
}

pub fn cmd_doctor(args: &[String], engine_version: &str, platform_name: &str) -> Result<(), String> {
    let mut json_mode = false;
    let mut project_dir: Option<PathBuf> = None;

    for arg in args {
        match arg.as_str() {
            "--json" => json_mode = true,
            "-h" | "--help" => {
                eprintln!("Usage: orosmo doctor [--json] [project-path]");
                eprintln!("Checks compiler (C++23), build tools, vcpkg, SDK layout and graphics environment.");
                return Ok(());
            }
            other if other.starts_with('-') => {
                return Err(format!("unknown option for doctor: {other}"));
            }
            path => {
                if project_dir.is_some() {
                    return Err(format!("unexpected extra argument: {path}"));
                }
                project_dir = Some(PathBuf::from(path));
            }
        }
    }

    let dir = project_dir.unwrap_or_else(|| PathBuf::from("."));
    let triplet = default_vcpkg_triplet();

    let mut checks = vec![
        check_cpp_compiler(),
        check_tool(
            "GNU Make",
            "make",
            &["--version"],
            true,
            os_install_hint("sudo apt install make", "xcode-select --install", "scoop install make (or MSYS2 make)"),
        ),
        check_tool(
            "Archiver (ar)",
            "ar",
            &["--version"],
            !cfg!(target_os = "macos"),
            os_install_hint("sudo apt install binutils", "xcode-select --install", "Included with MSYS2 MinGW-w64"),
        ),
        check_vcpkg(),
        check_tool(
            "Python 3",
            if cfg!(windows) { "python" } else { "python3" },
            &["--version"],
            false,
            os_install_hint("sudo apt install python3", "brew install python", "scoop install python"),
        ),
        check_tool(
            "CMake",
            "cmake",
            &["--version"],
            false,
            os_install_hint("sudo apt install cmake", "brew install cmake", "scoop install cmake"),
        ),
    ];
    checks.extend(check_project_layout(&dir, triplet));
    checks.extend(check_graphics_env());

    let errors = checks.iter().filter(|c| c.severity == Severity::Error).count();
    let warnings = checks.iter().filter(|c| c.severity == Severity::Warn).count();

    if json_mode {
        println!("{}", format_json_report(engine_version, platform_name, triplet, &checks));
    } else {
        println!("Orosmo doctor — v{engine_version} ({platform_name}, triplet {triplet})");
        println!("----------------------------------------------------------------");
        for c in &checks {
            println!("  {:6} {:18} {}", c.severity.tag(), c.name, c.detail);
            if let Some(hint) = &c.hint {
                println!("         -> {hint}");
            }
        }
        println!("----------------------------------------------------------------");
        println!("Summary: {} ok, {} warning(s), {} error(s)", checks.len() - warnings - errors, warnings, errors);
    }

    if errors > 0 {
        Err(format!("doctor found {errors} missing required prerequisite(s)"))
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn parses_gcc_and_clang_versions_for_cpp23() {
        let gcc13 = parse_compiler_version("g++ (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0\nCopyright...").unwrap();
        assert_eq!((gcc13.family, gcc13.major, gcc13.minor), ("gcc", 13, 3));
        assert!(compiler_supports_cpp23(&gcc13));

        let gcc12 = parse_compiler_version("g++ (Debian 12.2.0-14) 12.2.0").unwrap();
        assert_eq!((gcc12.family, gcc12.major, gcc12.minor), ("gcc", 12, 2));
        assert!(!compiler_supports_cpp23(&gcc12));

        let clang18 = parse_compiler_version("Ubuntu clang version 18.1.3 (1ubuntu1)").unwrap();
        assert_eq!((clang18.family, clang18.major, clang18.minor), ("clang", 18, 1));
        assert!(compiler_supports_cpp23(&clang18));

        let clang16 = parse_compiler_version("clang version 16.0.6").unwrap();
        assert!(!compiler_supports_cpp23(&clang16));

        let apple = parse_compiler_version("Apple clang version 15.0.0 (clang-1500.3.9.4)").unwrap();
        assert_eq!((apple.family, apple.major, apple.minor), ("apple-clang", 15, 0));
        assert!(compiler_supports_cpp23(&apple));
    }

    #[test]
    fn json_report_reflects_errors_and_ok_state() {
        let ok_checks = vec![CheckResult::ok("GNU Make", true, "GNU Make 4.3")];
        let parsed: serde_json::Value =
            serde_json::from_str(&format_json_report("0.4.0", "linux-x64", "x64-linux", &ok_checks)).unwrap();
        assert_eq!(parsed["ok"], true);
        assert_eq!(parsed["checks"][0]["status"], "ok");

        let err_checks = vec![CheckResult::error("C++23 compiler", "not found", "apt install g++")];
        let parsed_err: serde_json::Value =
            serde_json::from_str(&format_json_report("0.4.0", "linux-x64", "x64-linux", &err_checks)).unwrap();
        assert_eq!(parsed_err["ok"], false);
        assert_eq!(parsed_err["checks"][0]["status"], "error");
        assert_eq!(parsed_err["checks"][0]["hint"], "apt install g++");
    }

    #[test]
    fn detects_sdk_project_layout() {
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let root = env::temp_dir().join(format!("orosmo_doctor_test_{stamp}"));
        let sdk = root.join("orosmo");
        fs::create_dir_all(sdk.join("includes/orosmo")).unwrap();
        fs::create_dir_all(sdk.join("lib/debug")).unwrap();
        fs::create_dir_all(sdk.join("tools")).unwrap();
        fs::create_dir_all(sdk.join("cmake")).unwrap();
        fs::write(sdk.join("lib/debug/libcore.a"), b"!<arch>\n").unwrap();
        let cui_name = if cfg!(windows) { "cui.exe" } else { "cui" };
        fs::write(sdk.join("tools").join(cui_name), b"").unwrap();
        fs::write(sdk.join("cmake/OrosmoConfig.cmake"), b"# cmake\n").unwrap();

        let checks = check_project_layout(&root, "x64-linux");
        assert!(checks.iter().any(|c| c.name == "Orosmo SDK" && c.severity == Severity::Ok));
        assert!(checks.iter().any(|c| c.name == "CUI compiler" && c.severity == Severity::Ok));
        assert!(checks.iter().any(|c| c.name == "SDK dependencies" && c.severity == Severity::Warn));
        assert!(checks.iter().any(|c| c.name == "CMake package" && c.severity == Severity::Ok));

        let _ = fs::remove_dir_all(&root);
    }
}
