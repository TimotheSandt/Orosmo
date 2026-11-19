mod ast;
mod engine;
mod generator;
mod json;
mod lexer;
mod parser;
mod registry;
mod scanner;
#[cfg(test)]
mod tests;
mod validator;

use crate::generator::Generator;
use crate::json::json_escape;
use crate::lexer::{Lexer, TokenType};
use crate::registry::{Registry, registry_from_json, registry_to_json};
use crate::scanner::Scanner;
use crate::validator::validate;
use std::collections::BTreeMap;
use std::env;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};

const HASH_TAG: &str = "// @cui-hash:";
// Bump this whenever generator output changes without a corresponding .cui syntax change.
const OUTPUT_FORMAT_VERSION: &str = "6";

fn main() {
    if let Err(err) = run_cli() {
        eprintln!("{err}");
        std::process::exit(1);
    }
}

fn run_cli() -> Result<(), String> {
    let mut args = env::args().skip(1).collect::<Vec<_>>();
    if args.is_empty() {
        print_usage();
        return Err("missing command".to_string());
    }
    let command = args.remove(0);
    match command.as_str() {
        "scan" => cmd_scan(&args),
        "build" => cmd_build(&args),
        "run" => cmd_run(&args),
        "redirect" => cmd_redirect(&args),
        "check" => cmd_check(&args),
        "spec" => {
            println!("{}", engine::spec_json());
            Ok(())
        }
        "-h" | "--help" | "help" => {
            print_usage();
            Ok(())
        }
        _ => {
            print_usage();
            Err(format!("unknown command: {command}"))
        }
    }
}

fn cmd_scan(args: &[String]) -> Result<(), String> {
    let parsed = parse_args(args);
    let headers = required_opt(&parsed.options, "--headers")?;
    let output = required_opt(&parsed.options, "--output")?;
    let headers_dir = PathBuf::from(headers);
    if !headers_dir.is_dir() {
        return Err(format!(
            "Error: headers dir not found: {}",
            headers_dir.display()
        ));
    }
    let registry = Scanner::new().scan_directory(&headers_dir)?;
    let out_path = PathBuf::from(output);
    if let Some(parent) = out_path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("failed to create {}: {err}", parent.display()))?;
    }
    fs::write(&out_path, registry_to_json(&registry))
        .map_err(|err| format!("failed to write {}: {err}", out_path.display()))?;
    println!(
        "[cui scan] {} component(s), {} function(s), {} enum(s) -> {}",
        registry.components.len(),
        registry.functions.len(),
        registry.enums.len(),
        out_path.display()
    );
    Ok(())
}

fn cmd_build(args: &[String]) -> Result<(), String> {
    let parsed = parse_args(args);
    let registry_path = required_opt(&parsed.options, "--registry")?;
    let output = required_opt(&parsed.options, "--output")?;
    let registry = read_registry(Path::new(registry_path))?;
    let out_dir = PathBuf::from(output);
    fs::create_dir_all(&out_dir)
        .map_err(|err| format!("failed to create {}: {err}", out_dir.display()))?;
    if parsed.positionals.is_empty() {
        return Err("build requires at least one .cui file".to_string());
    }
    for file in &parsed.positionals {
        let cui_path = PathBuf::from(file);
        if !cui_path.exists() {
            eprintln!("Error: file not found: {}", cui_path.display());
            continue;
        }
        compile_file(&cui_path, &out_dir, Some(&registry))?;
    }
    Ok(())
}

fn cmd_run(args: &[String]) -> Result<(), String> {
    let parsed = parse_args(args);
    let output = required_opt(&parsed.options, "--output")?;
    // Registre des composants hérités (UI::View) : seulement si --headers est donné ; la cible engine/ui n'en a pas besoin.
    let registry = match parsed.options.get("--headers").filter(|h| !h.is_empty()) {
        Some(headers) => {
            let headers_dir = PathBuf::from(headers);
            if !headers_dir.is_dir() {
                return Err(format!("Error: headers dir not found: {}", headers_dir.display()));
            }
            Some(Scanner::new().scan_directory(&headers_dir)?)
        }
        None => None,
    };
    let out_dir = PathBuf::from(output);
    fs::create_dir_all(&out_dir)
        .map_err(|err| format!("failed to create {}: {err}", out_dir.display()))?;
    if parsed.positionals.is_empty() {
        return Err("run requires at least one .cui file".to_string());
    }
    for file in &parsed.positionals {
        let cui_path = PathBuf::from(file);
        if !cui_path.exists() {
            eprintln!("Error: file not found: {}", cui_path.display());
            continue;
        }
        compile_file(&cui_path, &out_dir, registry.as_ref())?;
    }
    Ok(())
}

fn cmd_redirect(args: &[String]) -> Result<(), String> {
    let parsed = parse_args(args);
    let output = required_opt(&parsed.options, "--output")?;
    let out_dir = PathBuf::from(output);
    fs::create_dir_all(&out_dir)
        .map_err(|err| format!("failed to create {}: {err}", out_dir.display()))?;
    if parsed.positionals.is_empty() {
        return Err("redirect requires at least one .cui file".to_string());
    }
    for file in &parsed.positionals {
        let cui_path = PathBuf::from(file);
        write_redirect(&out_dir, &cui_path)?;
        println!(
            "[cui] redirect: {}",
            out_dir.join(file_name(&cui_path)).display()
        );
    }
    Ok(())
}

fn cmd_check(args: &[String]) -> Result<(), String> {
    let parsed = parse_args(args);
    let read_stdin = parsed.flags.iter().any(|flag| flag == "--stdin");
    let file = parsed
        .positionals
        .first()
        .cloned()
        .unwrap_or_else(|| "<stdin>".to_string());
    let source = if read_stdin {
        let mut source = String::new();
        io::stdin()
            .read_to_string(&mut source)
            .map_err(|err| format!("failed to read stdin: {err}"))?;
        source
    } else {
        let path = Path::new(&file);
        if !path.exists() {
            println!(
                "[{}]",
                diagnostic_json(&file, 1, 1, 1, 1, &format!("File not found: {}", path.display()), "error")
            );
            return Ok(());
        }
        fs::read_to_string(path)
            .map_err(|err| format!("failed to read {}: {err}", path.display()))?
    };

    let mut diagnostics = Vec::new();
    let tokens = match Lexer::new(&source).tokenize() {
        Ok(tokens) => tokens,
        Err(err) => {
            println!("[{}]", diagnostic_json(&file, err.line, err.col, err.line, err.col + 1, &err.message, "error"));
            return Ok(());
        }
    };
    let ast = match parser::parse(tokens) {
        Ok(ast) => ast,
        Err(err) => {
            let end_col = err.col + token_len(&err.token).max(1);
            println!("[{}]", diagnostic_json(&file, err.line.max(1), err.col.max(1), err.line.max(1), end_col, &err.message, "error"));
            return Ok(());
        }
    };
    if engine::is_engine_target(&ast) {
        if let Err(errors) = engine::compile(&ast, &file, "check") {
            for e in errors {
                diagnostics.push(diagnostic_json(&file, e.line.max(1), e.col.max(1), e.line.max(1), e.col.max(1) + 1, &e.message, "error"));
            }
        }
    } else if let Some(registry_path) = parsed.options.get("--registry") {
        let path = Path::new(registry_path);
        if path.exists() {
            let registry = read_registry(path)?;
            for diagnostic in validate(&ast, &registry) {
                let line = diagnostic.line.max(1);
                let col = diagnostic.col.max(1);
                diagnostics.push(diagnostic_json(&file, line, col, line, col + 1, &diagnostic.message,
                                                 if diagnostic.is_warning { "warning" } else { "error" }));
            }
        }
    }
    println!("[{}]", diagnostics.join(","));
    Ok(())
}

// Diagnostic au format des compilateurs (cliquable dans un éditeur) : `fichier:ligne:colonne: error: message`.
fn diagnostic_line(file: &str, line: usize, col: usize, severity: &str, message: &str) -> String {
    format!("{file}:{}:{}: {severity}: {message}", line.max(1), col.max(1))
}

// Compile un source `.cui` en mémoire : (en-tête, implémentation) ou la liste des diagnostics `fichier:ligne:colonne`.
pub fn compile_source(source: &str, display: &str, stem: &str, registry: Option<&Registry>) -> Result<(String, String, Vec<String>), Vec<String>> {
    let tokens = Lexer::new(source)
        .tokenize()
        .map_err(|err| vec![diagnostic_line(display, err.line, err.col, "error", &err.message)])?;
    let ast = parser::parse(tokens).map_err(|err| vec![diagnostic_line(display, err.line, err.col, "error", &err.message)])?;
    if engine::is_engine_target(&ast) {
        return engine::compile(&ast, display, stem)
            .map(|out| (out.header, out.source, Vec::new()))
            .map_err(|errors| {
                errors
                    .iter()
                    .map(|e| diagnostic_line(display, e.line, e.col, "error", &e.message))
                    .collect()
            });
    }
    let Some(registry) = registry else {
        return Err(vec![diagnostic_line(display, 1, 1, "error", "the UI::View target needs the component registry (--headers)")]);
    };
    if let Some((line, col)) = first_binding(&ast) {
        return Err(vec![diagnostic_line(display, line, col, "error", &format!("two-way bindings ($...) need `import \"{}\"`", engine::IMPORT))]);
    }
    let diagnostics = validate(&ast, registry);
    let mut warnings = Vec::new();
    let mut errors = Vec::new();
    for d in &diagnostics {
        let line = diagnostic_line(display, d.line, d.col, if d.is_warning { "warning" } else { "error" }, &d.message);
        if d.is_warning {
            warnings.push(line);
        } else {
            errors.push(line);
        }
    }
    if !errors.is_empty() {
        errors.extend(warnings);
        return Err(errors);
    }
    let (header, implementation) = Generator::new(registry, format!("{stem}.cui"))
        .generate(&ast)
        .map_err(|err| vec![diagnostic_line(display, 1, 1, "error", &err)])?;
    Ok((header, implementation, warnings))
}

fn first_binding(ast: &crate::ast::FileAst) -> Option<(usize, usize)> {
    fn has(expr: &crate::ast::Expr) -> bool {
        use crate::ast::{Expr, StringPart};
        match expr {
            Expr::Binding(_) => true,
            Expr::Call { args, named_args, .. } => args.iter().any(has) || named_args.values().any(has),
            Expr::Binary { left, right, .. } => has(left) || has(right),
            Expr::Capture { expr, .. } => has(expr),
            Expr::StringLit(parts) => parts.iter().any(|p| matches!(p, StringPart::Interp { expr, .. } if has(expr))),
            _ => false,
        }
    }
    fn walk(e: &crate::ast::Element) -> Option<(usize, usize)> {
        if e.args.iter().any(has) || e.named_args.values().any(has) {
            return Some((e.line, e.col));
        }
        for m in &e.modifiers {
            if m.args.iter().any(has) || m.named_args.values().any(has) {
                return Some((m.line, m.col));
            }
        }
        e.children.iter().find_map(walk)
    }
    ast.views.iter().find_map(|v| walk(&v.root))
}

fn compile_file(cui_path: &Path, out_dir: &Path, registry: Option<&Registry>) -> Result<(), String> {
    let source = fs::read_to_string(cui_path)
        .map_err(|err| format!("failed to read {}: {err}", cui_path.display()))?;
    let stem = cui_path
        .file_stem()
        .and_then(|stem| stem.to_str())
        .ok_or_else(|| format!("invalid .cui filename: {}", cui_path.display()))?;
    let header_path = out_dir.join(format!("{stem}.gen.h"));
    let cpp_path = out_dir.join(format!("{stem}.gen.cpp"));

    let current_hash = source_hash(&source);
    if read_stored_hash(&header_path).as_deref() == Some(current_hash.as_str()) && cpp_path.exists() {
        println!("[cui] {} unchanged, skipping", file_name(cui_path));
        return Ok(());
    }

    let display = cui_path.display().to_string();
    let (header, implementation, warnings) = match compile_source(&source, &display, stem, registry) {
        Ok(out) => out,
        Err(diagnostics) => {
            for d in &diagnostics {
                eprintln!("{d}");
            }
            // Rien n'est écrit pour un source invalide (ni en-tête, ni redirection) : l'ancien code généré disparaît aussi, pour
            // que le build échoue au lieu de compiler une version périmée.
            let _ = fs::remove_file(&header_path);
            let _ = fs::remove_file(&cpp_path);
            return Err(format!("[cui] {} error(s) in {}", diagnostics.iter().filter(|d| d.contains(": error: ")).count(), display));
        }
    };
    for w in &warnings {
        eprintln!("{w}");
    }
    write_redirect(out_dir, cui_path)?;
    fs::write(&header_path, format!("{HASH_TAG} {current_hash}\n{header}"))
        .map_err(|err| format!("failed to write {}: {err}", header_path.display()))?;
    fs::write(&cpp_path, implementation)
        .map_err(|err| format!("failed to write {}: {err}", cpp_path.display()))?;
    println!(
        "[cui] {} -> {}, {}",
        file_name(cui_path),
        file_name(&header_path),
        file_name(&cpp_path)
    );
    Ok(())
}

fn write_redirect(out_dir: &Path, cui_path: &Path) -> Result<(), String> {
    fs::create_dir_all(out_dir)
        .map_err(|err| format!("failed to create {}: {err}", out_dir.display()))?;
    let cui_name = file_name(cui_path);
    let stem = cui_path
        .file_stem()
        .and_then(|stem| stem.to_str())
        .ok_or_else(|| format!("invalid .cui filename: {}", cui_path.display()))?;
    let redirect_path = out_dir.join(&cui_name);
    fs::write(
        &redirect_path,
        format!(
            "// AUTO-GENERATED - do not edit. Include redirect for {cui_name}\n#pragma once\n#include \"{stem}.gen.h\"\n"
        ),
    )
    .map_err(|err| format!("failed to write {}: {err}", redirect_path.display()))?;
    Ok(())
}

fn read_registry(path: &Path) -> Result<Registry, String> {
    if !path.exists() {
        return Err(format!("Error: registry not found: {}", path.display()));
    }
    let text = fs::read_to_string(path)
        .map_err(|err| format!("failed to read {}: {err}", path.display()))?;
    registry_from_json(&text)
}

fn source_hash(source: &str) -> String {
    let mut hash = 0xcbf29ce484222325u64;
    for byte in OUTPUT_FORMAT_VERSION
        .as_bytes()
        .iter()
        .chain(std::iter::once(&b'\n'))
        .chain(source.as_bytes().iter())
    {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3);
    }
    format!("{hash:016x}")
}

fn read_stored_hash(header_path: &Path) -> Option<String> {
    let text = fs::read_to_string(header_path).ok()?;
    let first = text.lines().next()?;
    first
        .strip_prefix(HASH_TAG)
        .map(|hash| hash.trim().to_string())
}

#[derive(Default)]
struct ParsedArgs {
    options: BTreeMap<String, String>,
    flags: Vec<String>,
    positionals: Vec<String>,
}

fn parse_args(args: &[String]) -> ParsedArgs {
    let mut parsed = ParsedArgs::default();
    let mut index = 0usize;
    while index < args.len() {
        let arg = &args[index];
        match arg.as_str() {
            "--headers" | "--output" | "--registry" => {
                if let Some(value) = args.get(index + 1) {
                    parsed.options.insert(arg.clone(), value.clone());
                    index += 2;
                } else {
                    parsed.options.insert(arg.clone(), String::new());
                    index += 1;
                }
            }
            "--stdin" => {
                parsed.flags.push(arg.clone());
                index += 1;
            }
            _ => {
                parsed.positionals.push(arg.clone());
                index += 1;
            }
        }
    }
    parsed
}

fn required_opt<'a>(options: &'a BTreeMap<String, String>, name: &str) -> Result<&'a str, String> {
    options
        .get(name)
        .filter(|value| !value.is_empty())
        .map(String::as_str)
        .ok_or_else(|| format!("{name} is required"))
}

fn diagnostic_json(
    file: &str,
    line: usize,
    col: usize,
    end_line: usize,
    end_col: usize,
    message: &str,
    severity: &str,
) -> String {
    format!(
        "{{\"file\":\"{}\",\"line\":{},\"col\":{},\"endLine\":{},\"endCol\":{},\"message\":\"{}\",\"severity\":\"{}\"}}",
        json_escape(file),
        line,
        col,
        end_line,
        end_col,
        json_escape(message),
        severity
    )
}

fn token_len(token: &crate::lexer::Token) -> usize {
    match token.ty {
        TokenType::Eof => 1,
        _ => token.text().chars().count(),
    }
}

fn file_name(path: &Path) -> String {
    path.file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("")
        .to_string()
}

fn print_usage() {
    eprintln!(
        "Usage:\n  cui scan --headers <dir> --output <registry.json>\n  cui build --registry <registry.json> --output <dir> <file.cui>...\n  cui run [--headers <dir>] --output <dir> <file.cui>...   (--headers: UI::View target only; `import \"engine/ui\"` needs none)\n  cui redirect --output <dir> <file.cui>...\n  cui check [--registry <registry.json>] [--stdin] <file.cui>\n  cui spec        components, modifiers and theme properties of the engine/ui target (JSON, for editors)"
    );
}
