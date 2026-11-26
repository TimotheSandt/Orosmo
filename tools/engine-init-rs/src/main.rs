mod doctor;
mod home;
mod install;
mod makerun;
mod project;

use std::fs;
use std::io::{self, Cursor, Read};
use std::path::{Path, PathBuf};
use zip::ZipArchive;

use home::{engine_path, is_newer, list_versions, normalize_version, orosmo_home, path_for_make};
use install::{Checksum, install_tree_atomic, verify_checksum, zip_version};
use makerun::Output;

thread_local! {
    static LOCAL_PACKAGE: std::cell::RefCell<Option<PathBuf>> = const { std::cell::RefCell::new(None) };
}

// ── Embedded templates (set by build.rs) ─────────────────────────────────────
// The engine package itself is never embedded: `orosmo install` downloads it (or `--package <zip>` offline).

const TPL_SRC_ZIP:   &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/templates/src.zip"));
const TPL_MAKEFILE:  &str  = include_str!(concat!(env!("OUT_DIR"), "/templates/Makefile"));
const TPL_CONFIG_MK: &str  = include_str!(concat!(env!("OUT_DIR"), "/templates/config.mk"));
const TPL_GITIGNORE: &str  = include_str!(concat!(env!("OUT_DIR"), "/templates/gitignore"));

// Written once, gitignored — not hash-tracked.
const TPL_CONFIG_LOCAL_MK: &str = "\
# Machine-specific overrides — gitignored, do NOT commit.\n\
# Uncomment and set values relevant to your setup.\n\
\n\
# ENGINE_DIR  := /path/to/engine   # default: orosmo/ if present, else `orosmo path $(OROSMO_VERSION)`\n\
# _ENGINE_MK  := $(ENGINE_DIR)/make/engine.mk\n\
";

const HASH_TAG: &str = "# @engine-template-hash:";
const SUMS_ASSET: &str = "SHA256SUMS";

/// `OROSMO_DOWNLOAD_BASE` : dossier local (ou URL http(s)) qui contient les fichiers d'une release (`orosmo-pkg-<plateforme>.zip`,
/// `SHA256SUMS`) ; remplace l'API du dépôt. Sert aux tests de bout en bout et aux miroirs ; install.sh lit la même variable.
fn download_base() -> Option<String> {
    std::env::var("OROSMO_DOWNLOAD_BASE").ok().map(|b| b.trim_end_matches('/').to_string()).filter(|b| !b.is_empty())
}

fn fetch_from_base(base: &str, name: &str) -> Result<Vec<u8>, String> {
    if base.starts_with("http://") || base.starts_with("https://") {
        return download_bytes(&format!("{base}/{name}"), name);
    }
    let dir = base.strip_prefix("file://").unwrap_or(base);
    let path = Path::new(dir).join(name);
    fs::read(&path).map_err(|e| format!("failed to read {}: {e}", path.display()))
}

/// Paquet du moteur pris dans `OROSMO_DOWNLOAD_BASE`, vérifié contre son `SHA256SUMS` s'il y en a un.
fn package_from_base(base: &str) -> Result<Vec<u8>, String> {
    let pkg_name = engine_pkg_name();
    println!("  Downloading {pkg_name} from {base} (OROSMO_DOWNLOAD_BASE)...");
    let bytes = fetch_from_base(base, &pkg_name)?;
    let sums = fetch_from_base(base, SUMS_ASSET).ok().map(|b| String::from_utf8_lossy(&b).into_owned());
    match verify_checksum(&bytes, &pkg_name, sums.as_deref())? {
        Checksum::Verified => println!("  [ok] SHA-256 verified"),
        Checksum::NoSumsFile => eprintln!("  Warning: no {SUMS_ASSET} in {base}; {pkg_name} is not verified"),
    }
    Ok(bytes)
}

// ── Engine identity (baked in from mk/config.mk via build.rs) ─────────

const ENGINE_NAME:        &str = env!("ENGINE_NAME");
const ENGINE_VERSION:     &str = env!("ENGINE_VERSION");
const ENGINE_PUBLISHER:   &str = env!("ENGINE_PUBLISHER");
const ENGINE_DESCRIPTION: &str = env!("ENGINE_DESCRIPTION");
const ENGINE_HOMEPAGE:    &str = env!("ENGINE_HOMEPAGE");

// ── Release host (baked in from git remote + Makefile) ───────────────────────

const REPO_HOST:   &str = env!("REPO_HOST");    // e.g. "github.com" or "gitlab.com"
const GITHUB_REPO: &str = env!("GITHUB_REPO");  // owner/repo slug

fn platform() -> &'static str {
    if cfg!(target_os = "windows") {
        "windows-x64"
    } else if cfg!(target_os = "macos") && cfg!(target_arch = "aarch64") {
        "macos-arm64"
    } else if cfg!(target_os = "macos") {
        "macos-x64"
    } else if cfg!(target_arch = "aarch64") {
        "linux-arm64"
    } else {
        "linux-x64"
    }
}

fn tool_asset_name() -> String {
    if cfg!(windows) {
        format!("orosmo-{}.exe", platform())
    } else {
        format!("orosmo-{}", platform())
    }
}

fn engine_pkg_name() -> String {
    format!("orosmo-pkg-{}.zip", platform())
}

// ── Entry point ───────────────────────────────────────────────────────────────

fn main() {
    let code = match run() {
        Ok(code) => code,
        Err(e) => {
            eprintln!("error: {e}");
            1
        }
    };
    std::process::exit(code);
}

// `--package <zip>` : paquet local (téléchargé à la main, ou construit par `make engine-tool`) au lieu de la release.
fn take_package_option(args: &mut Vec<String>) -> Result<Option<PathBuf>, String> {
    // Rien après `--` (arguments du programme de `orosmo run`) n'est une option de l'outil.
    let end = args.iter().position(|a| a == "--").unwrap_or(args.len());
    if let Some(i) = args[..end].iter().position(|a| a == "--package") {
        if i + 1 >= end {
            return Err("--package expects the path of an orosmo-pkg-<platform>.zip".to_string());
        }
        let path = PathBuf::from(args.remove(i + 1));
        args.remove(i);
        if !path.is_file() {
            return Err(format!("package not found: {}", path.display()));
        }
        return Ok(Some(path));
    }
    Ok(None)
}

fn ok(r: Result<(), String>) -> Result<i32, String> {
    r.map(|()| 0)
}

fn run() -> Result<i32, String> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let package = take_package_option(&mut args)?;
    LOCAL_PACKAGE.with(|p| *p.borrow_mut() = package);
    let cmd = args.first().map(String::as_str).unwrap_or("");
    let rest = if args.is_empty() { &[][..] } else { &args[1..] };

    match cmd {
        "install"       => ok(cmd_install(rest)),
        "list"          => ok(cmd_list()),
        "remove"        => ok(cmd_remove(rest)),
        "path"          => ok(cmd_path(rest)),
        "init"          => ok(cmd_init(rest)),
        "update"        => ok(cmd_update(rest)),
        "migrate"       => cmd_migrate(rest),
        "build"         => cmd_build(rest),
        "run"           => cmd_run(rest),
        "clean"         => cmd_clean(rest),
        "self-update"   => ok(cmd_self_update(rest)),
        "doctor"        => ok(doctor::cmd_doctor(rest, ENGINE_VERSION, platform())),
        "-V" | "--version" | "version" => { println!("{ENGINE_NAME} {ENGINE_VERSION}"); Ok(0) }
        "-h" | "--help" | "help" | "" => { print_usage(); Ok(0) }
        other => {
            print_usage();
            Err(format!("unknown command: {other}"))
        }
    }
}

/// Retire `flag` de `args` ; vrai s'il y était.
fn take_flag(args: &mut Vec<String>, flag: &str) -> bool {
    let before = args.len();
    args.retain(|a| a != flag);
    args.len() != before
}

fn reject_unknown_options(args: &[String], cmd: &str) -> Result<(), String> {
    match args.iter().find(|a| a.starts_with('-')) {
        Some(o) => Err(format!("unknown option for {cmd}: {o}")),
        None => Ok(()),
    }
}

// ── orosmo install / list / remove / path ─────────────────────────────────────

fn cmd_install(args: &[String]) -> Result<(), String> {
    let mut args = args.to_vec();
    let force = take_flag(&mut args, "--force");
    reject_unknown_options(&args, "install")?;
    if args.len() > 1 {
        return Err("usage: orosmo install [version|latest] [--force]".to_string());
    }
    let requested = args.first().map(String::as_str).filter(|v| *v != "latest");
    let home = orosmo_home()?;
    let (version, path) = install_version(&home, requested, force)?;
    println!("Engine {version} installed in {}", path.display());
    Ok(())
}

fn cmd_list() -> Result<(), String> {
    let home = orosmo_home()?;
    let versions = list_versions(&home);
    if versions.is_empty() {
        println!("No engine installed in {} (run `orosmo install latest`).", home::engines_dir(&home).display());
        return Ok(());
    }
    let current = fs::read_to_string("config.mk").ok().and_then(|c| project::read_orosmo_version(&c));
    for v in versions {
        let mark = if current.as_deref() == Some(v.as_str()) { "  (this project)" } else { "" };
        println!("{v}{mark}");
    }
    Ok(())
}

fn cmd_remove(args: &[String]) -> Result<(), String> {
    let [v] = args else { return Err("usage: orosmo remove <version>".to_string()) };
    let p = home::remove_version(&orosmo_home()?, v)?;
    println!("Removed {}", p.display());
    Ok(())
}

/// Utilisé par le Makefile (`$(shell orosmo path $(OROSMO_VERSION))`) : seulement le chemin sur stdout ; code 1 si absent.
fn cmd_path(args: &[String]) -> Result<(), String> {
    let [v] = args else { return Err("usage: orosmo path <version>".to_string()) };
    let home = orosmo_home()?;
    match engine_path(&home, v) {
        Some(p) => {
            println!("{}", path_for_make(&p));
            Ok(())
        }
        None => Err(format!("engine {v} is not installed; run `orosmo install {v}`")),
    }
}

/// Installe `requested` (None : la dernière release) dans `$OROSMO_HOME/engines/<v>` ; renvoie la version et le dossier.
/// Déjà installée (sans `force`) : rien n'est téléchargé.
fn install_version(home: &Path, requested: Option<&str>, force: bool) -> Result<(String, PathBuf), String> {
    let requested = requested.map(normalize_version).transpose()?;
    let engines = home::engines_dir(home);

    if let Some(pkg) = LOCAL_PACKAGE.with(|p| p.borrow().clone()) {
        let bytes = fs::read(&pkg).map_err(|e| format!("failed to read {}: {e}", pkg.display()))?;
        let version = match requested.clone().or_else(|| zip_version(&bytes)) {
            Some(v) => normalize_version(&v)?,
            None => {
                eprintln!("  Warning: {} has no VERSION file; assuming {ENGINE_VERSION}", pkg.display());
                ENGINE_VERSION.to_string()
            }
        };
        if !force && let Some(p) = engine_path(home, &version) {
            println!("  Engine {version} already installed (--force replaces it with {}).", pkg.display());
            return Ok((version, p));
        }
        println!("  Using local package {} (engine {version}, no checksum)...", pkg.display());
        return install_bytes(&engines, &version, &bytes, force);
    }

    if let Some(v) = &requested
        && !force
        && let Some(p) = engine_path(home, v)
    {
        println!("  Engine {v} already installed.");
        return Ok((v.clone(), p));
    }

    if let Some(base) = download_base() {
        let bytes = package_from_base(&base)?;
        let version = normalize_version(&zip_version(&bytes)
            .ok_or_else(|| format!("{} in {base} has no VERSION file", engine_pkg_name()))?)?;
        if let Some(v) = &requested
            && *v != version
        {
            return Err(format!("{base} holds engine {version}, not {v}"));
        }
        if !force && let Some(p) = engine_path(home, &version) {
            println!("  Engine {version} already installed.");
            return Ok((version, p));
        }
        return install_bytes(&engines, &version, &bytes, force);
    }

    let release = match &requested {
        None => Release::fetch_latest(),
        Some(v) => Release::fetch_tag(v),
    }?;
    let version = normalize_version(&release.tag)?;
    if !force && let Some(p) = engine_path(home, &version) {
        println!("  Engine {version} already installed.");
        return Ok((version, p));
    }
    let pkg_name = engine_pkg_name();
    let url = release.find_asset(&pkg_name)
        .ok_or_else(|| format!("release {} has no asset '{pkg_name}'", release.tag))?;
    println!("  Downloading {pkg_name} ({})...", release.tag);
    let bytes = download_bytes(url, &pkg_name)?;
    check_download(&release, &bytes, &pkg_name)?;
    install_bytes(&engines, &version, &bytes, force)
}

/// Vérifie `bytes` contre le `SHA256SUMS` de la release ; une release sans ce fichier n'échoue pas (avertissement).
fn check_download(release: &Release, bytes: &[u8], name: &str) -> Result<(), String> {
    let sums = match release.find_asset(SUMS_ASSET) {
        Some(url) => Some(String::from_utf8_lossy(&download_bytes(url, SUMS_ASSET)?).into_owned()),
        None => None,
    };
    match verify_checksum(bytes, name, sums.as_deref())? {
        Checksum::Verified => println!("  [ok] SHA-256 verified"),
        Checksum::NoSumsFile => eprintln!("  Warning: release {} publishes no {SUMS_ASSET}; {name} is not verified", release.tag),
    }
    Ok(())
}

fn install_bytes(engines: &Path, version: &str, bytes: &[u8], force: bool) -> Result<(String, PathBuf), String> {
    let dest = engines.join(version);
    let count = install_tree_atomic(bytes, &dest, force)?;
    println!("  [ok] engines/{version} ({count} files)");
    Ok((version.to_string(), dest))
}

/// Dernière version (installée au besoin). Hors ligne : la plus récente déjà installée, avec un avertissement.
fn latest_engine(home: &Path) -> Result<(String, PathBuf), String> {
    match install_version(home, None, false) {
        Ok(r) => Ok(r),
        Err(e) => {
            let newest = list_versions(home).pop().ok_or(e.clone())?;
            eprintln!("  Warning: {e}; using the newest installed engine {newest}");
            let p = engine_path(home, &newest).ok_or(e)?;
            Ok((newest, p))
        }
    }
}

// ── orosmo init [project-name] [--vendor] ─────────────────────────────────────

fn cmd_init(args: &[String]) -> Result<(), String> {
    let mut args = args.to_vec();
    let vendor = take_flag(&mut args, "--vendor");
    reject_unknown_options(&args, "init")?;
    let (name, dir, in_place) = match args.first() {
        Some(n) => {
            validate_name(n)?;
            let d = PathBuf::from(n);
            if d.exists() {
                return Err(format!("directory '{}' already exists", d.display()));
            }
            (n.clone(), d, false)
        }
        None => {
            let cwd = std::env::current_dir()
                .map_err(|e| format!("failed to get current directory: {e}"))?;
            let n = cwd
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("project")
                .to_string();
            validate_name(&n)?;
            (n, PathBuf::from("."), true)
        }
    };

    println!("Initializing project '{name}'...");

    // Le moteur d'abord : un échec ne laisse pas de projet à moitié créé.
    let (version, label) = if vendor {
        let release = fetch_release_or_warn();
        fs::create_dir_all(&dir).map_err(|e| format!("failed to create project directory: {e}"))?;
        let v = extract_engine(&dir, release.as_ref())?;
        (v.clone(), format!("{v}, vendored in orosmo/"))
    } else {
        let home = orosmo_home()?;
        let (v, p) = latest_engine(&home)?;
        fs::create_dir_all(&dir).map_err(|e| format!("failed to create project directory: {e}"))?;
        (v.clone(), format!("{v}, {}", p.display()))
    };

    create_sources(&dir, &name)?;
    write_tracked_file(&dir.join("Makefile"),     &render(TPL_MAKEFILE,  &name), TPL_MAKEFILE)?;
    write_tracked_file(&dir.join(".gitignore"),   &render(TPL_GITIGNORE, &name), TPL_GITIGNORE)?;
    let config = project::set_orosmo_version(&render_version(TPL_CONFIG_MK, &name, &version), &version);
    write_once_file(&dir.join("config.mk"),       &config)?;
    write_once_file(&dir.join("config.local.mk"), TPL_CONFIG_LOCAL_MK)?;

    println!();
    println!("Project '{name}' ready (engine {label}).");
    if !in_place {
        println!("  cd {name}");
    }
    println!("  orosmo build      # or: make debug");
    println!("  orosmo run");
    Ok(())
}

fn render_version(template: &str, name: &str, version: &str) -> String {
    render(template, name).replace("{{OROSMO_VERSION}}", version)
}

fn project_name(project_dir: &Path) -> String {
    let from_config = fs::read_to_string(project_dir.join("config.mk")).ok()
        .and_then(|c| project::read_var(&c, "PROJECT_NAME"))
        .filter(|n| !n.contains("{{") && !n.contains("$("));
    from_config.unwrap_or_else(|| {
        std::path::absolute(project_dir).ok()
            .and_then(|p| p.file_name().and_then(|n| n.to_str()).map(str::to_string))
            .unwrap_or_else(|| "project".to_string())
    })
}

/// Écrit `OROSMO_VERSION := version` dans config.mk (ajoute aussi les nouveaux champs du gabarit). Renvoie l'ancienne valeur.
fn write_project_version(project_dir: &Path, version: &str) -> Result<Option<String>, String> {
    let path = project_dir.join("config.mk");
    let old = fs::read_to_string(&path).ok().and_then(|c| project::read_orosmo_version(&c));
    let template = project::set_orosmo_version(&TPL_CONFIG_MK.replace("{{OROSMO_VERSION}}", version), version);
    merge_config_mk(&path, &template, project_dir)?;
    let content = fs::read_to_string(&path).map_err(|e| format!("failed to read {}: {e}", path.display()))?;
    let updated = project::set_orosmo_version(&content, version);
    if updated != content {
        fs::write(&path, updated).map_err(|e| format!("failed to write {}: {e}", path.display()))?;
        println!("  [updated] {} (OROSMO_VERSION := {version})", path.display());
    }
    Ok(old)
}

fn update_template_files(project_dir: &Path) -> Result<(), String> {
    update_tracked_file(&project_dir.join("Makefile"),   TPL_MAKEFILE,  project_dir)?;
    update_tracked_file(&project_dir.join(".gitignore"), TPL_GITIGNORE, project_dir)?;
    write_once_file(&project_dir.join("config.local.mk"), TPL_CONFIG_LOCAL_MK)
}

// ── orosmo update [path] ──────────────────────────────────────────────────────

fn cmd_update(args: &[String]) -> Result<(), String> {
    reject_unknown_options(args, "update")?;
    let project_dir = args.first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."));
    let vendored = project_dir.join("orosmo").is_dir();
    if !vendored && !project_dir.join("config.mk").is_file() {
        return Err(format!(
            "'{}' does not look like an Orosmo project (no config.mk)",
            project_dir.display()
        ));
    }

    if vendored {
        // Ancien projet (ou `init --vendor`) : le moteur copié dans orosmo/ est remplacé, comme avant.
        let release = fetch_release_or_warn();
        if let Some(r) = &release {
            println!("  Latest: {}", r.tag);
        }
        let version = extract_engine(&project_dir, release.as_ref())?;
        update_template_files(&project_dir)?;
        write_project_version(&project_dir, &version)?;
        update_tool_binary(&project_dir, release.as_ref())?;
        println!("Done. This project vendors the engine in orosmo/; `orosmo migrate` switches it to the shared engines in OROSMO_HOME.");
        return Ok(());
    }

    let home = orosmo_home()?;
    let (version, path) = latest_engine(&home)?;
    update_template_files(&project_dir)?;
    let old = write_project_version(&project_dir, &version)?;
    match old {
        Some(o) if o != version => println!("Done: engine {o} -> {version} ({})", path.display()),
        _ => println!("Done: engine {version} ({})", path.display()),
    }
    Ok(())
}

// ── orosmo migrate [path] ─────────────────────────────────────────────────────

fn cmd_migrate(args: &[String]) -> Result<i32, String> {
    reject_unknown_options(args, "migrate")?;
    let project_dir = args.first().map(PathBuf::from).unwrap_or_else(|| PathBuf::from("."));
    let vendored = project_dir.join("orosmo");
    let old_dir = project_dir.join("orosmo.old");
    if !vendored.is_dir() {
        return Err(format!("'{}' has no orosmo/ directory: nothing to migrate", project_dir.display()));
    }
    if old_dir.exists() {
        return Err(format!("{} already exists: remove it (or restore it as orosmo/) first", old_dir.display()));
    }

    // Même version que le moteur copié si possible (fichier VERSION du paquet), sinon la dernière.
    let home = orosmo_home()?;
    let vendored_version = fs::read_to_string(vendored.join("VERSION")).ok()
        .and_then(|v| normalize_version(&v).ok());
    let (version, path) = match &vendored_version {
        Some(v) => install_version(&home, Some(v), false).or_else(|e| {
            eprintln!("  Warning: cannot install engine {v} ({e}); using the latest release instead");
            latest_engine(&home)
        })?,
        None => latest_engine(&home)?,
    };
    update_template_files(&project_dir)?;
    write_project_version(&project_dir, &version)?;

    fs::rename(&vendored, &old_dir).map_err(|e| format!("failed to rename orosmo/ to orosmo.old/: {e}"))?;
    println!("  [moved] orosmo/ -> orosmo.old/ (kept until the project builds with engine {version})");
    println!("Building with {}...", path.display());
    let code = makerun::run_make(&project_dir, &build_goals(false), Output::Human)?;
    if code == 0 {
        fs::remove_dir_all(&old_dir).map_err(|e| format!("build ok, but failed to remove {}: {e}", old_dir.display()))?;
        println!("Migrated: the project now uses engine {version} from {}; orosmo.old/ removed.", path.display());
    } else {
        eprintln!("Build failed: the previous engine is kept in orosmo.old/ (rename it back to orosmo/ to undo).");
    }
    Ok(code)
}

// ── orosmo build / run / clean ────────────────────────────────────────────────

struct BuildOptions {
    release: bool,
    json: bool,
    dir: PathBuf,
    program_args: Vec<String>,
}

fn parse_build_options(args: &[String], cmd: &str, allow_json: bool) -> Result<BuildOptions, String> {
    let mut o = BuildOptions { release: false, json: false, dir: PathBuf::from("."), program_args: Vec::new() };
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--release" => o.release = true,
            "--debug" => o.release = false,
            "--json" if allow_json => o.json = true,
            "-C" => o.dir = PathBuf::from(it.next().ok_or("-C expects a project directory")?),
            "--" => {
                o.program_args = it.by_ref().cloned().collect();
            }
            other => return Err(format!("unknown argument for {cmd}: {other}")),
        }
    }
    if !o.dir.join("Makefile").is_file() {
        return Err(format!("no Makefile in '{}': not an Orosmo project directory", o.dir.display()));
    }
    Ok(o)
}

fn build_goals(release: bool) -> Vec<String> {
    let jobs = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1);
    vec![format!("-j{jobs}"), if release { "release" } else { "debug" }.to_string()]
}

fn cmd_build(args: &[String]) -> Result<i32, String> {
    let o = parse_build_options(args, "build", true)?;
    if !o.program_args.is_empty() {
        return Err("build takes no program arguments (see `orosmo run -- …`)".to_string());
    }
    makerun::run_make(&o.dir, &build_goals(o.release), if o.json { Output::Json } else { Output::Human })
}

fn cmd_run(args: &[String]) -> Result<i32, String> {
    let o = parse_build_options(args, "run", false)?;
    let code = makerun::run_make(&o.dir, &build_goals(o.release), Output::Human)?;
    if code != 0 {
        return Ok(code);
    }
    // Mêmes chemins que le Makefile du gabarit : bin/debug/main, bin/release/$(PROJECT_NAME).
    let exe = if cfg!(windows) { ".exe" } else { "" };
    let rel = if o.release {
        PathBuf::from("bin").join("release").join(format!("{}{exe}", project_name(&o.dir)))
    } else {
        PathBuf::from("bin").join("debug").join(format!("main{exe}"))
    };
    let bin = std::path::absolute(o.dir.join(&rel)).map_err(|e| format!("invalid path {}: {e}", rel.display()))?;
    if !bin.is_file() {
        return Err(format!("built program not found: {}", bin.display()));
    }
    println!("Running {}", rel.display());
    let status = std::process::Command::new(&bin)
        .args(&o.program_args)
        .current_dir(&o.dir)
        .status()
        .map_err(|e| format!("failed to start {}: {e}", bin.display()))?;
    Ok(status.code().unwrap_or(1))
}

fn cmd_clean(args: &[String]) -> Result<i32, String> {
    let o = parse_build_options(args, "clean", false)?;
    makerun::run_make(&o.dir, &["clean".to_string()], Output::Human)
}

// ── orosmo self-update ────────────────────────────────────────────────────────

fn cmd_self_update(args: &[String]) -> Result<(), String> {
    let mut args = args.to_vec();
    let force = take_flag(&mut args, "--force");
    reject_unknown_options(&args, "self-update")?;
    let release = Release::fetch_latest()?;
    if !force && !is_newer(&release.tag, ENGINE_VERSION) {
        println!("orosmo {ENGINE_VERSION} is up to date (latest release: {}).", release.tag);
        return Ok(());
    }
    let exe = std::env::current_exe().map_err(|e| format!("cannot locate the running tool: {e}"))?;
    let asset = tool_asset_name();
    let url = release.find_asset(&asset)
        .ok_or_else(|| format!("release {} has no asset '{asset}'", release.tag))?;
    println!("  Downloading {asset} ({})...", release.tag);
    let bytes = download_bytes(url, &asset)?;
    check_download(&release, &bytes, &asset)?;
    replace_binary(&exe, &bytes)?;
    println!("orosmo updated {ENGINE_VERSION} -> {} ({})", release.tag, exe.display());
    Ok(())
}

/// Remplace un exécutable : nouveau fichier écrit à côté puis renommé (sous Windows, l'ancien, peut-être en cours
/// d'exécution, est d'abord renommé en `.old`).
fn replace_binary(dest: &Path, bytes: &[u8]) -> Result<(), String> {
    let new = dest.with_extension("new");
    fs::write(&new, bytes).map_err(|e| format!("failed to write {}: {e}", new.display()))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = fs::set_permissions(&new, fs::Permissions::from_mode(0o755));
    }
    #[cfg(windows)]
    {
        let backup = dest.with_extension("old");
        let _ = fs::remove_file(&backup);
        if dest.exists() {
            fs::rename(dest, &backup).map_err(|e| format!("failed to move aside {}: {e}", dest.display()))?;
        }
    }
    if let Err(e) = fs::rename(&new, dest) {
        #[cfg(windows)]
        let _ = fs::rename(dest.with_extension("old"), dest);
        let _ = fs::remove_file(&new);
        return Err(format!("failed to replace {}: {e}", dest.display()));
    }
    Ok(())
}

// ── GitHub release ────────────────────────────────────────────────────────────

fn fetch_release_or_warn() -> Option<Release> {
    if LOCAL_PACKAGE.with(|p| p.borrow().is_some()) || download_base().is_some() {
        return None; // paquet local ou OROSMO_DOWNLOAD_BASE : pas d'API du dépôt
    }
    println!("Fetching latest engine release from {REPO_HOST}...");
    match Release::fetch_latest() {
        Ok(r) => Some(r),
        Err(e) => {
            eprintln!("  Warning: cannot reach {REPO_HOST} ({e})");
            None
        }
    }
}

struct Release {
    tag:    String,
    assets: Vec<(String, String)>, // (name, browser_download_url)
}

fn api_get(url: &str) -> Result<serde_json::Value, String> {
    let resp = ureq::get(url)
        .set("User-Agent", &format!("{ENGINE_NAME}/{ENGINE_VERSION}"))
        .set("Accept",     "application/vnd.github+json")
        .call()
        .map_err(|e| format!("{REPO_HOST} API error: {e}"))?;
    resp.into_json().map_err(|e| format!("failed to parse release JSON: {e}"))
}

impl Release {
    fn fetch_latest() -> Result<Self, String> {
        if REPO_HOST == "github.com" {
            Self::from_github(&api_get(&format!("https://api.github.com/repos/{GITHUB_REPO}/releases/latest"))?)
        } else if REPO_HOST.contains("gitlab") {
            // GitLab returns an array sorted newest-first.
            let body = api_get(&Self::gitlab_base())?;
            Self::from_gitlab(body.as_array().and_then(|a| a.first()).ok_or("no releases found on GitLab")?)
        } else {
            Err(Self::unsupported())
        }
    }

    /// Release d'une version donnée (tag `v<x.y.z>`, ou `<x.y.z>` pour un dépôt qui ne préfixe pas ses tags).
    fn fetch_tag(version: &str) -> Result<Self, String> {
        let mut last = String::new();
        for tag in [format!("v{version}"), version.to_string()] {
            let r = if REPO_HOST == "github.com" {
                api_get(&format!("https://api.github.com/repos/{GITHUB_REPO}/releases/tags/{tag}")).and_then(|b| Self::from_github(&b))
            } else if REPO_HOST.contains("gitlab") {
                api_get(&format!("{}/{tag}", Self::gitlab_base())).and_then(|b| Self::from_gitlab(&b))
            } else {
                return Err(Self::unsupported());
            };
            match r {
                Ok(rel) => return Ok(rel),
                Err(e) => last = e,
            }
        }
        Err(format!("no release for engine {version} ({last})"))
    }

    fn unsupported() -> String {
        format!("unsupported git host '{REPO_HOST}' — only github.com and gitlab.* are supported")
    }

    fn gitlab_base() -> String {
        // We URL-encode the owner/repo slug as the project id.
        format!("https://{REPO_HOST}/api/v4/projects/{}/releases", GITHUB_REPO.replace('/', "%2F"))
    }

    fn from_github(body: &serde_json::Value) -> Result<Self, String> {
        let tag = body["tag_name"].as_str().ok_or("missing tag_name in GitHub response")?.to_string();
        let assets = body["assets"].as_array()
            .map(|arr| arr.iter().filter_map(|a| {
                Some((a["name"].as_str()?.to_string(),
                      a["browser_download_url"].as_str()?.to_string()))
            }).collect())
            .unwrap_or_default();
        Ok(Self { tag, assets })
    }

    fn from_gitlab(release: &serde_json::Value) -> Result<Self, String> {
        let tag = release["tag_name"].as_str().ok_or("missing tag_name in GitLab response")?.to_string();
        // GitLab generic-package / release assets live under assets.links
        let assets = release["assets"]["links"].as_array()
            .map(|arr| arr.iter().filter_map(|a| {
                Some((a["name"].as_str()?.to_string(),
                      a["url"].as_str()?.to_string()))
            }).collect())
            .unwrap_or_default();
        Ok(Self { tag, assets })
    }

    fn find_asset(&self, name: &str) -> Option<&str> {
        self.assets.iter()
            .find(|(n, _)| n == name)
            .map(|(_, url)| url.as_str())
    }
}

fn download_bytes(url: &str, label: &str) -> Result<Vec<u8>, String> {
    let resp = ureq::get(url)
        .set("User-Agent", &format!("{ENGINE_NAME}/{ENGINE_VERSION}"))
        .call()
        .map_err(|e| format!("download of {label} failed: {e}"))?;

    let mut bytes = Vec::new();
    resp.into_reader()
        .read_to_end(&mut bytes)
        .map_err(|e| format!("failed to read {label}: {e}"))?;
    Ok(bytes)
}

// ── Vendored engine (`init --vendor`, old projects) ───────────────────────────

/// Extrait le moteur dans `<projet>/orosmo/` (ancien comportement). Renvoie sa version.
fn extract_engine(project_dir: &Path, release: Option<&Release>) -> Result<String, String> {
    if let Some(path) = LOCAL_PACKAGE.with(|p| p.borrow().clone()) {
        println!("  Using local package {}...", path.display());
        let bytes = fs::read(&path).map_err(|e| format!("failed to read {}: {e}", path.display()))?;
        return extract_vendored(project_dir, &bytes, None);
    }
    if let Some(base) = download_base() {
        let bytes = package_from_base(&base)?;
        return extract_vendored(project_dir, &bytes, None);
    }
    let pkg_name = engine_pkg_name();
    let Some(r) = release else {
        return Err(format!(
            "no engine package available: {REPO_HOST} unreachable.\n\
             Download {pkg_name} from {REPO_HOST}/{GITHUB_REPO}/releases and pass it with --package <zip>."
        ));
    };
    let url = r.find_asset(&pkg_name).ok_or_else(|| format!("release {} has no asset '{pkg_name}'", r.tag))?;
    println!("  Downloading {} ({})...", pkg_name, r.tag);
    let bytes = download_bytes(url, &pkg_name)?;
    check_download(r, &bytes, &pkg_name)?;
    extract_vendored(project_dir, &bytes, Some(&r.tag))
}

fn extract_vendored(project_dir: &Path, bytes: &[u8], tag: Option<&str>) -> Result<String, String> {
    // Extraction par-dessus (pas de remplacement atomique) : les fichiers propres à un ancien projet dans orosmo/ restent.
    let dest = project_dir.join("orosmo");
    let count = install::extract_zip_into(bytes, &dest)?;
    println!("  [ok] orosmo/ ({count} files)");
    let version = tag.map(str::to_string).or_else(|| zip_version(bytes)).unwrap_or_else(|| ENGINE_VERSION.to_string());
    normalize_version(&version)
}

// ── Tool binary in an old project ─────────────────────────────────────────────
// If orosmo[.exe] exists in the project dir and a newer release is available,
// replaces it with the binary from the release.

fn update_tool_binary(project_dir: &Path, release: Option<&Release>) -> Result<(), String> {
    let ext = if cfg!(windows) { ".exe" } else { "" };
    let dest = project_dir.join(format!("orosmo{ext}"));
    if !dest.is_file() {
        return Ok(());
    }
    let Some(release) = release else {
        println!("  [skip] orosmo{ext} (release host unreachable, binary not updated)");
        return Ok(());
    };
    if !is_newer(&release.tag, ENGINE_VERSION) {
        println!("  [skip] orosmo{ext} (already up to date — v{ENGINE_VERSION})");
        return Ok(());
    }
    let asset_name = tool_asset_name();
    let url = release.find_asset(&asset_name)
        .ok_or_else(|| format!("release {} has no asset '{asset_name}'", release.tag))?;
    println!("  Downloading {} ({})...", asset_name, release.tag);
    let bytes = download_bytes(url, &asset_name)?;
    check_download(release, &bytes, &asset_name)?;
    replace_binary(&dest, &bytes)?;
    println!("  [ok] orosmo{ext} (updated {} → {})", ENGINE_VERSION, release.tag);
    Ok(())
}

// ── Source files ──────────────────────────────────────────────────────────────

fn create_sources(project_dir: &Path, name: &str) -> Result<(), String> {
    let cursor = Cursor::new(TPL_SRC_ZIP);
    let mut archive = ZipArchive::new(cursor)
        .map_err(|e| format!("failed to read embedded src template: {e}"))?;

    let mut count = 0;
    for i in 0..archive.len() {
        let mut entry = archive.by_index(i)
            .map_err(|e| format!("failed to read src entry {i}: {e}"))?;
        if entry.is_dir() { continue; }

        let rel  = entry.name().to_string();
        let dest = project_dir.join("src").join(&rel);
        if let Some(parent) = dest.parent() {
            fs::create_dir_all(parent)
                .map_err(|e| format!("failed to create {}: {e}", parent.display()))?;
        }

        let mut data = Vec::new();
        io::copy(&mut entry, &mut data)
            .map_err(|e| format!("failed to read src/{rel}: {e}"))?;

        let content = if is_text_file(&rel) {
            render(&String::from_utf8_lossy(&data), name).into_bytes()
        } else {
            data
        };
        fs::write(&dest, &content)
            .map_err(|e| format!("failed to write {}: {e}", dest.display()))?;
        count += 1;
    }

    println!("  [ok] src/ ({count} files)");
    Ok(())
}

fn is_text_file(name: &str) -> bool {
    matches!(
        Path::new(name).extension().and_then(|e| e.to_str()).unwrap_or(""),
        "cpp" | "h" | "hpp" | "c" | "inl" | "glsl" | "vert" | "frag" | "cui" | "txt" | "md"
    )
}

// ── config.mk merge ───────────────────────────────────────────────────────────

fn merge_config_mk(path: &Path, template: &str, project_dir: &Path) -> Result<(), String> {
    let project_name = project_dir
        .file_name().and_then(|n| n.to_str()).unwrap_or("project");
    let rendered = render(template, project_name);

    if !path.exists() {
        fs::write(path, &rendered)
            .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
        println!("  [created] {}", path.display());
        return Ok(());
    }

    let existing = fs::read_to_string(path)
        .map_err(|e| format!("failed to read {}: {e}", path.display()))?;

    let defined: Vec<String> = existing.lines()
        .filter(|l| !l.starts_with(HASH_TAG))
        .filter_map(|line| {
            let t = line.trim();
            if t.starts_with('#') || t.is_empty() { return None; }
            let pos = t.find("?=").or_else(|| t.find(":="))?;
            Some(t[..pos].trim().to_owned())
        })
        .collect();

    let new_lines: Vec<String> = rendered.lines()
        .filter_map(|line| {
            let t = line.trim();
            if t.starts_with('#') || t.is_empty() { return None; }
            let pos = t.find("?=").or_else(|| t.find(":="))?;
            let var = t[..pos].trim();
            if defined.contains(&var.to_owned()) { return None; }
            Some(line.to_owned())
        })
        .collect();

    if new_lines.is_empty() {
        println!("  [skip] {} (no new fields)", path.display());
        return Ok(());
    }

    let added: Vec<&str> = new_lines.iter()
        .filter_map(|line| {
            let pos = line.find("?=").or_else(|| line.find(":="))?;
            Some(line[..pos].trim())
        })
        .collect();

    let mut content = existing;
    if !content.ends_with('\n') { content.push('\n'); }
    content.push_str("\n# Added by orosmo update\n");
    for line in &new_lines {
        content.push_str(line);
        content.push('\n');
    }
    fs::write(path, &content)
        .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
    println!("  [updated] {} (added: {})", path.display(), added.join(", "));
    Ok(())
}

// ── Template file helpers ─────────────────────────────────────────────────────

fn template_hash(content: &str) -> String {
    let mut h = 0xcbf29ce484222325u64;
    for b in content.as_bytes() {
        h ^= u64::from(*b);
        h = h.wrapping_mul(0x100000001b3);
    }
    format!("{h:016x}")
}

fn write_tracked_file(path: &Path, content: &str, template: &str) -> Result<(), String> {
    let tagged = format!("{HASH_TAG} {}\n{content}", template_hash(template));
    fs::write(path, &tagged)
        .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
    println!("  [ok] {}", path.display());
    Ok(())
}

fn write_once_file(path: &Path, content: &str) -> Result<(), String> {
    if path.exists() {
        println!("  [skip] {} (already exists)", path.display());
        return Ok(());
    }
    fs::write(path, content)
        .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
    println!("  [ok] {}", path.display());
    Ok(())
}

fn update_tracked_file(path: &Path, new_template: &str, project_dir: &Path) -> Result<(), String> {
    let new_hash = template_hash(new_template);
    let project_name = project_dir
        .file_name().and_then(|n| n.to_str()).unwrap_or("project");
    let new_content = render(new_template, project_name);
    let new_tagged  = format!("{HASH_TAG} {new_hash}\n{new_content}");

    if !path.exists() {
        fs::write(path, &new_tagged)
            .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
        println!("  [created] {}", path.display());
        return Ok(());
    }

    let existing = fs::read_to_string(path)
        .map_err(|e| format!("failed to read {}: {e}", path.display()))?;

    let stored_hash = existing
        .lines().next()
        .and_then(|l| l.strip_prefix(&format!("{HASH_TAG} ")))
        .map(str::trim)
        .unwrap_or("");

    if stored_hash == new_hash {
        println!("  [skip] {} (template unchanged)", path.display());
        return Ok(());
    }

    let body = existing.lines().skip(1).collect::<Vec<_>>().join("\n") + "\n";
    if body.trim() == new_content.trim() {
        fs::write(path, &new_tagged)
            .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
        println!("  [updated] {}", path.display());
    } else {
        println!("  [!] {} has local modifications. Update anyway? [y/N]", path.display());
        let mut input = String::new();
        io::stdin().read_line(&mut input).ok();
        if input.trim().eq_ignore_ascii_case("y") {
            fs::write(path, &new_tagged)
                .map_err(|e| format!("failed to write {}: {e}", path.display()))?;
            println!("  [updated] {}", path.display());
        } else {
            println!("  [kept] {}", path.display());
        }
    }

    Ok(())
}

// ── Render / utilities ────────────────────────────────────────────────────────

fn render(template: &str, project_name: &str) -> String {
    let guard = project_name.to_uppercase().replace(['-', ' '], "_");
    let lower = project_name.to_lowercase().replace(' ', "-");
    template
        .replace("{{PROJECT_NAME}}", project_name)
        .replace("{{PROJECT_NAME_LOWER}}", &lower)
        .replace("{{GUARD}}", &guard)
}

fn validate_name(name: &str) -> Result<(), String> {
    if name.is_empty() {
        return Err("project name cannot be empty".to_string());
    }
    if name.chars().any(|c| matches!(c, '/' | '\\' | ':' | '*' | '?' | '"' | '<' | '>' | '|')) {
        return Err(format!("invalid character in project name: '{name}'"));
    }
    Ok(())
}

fn print_usage() {
    eprintln!("{ENGINE_NAME} v{ENGINE_VERSION}");
    if !ENGINE_PUBLISHER.is_empty() { eprintln!("  by {ENGINE_PUBLISHER}"); }
    if !ENGINE_DESCRIPTION.is_empty() { eprintln!("  {ENGINE_DESCRIPTION}"); }
    if !ENGINE_HOMEPAGE.is_empty() { eprintln!("  {ENGINE_HOMEPAGE}"); }
    eprintln!();
    eprintln!("Engines:");
    eprintln!("  orosmo install [version|latest] [--force]  download, verify (SHA256SUMS) and install an engine");
    eprintln!("  orosmo list                       installed engine versions");
    eprintln!("  orosmo remove <version>           delete an installed engine");
    eprintln!("  orosmo path <version>             print the engine directory (exit code 1 if not installed)");
    eprintln!();
    eprintln!("Projects:");
    eprintln!("  orosmo init [name] [--vendor]     create a project (current directory if no name); installs the engine if needed");
    eprintln!("  orosmo update [path]              move the project to the latest engine, refresh Makefile and .gitignore");
    eprintln!("  orosmo migrate [path]             old project with orosmo/ -> shared engine (orosmo/ removed after a successful build)");
    eprintln!("  orosmo build [--release] [--json] [-C dir]   build with make (debug by default)");
    eprintln!("  orosmo run [--release] [-C dir] [-- args...] build, then run the program with args");
    eprintln!("  orosmo clean [-C dir]             make clean");
    eprintln!();
    eprintln!("Tool:");
    eprintln!("  orosmo self-update [--force]      replace this tool with the one of the latest release");
    eprintln!("  orosmo doctor [--json] [--fix] [path]  check compiler, system packages and engine; --fix proposes the install command");
    eprintln!("  --package <orosmo-pkg.zip>        (install, init, update) use this local package instead of downloading");
    eprintln!("  OROSMO_DOWNLOAD_BASE=<dir|url>    take the release files (package, SHA256SUMS) from there instead of {REPO_HOST}");
    eprintln!();
    eprintln!("Engines live in $OROSMO_HOME/engines/<version> (default: ~/.orosmo, %LOCALAPPDATA%\\Orosmo on Windows).");
    eprintln!("A project names its engine in config.mk (OROSMO_VERSION := x.y.z); its Makefile finds it with `orosmo path`.");
    eprintln!("Releases come from {REPO_HOST}/{GITHUB_REPO}.");
    eprintln!();
    eprintln!("`init` creates: src/ (starter C++ project), Makefile and .gitignore (updated by `orosmo update`),");
    eprintln!("config.mk (yours to edit), config.local.mk (machine-specific, gitignored).");
    eprintln!("`init --vendor` copies the engine into orosmo/ instead (old behaviour; `update` then replaces orosmo/).");
    eprintln!();
    eprintln!("build --json prints one JSON object per line: {{\"event\":\"begin|end|diagnostic|done\", ...}}; the exit code is make's.");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_template_gets_the_engine_version() {
        assert!(TPL_CONFIG_MK.contains("{{OROSMO_VERSION}}"));
        let out = render_version(TPL_CONFIG_MK, "mygame", "1.2.3");
        assert!(!out.contains("{{"), "placeholder left in config.mk:\n{out}");
        assert_eq!(project::read_orosmo_version(&out).as_deref(), Some("1.2.3"));
    }

    #[test]
    fn release_files_from_a_local_directory() {
        let dir = std::env::temp_dir().join(format!("orosmo_base_test_{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("a.txt"), b"x").unwrap();
        assert_eq!(fetch_from_base(dir.to_str().unwrap(), "a.txt").unwrap(), b"x");
        assert_eq!(fetch_from_base(&format!("file://{}", dir.display()), "a.txt").unwrap(), b"x");
        assert!(fetch_from_base(dir.to_str().unwrap(), "missing").is_err());
        let _ = fs::remove_dir_all(&dir);
    }
}
