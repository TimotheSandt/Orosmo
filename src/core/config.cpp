#include <orosmo/core/config.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace orosmo::core
{

const Json *Config::find(const Json &root, std::string_view dottedKey)
{
    const Json *node = &root;
    std::size_t start = 0;
    while (start <= dottedKey.size())
    {
        const std::size_t dot = dottedKey.find('.', start);
        const std::string part(dottedKey.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        if (!node->is_object())
            return nullptr;
        auto it = node->find(part);
        if (it == node->end())
            return nullptr;
        node = &*it;
        if (dot == std::string_view::npos)
            return node;
        start = dot + 1;
    }
    return node;
}

void Config::assign(Json &root, std::string_view dottedKey, Json value)
{
    Json *node = &root;
    std::size_t start = 0;
    for (;;)
    {
        const std::size_t dot = dottedKey.find('.', start);
        const std::string part(dottedKey.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        if (dot == std::string_view::npos)
        {
            (*node)[part] = std::move(value);
            return;
        }
        Json &child = (*node)[part];
        if (!child.is_object())
            child = Json::object();
        node = &child;
        start = dot + 1;
    }
}

void Config::merge(Json &into, const Json &from)
{
    if (!from.is_object())
    {
        into = from;
        return;
    }
    if (!into.is_object())
        into = Json::object();
    for (auto it = from.begin(); it != from.end(); ++it)
        merge(into[it.key()], it.value());
}

void Config::collectLeaves(const Json &j, const std::string &prefix, std::vector<std::pair<std::string, Json>> &out)
{
    if (j.is_object() && !j.empty())
    {
        for (auto it = j.begin(); it != j.end(); ++it)
            collectLeaves(it.value(), prefix.empty() ? it.key() : prefix + "." + it.key(), out);
    }
    else if (!prefix.empty())
        out.emplace_back(prefix, j);
}

Json Config::effective() const
{
    Json out = mDefaults;
    merge(out, mFile);
    merge(out, mCli);
    return out;
}

void Config::declare(std::string key, Json defaultValue, std::string description)
{
    assign(mDefaults, key, defaultValue);
    std::erase_if(mDeclared, [&](const Declared &d) { return d.key == key; });
    mDeclared.push_back({std::move(key), std::move(defaultValue), std::move(description)});
}

Result<Json> Config::get(std::string_view key) const
{
    // Même vue fusionnée que applyTo() : un objet demandé contient aussi les clés que seule une couche plus basse définit.
    const Json merged = effective();
    if (const Json *v = find(merged, key))
        return *v;
    return makeError(ErrorCode::kNotFound, "unknown setting: " + std::string(key));
}

void Config::set(std::string_view key, Json value) { assign(mFile, key, std::move(value)); }

Result<std::vector<std::string>> Config::loadFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return makeError(ErrorCode::kIo, "cannot open config file: " + path.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto parsed = parseJson(buffer.str());
    if (!parsed)
        return makeError(ErrorCode::kParse, "invalid JSON in " + path.string());
    if (!parsed->is_object())
        return makeError(ErrorCode::kParse, "config file must contain a JSON object: " + path.string());

    std::vector<std::pair<std::string, Json>> before, after;
    collectLeaves(effective(), "", before);
    mFile = std::move(*parsed);
    collectLeaves(effective(), "", after);

    std::map<std::string, Json> old(before.begin(), before.end());
    std::vector<std::string> changed;
    for (const auto &[key, value] : after)
    {
        auto it = old.find(key);
        if (it == old.end() || it->second != value)
            changed.push_back(key);
        if (it != old.end())
            old.erase(it);
    }
    for (const auto &[key, value] : old) // clés disparues
        changed.push_back(key);
    std::sort(changed.begin(), changed.end());
    return changed;
}

Result<> Config::saveFile(const std::filesystem::path &path) const
{
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return makeError(ErrorCode::kIo, "cannot write config file: " + path.string());
    out << dumpJson(mFile) << '\n';
    out.close(); // le vidage du tampon peut échouer (disque plein) : seule la fermeture le révèle pour un petit fichier
    return out ? Result<>{} : makeError(ErrorCode::kIo, "write failed: " + path.string());
}

Result<std::vector<std::string>> Config::parseArgs(const std::vector<std::string> &args)
{
    std::vector<std::string> positional;
    Json cli = Json::object();
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string &a = args[i];
        if (a.rfind("--", 0) != 0 || a.size() == 2)
        {
            positional.push_back(a);
            continue;
        }
        std::string key = a.substr(2);
        std::string value;
        bool hasValue = false;
        if (const auto eq = key.find('='); eq != std::string::npos)
        {
            value = key.substr(eq + 1);
            key = key.substr(0, eq);
            hasValue = true;
        }
        const bool negated = !hasValue && key.rfind("no-", 0) == 0 && find(mDefaults, key.substr(3)) != nullptr;
        if (negated)
        {
            const Json *target = find(mDefaults, key.substr(3));
            if (!target->is_boolean())
                return makeError(ErrorCode::kInvalidArgument, "--" + key + ": only boolean settings can be negated");
            assign(cli, key.substr(3), false);
            continue;
        }
        const Json *declared = find(mDefaults, key);
        const bool isBool = declared && declared->is_boolean();
        if (!hasValue && !isBool)
        {
            if (i + 1 >= args.size())
                return makeError(ErrorCode::kInvalidArgument, "missing value for --" + key);
            value = args[++i];
            hasValue = true;
        }
        if (!hasValue) // drapeau booléen seul
        {
            assign(cli, key, true);
            continue;
        }
        // Le type vient de la valeur par défaut ; sans déclaration : JSON si possible, sinon texte.
        Json parsedValue;
        if (declared && declared->is_string())
            parsedValue = value;
        else
        {
            parsedValue = Json::parse(value, nullptr, false);
            if (parsedValue.is_discarded())
                parsedValue = value;
        }
        // Un entier est accepté pour un réglage flottant, pas l'inverse (--tasks=2.5 pour un entier : erreur ici).
        const bool numberOk = declared && declared->is_number() && parsedValue.is_number() &&
                              !(declared->is_number_integer() && !parsedValue.is_number_integer());
        if (declared && declared->type() != parsedValue.type() && !numberOk)
            return makeError(ErrorCode::kParse, "--" + key + ": expected " + std::string(declared->type_name()) + ", got '" + value + "'");
        assign(cli, key, std::move(parsedValue));
    }
    merge(mCli, cli);
    return positional;
}

Result<std::vector<std::string>> Config::parseArgs(int argc, const char *const *argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return parseArgs(args);
}

std::string Config::describe() const
{
    std::vector<Declared> sorted = mDeclared;
    std::sort(sorted.begin(), sorted.end(), [](const Declared &a, const Declared &b) { return a.key < b.key; });
    std::string out;
    for (const auto &d : sorted)
    {
        out += "  --" + d.key + " (default " + d.defaultValue.dump() + ")";
        if (const auto cur = get(d.key); cur && *cur != d.defaultValue)
            out += " = " + cur->dump();
        if (!d.description.empty())
            out += "  " + d.description;
        out += '\n';
    }
    return out;
}

} // namespace orosmo::core
