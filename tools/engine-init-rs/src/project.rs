//! `config.mk` du projet : la version du moteur (`OROSMO_VERSION := x.y.z`) et quelques valeurs lues par l'outil.

/// Position de l'opérateur d'affectation make (`:=`, `?=`, `=`) si la ligne affecte `var`.
fn assignment<'a>(line: &'a str, var: &str) -> Option<(&'a str, &'a str)> {
    let t = line.trim_start();
    let rest = t.strip_prefix(var)?;
    let rest = rest.trim_start();
    for op in [":=", "?=", "="] {
        if let Some(value) = rest.strip_prefix(op) {
            return Some((op, value));
        }
    }
    None
}

/// Valeur de `var` telle que GNU Make la voit : affectations dans l'ordre, la dernière gagne, sauf `?=` qui ne s'applique que si
/// la variable n'est pas encore définie (même vide). Commentaire en fin de ligne retiré. None si absente ou vide.
pub fn read_var(content: &str, var: &str) -> Option<String> {
    let mut value: Option<String> = None;
    for (op, v) in content.lines().filter(|l| !l.trim_start().starts_with('#')).filter_map(|l| assignment(l, var)) {
        if op == "?=" && value.is_some() {
            continue;
        }
        value = Some(v.split('#').next().unwrap_or("").trim().to_string());
    }
    value.filter(|v| !v.is_empty())
}

/// Version du moteur nommée par le projet ; une valeur de gabarit non remplie (`{{…}}`, `$(…)`) ne compte pas.
pub fn read_orosmo_version(content: &str) -> Option<String> {
    read_var(content, "OROSMO_VERSION").filter(|v| !v.contains("{{") && !v.contains("$("))
}

/// Remplace la valeur de `OROSMO_VERSION` (toutes ses affectations, opérateur conservé), ou ajoute la ligne à la fin.
pub fn set_orosmo_version(content: &str, version: &str) -> String {
    let mut found = false;
    let mut out: Vec<String> = content
        .lines()
        .map(|line| {
            if line.trim_start().starts_with('#') {
                return line.to_string();
            }
            match assignment(line, "OROSMO_VERSION") {
                Some((op, _)) => {
                    found = true;
                    let indent = &line[..line.len() - line.trim_start().len()];
                    format!("{indent}OROSMO_VERSION {op} {version}")
                }
                None => line.to_string(),
            }
        })
        .collect();
    if !found {
        if out.last().is_some_and(|l| !l.trim().is_empty()) {
            out.push(String::new());
        }
        out.push("# Engine version used by this project (resolved by `orosmo path`; changed by `orosmo update`).".to_string());
        out.push(format!("OROSMO_VERSION := {version}"));
    }
    let mut s = out.join("\n");
    s.push('\n');
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rewrites_orosmo_version() {
        let cfg = "# Project\nPROJECT_NAME ?= game # name\nOROSMO_VERSION := 0.6.0\nVERSION ?= 0.1.0\n";
        assert_eq!(read_orosmo_version(cfg).as_deref(), Some("0.6.0"));
        assert_eq!(read_var(cfg, "PROJECT_NAME").as_deref(), Some("game"));
        assert_eq!(read_var(cfg, "VERSION").as_deref(), Some("0.1.0"));
        let new = set_orosmo_version(cfg, "0.6.2");
        assert_eq!(new, "# Project\nPROJECT_NAME ?= game # name\nOROSMO_VERSION := 0.6.2\nVERSION ?= 0.1.0\n");

        // Opérateur et indentation conservés ; un commentaire qui cite la variable n'est pas touché.
        let cfg = "# OROSMO_VERSION := 1.0.0\n  OROSMO_VERSION?=0.5.0 # pinned\n";
        assert_eq!(set_orosmo_version(cfg, "0.6.2"), "# OROSMO_VERSION := 1.0.0\n  OROSMO_VERSION ?= 0.6.2\n");

        // Absente (ancien projet) : ajoutée à la fin.
        let cfg = "PROJECT_NAME ?= game";
        let new = set_orosmo_version(cfg, "0.6.2");
        assert!(new.starts_with("PROJECT_NAME ?= game\n\n# Engine version"));
        assert!(new.ends_with("OROSMO_VERSION := 0.6.2\n"));
        assert_eq!(read_orosmo_version(&new).as_deref(), Some("0.6.2"));

        // `?=` après une affectation (même vide) ne change rien ; seul, il donne la valeur.
        assert_eq!(read_orosmo_version("OROSMO_VERSION := 0.6.2\nOROSMO_VERSION ?= 0.7.0\n").as_deref(), Some("0.6.2"));
        assert_eq!(read_orosmo_version("OROSMO_VERSION :=\nOROSMO_VERSION ?= 0.7.0\n"), None);
        assert_eq!(read_orosmo_version("OROSMO_VERSION ?= 0.7.0\nOROSMO_VERSION := 0.8.0\n").as_deref(), Some("0.8.0"));

        // OROSMO_VERSION_EXTRA n'est pas OROSMO_VERSION ; un gabarit non rempli n'est pas une version.
        assert_eq!(read_orosmo_version("OROSMO_VERSION_EXTRA := 1\n"), None);
        assert_eq!(read_orosmo_version("OROSMO_VERSION := {{OROSMO_VERSION}}\n"), None);
    }
}
