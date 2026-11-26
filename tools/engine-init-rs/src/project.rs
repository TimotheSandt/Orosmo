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
    eval_var(None, &[("", content)], var).map(|(v, _)| v).filter(|v| !v.is_empty())
}

/// Comme Make qui inclut `sources` (nom, contenu) dans l'ordre, avec un état partagé : `initial` (variable d'environnement, par
/// exemple) puis chaque affectation ; `?=` ne s'applique que si la variable n'est pas encore définie. Renvoie la valeur et le
/// nom de sa source ; `Some(("", …))` : définie vide (ce n'est pas une absence).
pub fn eval_var<'a>(initial: Option<(String, &'a str)>, sources: &[(&'a str, &str)], var: &str) -> Option<(String, &'a str)> {
    let mut value = initial;
    for (name, content) in sources {
        for (op, v) in content.lines().filter(|l| !l.trim_start().starts_with('#')).filter_map(|l| assignment(l, var)) {
            if op == "?=" && value.is_some() {
                continue;
            }
            value = Some((v.split('#').next().unwrap_or("").trim().to_string(), *name));
        }
    }
    value
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

    #[test]
    fn evaluation_is_shared_across_files() {
        let mk = |a: &'static str, b: &'static str| eval_var(None, &[("config.mk", a), ("config.local.mk", b)], "ENGINE_DIR");
        // `?=` local après un `:=` de config.mk : config.mk gagne.
        assert_eq!(mk("ENGINE_DIR := orosmo\n", "ENGINE_DIR ?= missing\n"), Some(("orosmo".to_string(), "config.mk")));
        // `:=` vide local : définie vide, la valeur de config.mk n'est pas reprise.
        assert_eq!(mk("ENGINE_DIR := missing\n", "ENGINE_DIR :=\n"), Some((String::new(), "config.local.mk")));
        assert_eq!(mk("", ""), None);
        // L'environnement est remplacé par `:=`, pas par `?=`.
        let env = Some(("/env".to_string(), "environment"));
        assert_eq!(eval_var(env.clone(), &[("config.mk", "ENGINE_DIR ?= x\n")], "ENGINE_DIR"), env);
        assert_eq!(eval_var(env, &[("config.mk", "ENGINE_DIR := x # c\n")], "ENGINE_DIR"), Some(("x".to_string(), "config.mk")));
    }
}
