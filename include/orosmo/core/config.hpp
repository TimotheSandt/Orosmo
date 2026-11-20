// CORE-06 : configuration. Trois couches, la plus haute gagne : valeurs par défaut déclarées < fichier JSON < ligne de commande.
// Les clés sont pointées (`terrain.octaves`) et correspondent à des objets JSON imbriqués. Les valeurs par défaut
// sont déclarées avec une description : describe() produit l'aide de la ligne de commande.
//
// Ligne de commande : `--terrain.octaves 6`, `--terrain.octaves=6`, `--debug` (booléen vrai), `--no-debug` (faux).
// Les arguments qui ne commencent pas par `--` sont rendus tels quels (fichiers, etc.).
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/serialization.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::core
{

class Config
{
  public:
    // Déclare un réglage : valeur par défaut + description. Le type JSON de la valeur par défaut fixe le type attendu.
    void declare(std::string key, Json defaultValue, std::string description = "");

    // Valeur effective (CLI, sinon fichier, sinon défaut) ; kNotFound si inconnue partout.
    Result<Json> get(std::string_view key) const;
    template <typename T> Result<T> get(std::string_view key) const
    {
        auto j = get(key);
        if (!j)
            return std::unexpected(j.error());
        T value{};
        if (auto r = fromJsonValue(value, *j, std::string(key)); !r)
            return std::unexpected(r.error());
        return value;
    }
    template <typename T> T getOr(std::string_view key, T fallback) const
    {
        auto v = get<T>(key);
        return v ? *v : fallback;
    }

    // Écrit dans la couche « fichier » (utilisé par saveFile). Une valeur de CLI reste prioritaire.
    void set(std::string_view key, Json value);

    // Lit un fichier JSON dans la couche « fichier » (remplace son contenu). Renvoie les clés dont la valeur effective
    // a changé (rechargement à chaud). Fichier absent ou invalide : erreur, l'ancien contenu est conservé.
    Result<std::vector<std::string>> loadFile(const std::filesystem::path &path);
    Result<> saveFile(const std::filesystem::path &path) const;

    // Analyse argv (sans argv[0]). Renvoie les arguments positionnels.
    Result<std::vector<std::string>> parseArgs(int argc, const char *const *argv);
    Result<std::vector<std::string>> parseArgs(const std::vector<std::string> &args);

    // Toutes les clés déclarées avec valeur effective et description, une ligne par clé, triées.
    std::string describe() const;

    // Applique la section `prefix` sur une structure réfléchie (UI-12, états) ; champs absents : valeurs de l'objet gardées.
    template <Reflected T> Result<> applyTo(T &object, std::string_view prefix = "") const
    {
        Json merged = effective();
        const Json *node = &merged;
        if (!prefix.empty())
        {
            node = find(merged, prefix);
            if (!node)
                return {};
        }
        return fromJsonValue(object, *node, std::string(prefix));
    }

    // Vue fusionnée des trois couches.
    Json effective() const;

  private:
    struct Declared
    {
        std::string key;
        Json defaultValue;
        std::string description;
    };
    static const Json *find(const Json &root, std::string_view dottedKey);
    static void assign(Json &root, std::string_view dottedKey, Json value);
    static void merge(Json &into, const Json &from);
    static void collectLeaves(const Json &j, const std::string &prefix, std::vector<std::pair<std::string, Json>> &out);

    std::vector<Declared> mDeclared;
    Json mDefaults = Json::object();
    Json mFile = Json::object();
    Json mCli = Json::object();
};

} // namespace orosmo::core
