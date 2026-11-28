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

/// Contrôles d'un dossier de moteur (paquet de release extrait : `orosmo/` d'un ancien projet ou `engines/<v>`).
fn check_engine_dir(sdk_dir: &Path, repair: &str) -> Vec<CheckResult> {
    let mut results = Vec::new();
    let cui_name = if cfg!(windows) { "cui.exe" } else { "cui" };
    let inc_ok = sdk_dir.join("includes").join("orosmo").is_dir() || sdk_dir.join("include").join("orosmo").is_dir();
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
        results.push(CheckResult::error("Orosmo SDK", format!("incomplete SDK in {}", sdk_dir.display()), repair));
    }

    let cui_path = sdk_dir.join("tools").join(cui_name);
    if cui_path.is_file() {
        let ver = run_capture(&cui_path.display().to_string(), &["--version"])
            .filter(|(ok, _)| *ok)
            .map(|(_, out)| first_line(&out))
            .unwrap_or_else(|| cui_path.display().to_string());
        results.push(CheckResult::ok("CUI compiler", true, ver));
    } else {
        results.push(CheckResult::error("CUI compiler", format!("missing {}", cui_path.display()), repair));
    }

    // Contrat SDK : deps/{include,lib} (bibliothèques tierces précompilées du moteur). Plus de vcpkg_installed/.
    let deps_dir = sdk_dir.join("deps");
    if deps_dir.join("include").is_dir() && deps_dir.join("lib").is_dir() {
        results.push(CheckResult::ok("SDK dependencies", false, format!("found {}", deps_dir.display())));
    } else {
        results.push(CheckResult::error(
            "SDK dependencies",
            format!("missing {}/{{include,lib}}", deps_dir.display()),
            "Engine package without deps/ (older than 0.6.2): `orosmo install latest` then `orosmo update` (or `orosmo migrate`)",
        ));
    }

    let cmake_cfg = sdk_dir.join("cmake").join("OrosmoConfig.cmake");
    if cmake_cfg.is_file() {
        results.push(CheckResult::ok("CMake package", false, cmake_cfg.display().to_string()));
    }
    results
}

/// `ENGINE_DIR` imposé au Makefile du gabarit : `config.local.mk`, puis `config.mk`, puis l'environnement (une affectation dans
/// un fichier inclus l'emporte sur l'environnement). Une valeur calculée (`$(…)`) n'est pas interprétée. Chemin relatif : au projet.
fn engine_dir_override(project_dir: &Path) -> Option<(std::path::PathBuf, &'static str)> {
    // Ordre du gabarit : environnement, puis `-include config.mk`, puis `-include config.local.mk`, avec un état partagé.
    let config = std::fs::read_to_string(project_dir.join("config.mk")).unwrap_or_default();
    let local = std::fs::read_to_string(project_dir.join("config.local.mk")).unwrap_or_default();
    let env = std::env::var("ENGINE_DIR").ok().map(|v| (v, "environment"));
    let (value, from) = crate::project::eval_var(env, &[("config.mk", &config), ("config.local.mk", &local)], "ENGINE_DIR")?;
    // Définie vide : le gabarit passe à orosmo/ puis à OROSMO_VERSION, comme sans ENGINE_DIR.
    if value.trim().is_empty() || value.contains("$(") {
        return None;
    }
    Some((project_dir.join(value.trim()), from))
}

/// `home` : racine OROSMO_HOME (engines/<v>) ; None si elle est introuvable.
pub fn check_project_layout(project_dir: &Path, triplet: &str, home: Option<&Path>) -> Vec<CheckResult> {
    let mut results = Vec::new();
    let sdk_dir = project_dir.join("orosmo");
    let repo_mk = project_dir.join("mk").join("engine.mk");
    let cui_name = if cfg!(windows) { "cui.exe" } else { "cui" };
    let version = std::fs::read_to_string(project_dir.join("config.mk"))
        .ok()
        .and_then(|c| crate::project::read_orosmo_version(&c));

    if let Some(h) = home {
        let installed = crate::home::list_versions(h);
        if installed.is_empty() {
            results.push(CheckResult::warn(
                "Installed engines",
                false,
                format!("none in {}", crate::home::engines_dir(h).display()),
                "Run `orosmo install latest`",
            ));
        } else {
            results.push(CheckResult::ok("Installed engines", false, installed.join(", ")));
        }
    }

    if let Some((dir, from)) = engine_dir_override(project_dir) {
        // Priorité du gabarit : ENGINE_DIR donné passe avant orosmo/ et OROSMO_VERSION.
        if dir.join("make").join("engine.mk").is_file() {
            results.push(CheckResult::ok("Project engine", true, format!("ENGINE_DIR ({from}) -> {}", dir.display())));
            results.extend(check_engine_dir(&dir, &format!("Fix ENGINE_DIR in {from}")));
        } else {
            results.push(CheckResult::error(
                "Project engine",
                format!("ENGINE_DIR ({from}) = {}: no make/engine.mk there", dir.display()),
                format!("Fix or remove ENGINE_DIR in {from}"),
            ));
        }
    } else if sdk_dir.is_dir() {
        results.extend(check_engine_dir(&sdk_dir, "Run `orosmo update` to restore the engine package"));
    } else if let Some(v) = version {
        match home.and_then(|h| crate::home::engine_path(h, &v)) {
            Some(p) => {
                results.push(CheckResult::ok("Project engine", true, format!("OROSMO_VERSION {v} -> {}", p.display())));
                results.extend(check_engine_dir(&p, &format!("Reinstall it: `orosmo install {v} --force`")));
            }
            None => results.push(CheckResult::error(
                "Project engine",
                format!("engine {v} (OROSMO_VERSION in config.mk) is not installed"),
                format!("Run `orosmo install {v}`"),
            )),
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

/// macOS : Vulkan par MoltenVK, seule API (OpenGL s'arrête à 4.1). Le paquet du moteur n'embarque ni chargeur ni pilote
/// (A76) : les programmes cherchent le chargeur dans les dossiers de `ENGINE_VULKAN_RPATHS` (engine.mk), et le chargeur de
/// Homebrew trouve seul le manifeste de MoltenVK. Pur (`exists`, `var`) : testé sur tout système.
#[cfg_attr(not(any(target_os = "macos", test)), allow(dead_code))]
pub fn check_macos_vulkan(exists: impl Fn(&Path) -> bool, var: impl Fn(&str) -> Option<String>) -> Vec<CheckResult> {
    const INSTALL: &str = "brew install vulkan-loader molten-vk";
    let mut results = Vec::new();

    let loader = ["/opt/homebrew/lib", "/usr/local/lib"]
        .iter()
        .flat_map(|dir| ["libvulkan.1.dylib", "libvulkan.dylib"].iter().map(move |lib| format!("{dir}/{lib}")))
        .find(|p| exists(Path::new(p)));
    match loader {
        Some(path) => results.push(CheckResult::ok("Vulkan loader", false, path)),
        None => results.push(CheckResult::warn(
            "Vulkan loader",
            false,
            "libvulkan not found in /opt/homebrew/lib or /usr/local/lib (programs cannot render: OpenGL 4.1 is too old)",
            INSTALL,
        )),
    }

    let forced = ["VK_DRIVER_FILES", "VK_ICD_FILENAMES"]
        .iter()
        .find_map(|name| var(name).filter(|v| !v.is_empty()).map(|v| (*name, v)));
    let driver = match &forced {
        Some((_, list)) => list.split(':').map(str::to_string).find(|p| exists(Path::new(p))),
        None => {
            let mut dirs: Vec<String> = ["/opt/homebrew/etc", "/opt/homebrew/share", "/usr/local/etc", "/usr/local/share", "/etc"]
                .iter()
                .map(|d| d.to_string())
                .collect();
            if let Some(home) = var("HOME") {
                dirs.push(format!("{home}/.config"));
                dirs.push(format!("{home}/.local/share"));
            }
            dirs.iter().map(|d| format!("{d}/vulkan/icd.d/MoltenVK_icd.json")).find(|p| exists(Path::new(p)))
        }
    };
    match (driver, forced) {
        (Some(path), Some((name, _))) => results.push(CheckResult::ok("Vulkan driver", false, format!("{path} ({name})"))),
        (Some(path), None) => results.push(CheckResult::ok("Vulkan driver", false, path)),
        (None, Some((name, list))) => results.push(CheckResult::warn(
            "Vulkan driver",
            false,
            format!("{name} names no existing file: {list}"),
            format!("Unset {name}, or: {INSTALL}"),
        )),
        (None, None) => results.push(CheckResult::warn("Vulkan driver", false, "no MoltenVK_icd.json found", INSTALL)),
    }
    results
}

fn check_graphics_env() -> Vec<CheckResult> {
    #[cfg_attr(not(any(target_os = "linux", target_os = "macos")), allow(unused_mut))]
    let mut results = Vec::new();
    #[cfg(target_os = "macos")]
    results.extend(check_macos_vulkan(|p| p.is_file(), |name| env::var(name).ok()));
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

/// Gestionnaire de paquets du système, pour `doctor --fix`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PkgManager {
    Apt,
    Dnf,
    Pacman,
    Brew,
    /// pacman de MSYS2 (Windows) : chaîne MinGW-w64.
    Msys2,
    Winget,
}

/// Premier gestionnaire présent (`has(programme)`) pour `os` (`linux`, `macos`, `windows`).
pub fn detect_package_manager(has: impl Fn(&str) -> bool, os: &str) -> Option<PkgManager> {
    let order: &[(&str, PkgManager)] = match os {
        "windows" => &[("pacman", PkgManager::Msys2), ("winget", PkgManager::Winget)],
        "macos" => &[("brew", PkgManager::Brew)],
        _ => &[("apt-get", PkgManager::Apt), ("dnf", PkgManager::Dnf), ("pacman", PkgManager::Pacman)],
    };
    order.iter().find(|(prog, _)| has(prog)).map(|(_, pm)| *pm)
}

/// Commande qui installe les paquets système du développement Orosmo (compilateur, make, Python, en-têtes de fenêtrage et
/// chargeur Vulkan : ce que le paquet du moteur n'embarque jamais). Les bibliothèques tierces du moteur viennent de deps/.
pub fn fix_command(pm: PkgManager) -> Vec<String> {
    let (head, pkgs): (&[&str], &[&str]) = match pm {
        PkgManager::Apt => (&["sudo", "apt-get", "install"], &[
            "build-essential", "make", "python3", "libgl-dev", "libx11-dev", "libxrandr-dev", "libxinerama-dev",
            "libxcursor-dev", "libxi-dev", "libwayland-dev", "libxkbcommon-dev", "libvulkan-dev",
        ]),
        PkgManager::Dnf => (&["sudo", "dnf", "install"], &[
            "gcc-c++", "make", "python3", "mesa-libGL-devel", "libX11-devel", "libXrandr-devel", "libXinerama-devel",
            "libXcursor-devel", "libXi-devel", "wayland-devel", "libxkbcommon-devel", "vulkan-loader-devel",
        ]),
        PkgManager::Pacman => (&["sudo", "pacman", "-S", "--needed"], &[
            "base-devel", "python", "mesa", "libx11", "libxrandr", "libxinerama", "libxcursor", "libxi", "wayland",
            "libxkbcommon", "vulkan-icd-loader",
        ]),
        PkgManager::Brew => (&["brew", "install"], &["make", "python", "vulkan-loader", "molten-vk"]),
        PkgManager::Msys2 => (&["pacman", "-S", "--needed"], &[
            "mingw-w64-x86_64-gcc", "mingw-w64-x86_64-make", "mingw-w64-x86_64-python",
        ]),
        PkgManager::Winget => (&["winget", "install", "-e", "--id"], &["MSYS2.MSYS2"]),
    };
    head.iter().chain(pkgs).map(|s| s.to_string()).collect()
}

fn current_os() -> &'static str {
    if cfg!(windows) { "windows" } else if cfg!(target_os = "macos") { "macos" } else { "linux" }
}

/// Affiche la commande ; ne la lance qu'après une confirmation tapée dans un terminal (jamais de sudo sans accord).
fn run_fix() -> Result<(), String> {
    use std::io::{IsTerminal, Write};
    let has = |prog: &str| Command::new(prog).arg("--version").output().is_ok_and(|o| o.status.success());
    let Some(pm) = detect_package_manager(has, current_os()) else {
        println!("doctor --fix: no supported package manager found (apt, dnf, pacman, brew, winget / MSYS2).");
        println!("Install a C++23 compiler, GNU Make and Python 3 by hand (see `orosmo doctor`).");
        return Ok(());
    };
    let cmd = fix_command(pm);
    println!();
    println!("Package manager: {pm:?}. Command:");
    println!("  {}", cmd.join(" "));
    if pm == PkgManager::Winget {
        println!("Then, in the MSYS2 shell: pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-python");
    }
    if pm == PkgManager::Brew {
        println!("(The compiler comes from the Xcode command line tools: xcode-select --install)");
    }
    if !std::io::stdin().is_terminal() {
        println!("Not an interactive terminal: run the command yourself.");
        return Ok(());
    }
    print!("Run it now? [y/N] ");
    let _ = std::io::stdout().flush();
    let mut answer = String::new();
    std::io::stdin().read_line(&mut answer).map_err(|e| format!("cannot read the answer: {e}"))?;
    if !answer.trim().eq_ignore_ascii_case("y") {
        println!("Not run.");
        return Ok(());
    }
    let status = Command::new(&cmd[0])
        .args(&cmd[1..])
        .status()
        .map_err(|e| format!("cannot run {}: {e}", cmd[0]))?;
    if status.success() {
        println!("Done; run `orosmo doctor` again to check.");
        Ok(())
    } else {
        Err(format!("{} failed ({status})", cmd.join(" ")))
    }
}

pub fn cmd_doctor(args: &[String], engine_version: &str, platform_name: &str) -> Result<(), String> {
    let mut json_mode = false;
    let mut fix = false;
    let mut project_dir: Option<PathBuf> = None;

    for arg in args {
        match arg.as_str() {
            "--json" => json_mode = true,
            "--fix" => fix = true,
            "-h" | "--help" => {
                eprintln!("Usage: orosmo doctor [--json] [--fix] [project-path]");
                eprintln!("Checks compiler (C++23), build tools, installed engines, project engine and graphics environment.");
                eprintln!("--fix: shows the system package command (apt, dnf, pacman, brew, winget / MSYS2) and runs it after confirmation.");
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

    if fix && json_mode {
        return Err("--fix cannot be combined with --json".to_string());
    }
    let dir = project_dir.unwrap_or_else(|| PathBuf::from("."));
    let triplet = default_vcpkg_triplet();

    let mut checks = vec![
        check_cpp_compiler(),
        check_tool(
            "GNU Make",
            &crate::makerun::make_program(),
            &["--version"],
            true,
            os_install_hint("sudo apt install make", "xcode-select --install", "scoop install make (or MSYS2 make)"),
        ),
        // Le `ar` d'Apple ne connaît pas `--version` : `xcrun --find ar` rend son chemin.
        check_tool(
            "Archiver (ar)",
            if cfg!(target_os = "macos") { "xcrun" } else { "ar" },
            if cfg!(target_os = "macos") { &["--find", "ar"] } else { &["--version"] },
            !cfg!(target_os = "macos"),
            os_install_hint("sudo apt install binutils", "xcode-select --install", "Included with MSYS2 MinGW-w64"),
        ),
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
    let home = crate::home::orosmo_home().ok();
    checks.extend(check_project_layout(&dir, triplet, home.as_deref()));
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

    if fix {
        run_fix()?;
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

    fn mac_checks(files: &[&str], vars: &[(&str, &str)]) -> Vec<CheckResult> {
        check_macos_vulkan(
            |p| files.iter().any(|f| Path::new(f) == p),
            |k| vars.iter().find(|(n, _)| *n == k).map(|(_, v)| v.to_string()),
        )
    }

    #[test]
    fn macos_reports_the_homebrew_loader_and_moltenvk_driver() {
        let checks = mac_checks(
            &["/opt/homebrew/lib/libvulkan.1.dylib", "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"],
            &[],
        );
        assert_eq!(checks.len(), 2);
        assert_eq!((checks[0].name.as_str(), checks[0].severity), ("Vulkan loader", Severity::Ok));
        assert!(checks[0].detail.contains("/opt/homebrew/lib/libvulkan.1.dylib"));
        assert_eq!((checks[1].name.as_str(), checks[1].severity), ("Vulkan driver", Severity::Ok));
        assert!(checks[1].detail.contains("MoltenVK_icd.json"));
    }

    #[test]
    fn macos_without_vulkan_gives_the_brew_command_and_names_the_missing_fallback() {
        let checks = mac_checks(&[], &[]);
        for c in &checks {
            assert_eq!(c.severity, Severity::Warn, "{}", c.name);
            assert!(!c.required);
            assert!(c.hint.as_deref().unwrap().contains("brew install vulkan-loader molten-vk"));
        }
        assert!(checks[0].detail.contains("OpenGL 4.1"));
    }

    #[test]
    fn macos_driver_files_variable_wins_over_the_default_manifests() {
        let brew_icd = "/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json";
        let ok = mac_checks(&["/sdk/icd.json", brew_icd], &[("VK_DRIVER_FILES", "/nope.json:/sdk/icd.json")]);
        assert_eq!(ok[1].severity, Severity::Ok);
        assert!(ok[1].detail.contains("/sdk/icd.json") && ok[1].detail.contains("VK_DRIVER_FILES"));
        let broken = mac_checks(&[brew_icd], &[("VK_ICD_FILENAMES", "/nope.json")]);
        assert_eq!(broken[1].severity, Severity::Warn);
        assert!(broken[1].detail.contains("VK_ICD_FILENAMES"));
        let home = mac_checks(&["/Users/a/.local/share/vulkan/icd.d/MoltenVK_icd.json"], &[("HOME", "/Users/a")]);
        assert_eq!(home[1].severity, Severity::Ok);
    }

    #[test]
    fn brew_fix_command_installs_the_loader_and_the_driver() {
        let brew = fix_command(PkgManager::Brew);
        assert!(brew.iter().any(|p| p == "vulkan-loader") && brew.iter().any(|p| p == "molten-vk"));
    }

    #[test]
    fn detects_package_manager_and_fix_command() {
        let only = |names: &'static [&'static str]| move |p: &str| names.contains(&p);
        assert_eq!(detect_package_manager(only(&["dnf", "pacman"]), "linux"), Some(PkgManager::Dnf));
        assert_eq!(detect_package_manager(only(&["apt-get", "dnf"]), "linux"), Some(PkgManager::Apt));
        assert_eq!(detect_package_manager(only(&["brew", "apt-get"]), "macos"), Some(PkgManager::Brew));
        assert_eq!(detect_package_manager(only(&["winget"]), "windows"), Some(PkgManager::Winget));
        assert_eq!(detect_package_manager(only(&["pacman", "winget"]), "windows"), Some(PkgManager::Msys2));
        assert_eq!(detect_package_manager(only(&[]), "linux"), None);
        let apt = fix_command(PkgManager::Apt);
        assert_eq!(&apt[..3], ["sudo", "apt-get", "install"]);
        assert!(apt.iter().any(|p| p == "libvulkan-dev") && !apt.iter().any(|p| p.contains("vcpkg")));
        assert_eq!(fix_command(PkgManager::Brew)[0], "brew");
    }

    #[test]
    fn project_engine_from_orosmo_home() {
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let root = env::temp_dir().join(format!("orosmo_doctor_home_{stamp}"));
        let project = root.join("game");
        let home = root.join("home");
        fs::create_dir_all(&project).unwrap();
        fs::write(project.join("config.mk"), "OROSMO_VERSION := 0.6.2\n").unwrap();
        let checks = check_project_layout(&project, "x64-linux", Some(&home));
        assert!(checks.iter().any(|c| c.name == "Project engine" && c.severity == Severity::Error));
        assert!(checks.iter().any(|c| c.name == "Installed engines" && c.severity == Severity::Warn));

        let engine = home.join("engines/0.6.2");
        fs::create_dir_all(engine.join("include/orosmo")).unwrap();
        fs::create_dir_all(engine.join("lib/release")).unwrap();
        fs::create_dir_all(engine.join("deps/lib")).unwrap();
        fs::create_dir_all(engine.join("deps/include")).unwrap();
        fs::write(engine.join("lib/release/libcore.a"), b"!<arch>\n").unwrap();
        let checks = check_project_layout(&project, "x64-linux", Some(&home));
        assert!(checks.iter().any(|c| c.name == "Project engine" && c.severity == Severity::Ok));
        assert!(checks.iter().any(|c| c.name == "Orosmo SDK" && c.severity == Severity::Ok));
        assert!(checks.iter().any(|c| c.name == "SDK dependencies" && c.severity == Severity::Ok));
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn engine_dir_override_comes_first() {
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let root = env::temp_dir().join(format!("orosmo_doctor_engdir_{stamp}"));
        let project = root.join("game");
        let home = root.join("home");
        let sdk = root.join("sdk");
        fs::create_dir_all(&project).unwrap();
        fs::create_dir_all(sdk.join("make")).unwrap();
        fs::create_dir_all(sdk.join("include/orosmo")).unwrap();
        fs::create_dir_all(sdk.join("lib/release")).unwrap();
        fs::create_dir_all(sdk.join("deps/lib")).unwrap();
        fs::create_dir_all(sdk.join("deps/include")).unwrap();
        fs::create_dir_all(sdk.join("tools")).unwrap();
        fs::write(sdk.join("tools").join(if cfg!(windows) { "cui.exe" } else { "cui" }), b"").unwrap();
        fs::write(sdk.join("make/engine.mk"), b"# mk\n").unwrap();
        fs::write(sdk.join("lib/release/libcore.a"), b"!<arch>\n").unwrap();
        // OROSMO_VERSION 0.6.2 absent de OROSMO_HOME, mais ENGINE_DIR désigne un moteur valide : pas d'erreur.
        fs::write(project.join("config.mk"), "OROSMO_VERSION := 0.6.2\n").unwrap();
        fs::write(project.join("config.local.mk"), format!("ENGINE_DIR := {}\n", sdk.display())).unwrap();
        let checks = check_project_layout(&project, "x64-linux", Some(&home));
        assert!(checks.iter().any(|c| c.name == "Project engine" && c.severity == Severity::Ok), "{:?}", checks.iter().map(|c| &c.name).collect::<Vec<_>>());
        assert!(!checks.iter().any(|c| c.severity == Severity::Error), "{:?}", checks.iter().map(|c| (&c.name, &c.detail)).collect::<Vec<_>>());
        // `ENGINE_DIR :=` vide dans config.local.mk : la valeur de config.mk n'est pas reprise, résolution normale.
        fs::write(project.join("config.mk"), "OROSMO_VERSION := 0.6.2\nENGINE_DIR := missing\n").unwrap();
        fs::write(project.join("config.local.mk"), "ENGINE_DIR :=\n").unwrap();
        let checks = check_project_layout(&project, "x64-linux", Some(&home));
        assert!(!checks.iter().any(|c| c.detail.contains("missing")));
        // `?=` local après `:=` de config.mk : config.mk gagne.
        fs::write(project.join("config.mk"), format!("ENGINE_DIR := {}\n", sdk.display())).unwrap();
        fs::write(project.join("config.local.mk"), "ENGINE_DIR ?= missing\n").unwrap();
        let checks = check_project_layout(&project, "x64-linux", Some(&home));
        assert!(checks.iter().any(|c| c.name == "Project engine" && c.severity == Severity::Ok));
        fs::write(project.join("config.mk"), "OROSMO_VERSION := 0.6.2\n").unwrap();
        // ENGINE_DIR qui ne désigne pas un moteur : erreur qui nomme ENGINE_DIR.
        fs::write(project.join("config.local.mk"), "ENGINE_DIR := nowhere\n").unwrap();
        let checks = check_project_layout(&project, "x64-linux", Some(&home));
        assert!(checks.iter().any(|c| c.name == "Project engine" && c.severity == Severity::Error && c.detail.contains("ENGINE_DIR")));
        let _ = fs::remove_dir_all(&root);
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

        let checks = check_project_layout(&root, "x64-linux", None);
        assert!(checks.iter().any(|c| c.name == "Orosmo SDK" && c.severity == Severity::Ok));
        assert!(checks.iter().any(|c| c.name == "CUI compiler" && c.severity == Severity::Ok));
        // Un vcpkg_installed/ (ancien paquet) ne remplace plus deps/.
        fs::create_dir_all(sdk.join("vcpkg_installed/x64-linux/lib")).unwrap();
        let checks = check_project_layout(&root, "x64-linux", None);
        assert!(checks.iter().any(|c| c.name == "SDK dependencies" && c.severity == Severity::Error));
        assert!(checks.iter().any(|c| c.name == "CMake package" && c.severity == Severity::Ok));

        let _ = fs::remove_dir_all(&root);
    }
}
