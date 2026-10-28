#include "client/ui/JsonUi.hpp"
#include "client/ui/UiExpr.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace perlin::ui {
    namespace {
        bool ReadFile(const std::string& path, std::string& out) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        std::string SplitName(const std::string& key, std::string* ref = nullptr) {
            const size_t at = key.find('@');
            if (at == std::string::npos) {
                if (ref) ref->clear();
                return key;
            }
            if (ref) *ref = key.substr(at + 1);
            return key.substr(0, at);
        }

        std::string QuickNamespace(const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return {};
            char buffer[4096];
            in.read(buffer, sizeof(buffer));
            const std::string head(buffer, static_cast<size_t>(in.gcount()));
            size_t pos = 0;
            while ((pos = head.find("\"namespace\"", pos)) != std::string::npos) {
                size_t i = pos + 11;
                while (i < head.size() && (head[i] == ' ' || head[i] == '\t' || head[i] == ':'))
                    i++;
                if (i < head.size() && head[i] == '"') {
                    const size_t end = head.find('"', i + 1);
                    if (end != std::string::npos)
                        return head.substr(i + 1, end - i - 1);
                }
                pos += 11;
            }
            return {};
        }

        std::string ElementName(const Json& element) {
            if (element.IsObject() && !element.object.empty())
                return SplitName(element.object[0].first);
            return {};
        }

        void ApplyModification(Json& def, const Json& mod) {
            const Json* arrayName = mod.Find("array_name");
            const Json* operation = mod.Find("operation");
            if (!arrayName || !operation || !arrayName->IsString() || !operation->IsString())
                return;
            Json* array = def.Find(arrayName->string);
            if (!array || !array->IsArray()) {
                const std::string op = operation->string;
                if (op != "insert_back" && op != "insert_front")
                    return;
                def.Set(arrayName->string, Json::MakeArray());
                array = def.Find(arrayName->string);
            }
            const std::string& op = operation->string;
            const Json* value = mod.Find("value");
            std::string target;
            if (const Json* n = mod.Find("control_name")) target = n->StringOr("");
            else if (const Json* n2 = mod.Find("binding_name")) target = n2->StringOr("");
            else if (const Json* n3 = mod.Find("name")) target = n3->StringOr("");
            const auto matches = [&](const Json& element) {
                if (arrayName->string == "bindings") {
                    const Json* b = element.Find("binding_name");
                    return b && b->StringOr("") == target;
                }
                return ElementName(element) == target;
            };
            const auto find = [&]() -> long {
                for (size_t i = 0; i < array->array.size(); i++)
                    if (matches(array->array[i]))
                        return static_cast<long>(i);
                return -1;
            };
            std::vector<Json> values;
            if (value) {
                if (value->IsArray()) values = value->array;
                else values.push_back(*value);
            }
            if (op == "insert_back") {
                array->array.insert(array->array.end(), values.begin(), values.end());
            } else if (op == "insert_front") {
                array->array.insert(array->array.begin(), values.begin(), values.end());
            } else if (op == "insert_after" || op == "insert_before") {
                const long i = find();
                if (i < 0) return;
                const long at = op == "insert_after" ? i + 1 : i;
                array->array.insert(array->array.begin() + at, values.begin(), values.end());
            } else if (op == "remove") {
                const long i = find();
                if (i >= 0) array->array.erase(array->array.begin() + i);
            } else if (op == "replace") {
                const long i = find();
                if (i < 0) return;
                array->array.erase(array->array.begin() + i);
                array->array.insert(array->array.begin() + i, values.begin(), values.end());
            } else if (op == "move_back" || op == "move_front" || op == "move_after" || op == "move_before") {
                const long i = find();
                if (i < 0) return;
                Json moved = array->array[static_cast<size_t>(i)];
                array->array.erase(array->array.begin() + i);
                if (op == "move_back") {
                    array->array.push_back(std::move(moved));
                } else if (op == "move_front") {
                    array->array.insert(array->array.begin(), std::move(moved));
                } else {
                    std::string other;
                    if (const Json* t = mod.Find("target_control_name")) other = t->StringOr("");
                    long j = -1;
                    for (size_t k = 0; k < array->array.size(); k++)
                        if (ElementName(array->array[k]) == other)
                            j = static_cast<long>(k);
                    if (j < 0) {
                        array->array.insert(array->array.begin() + std::min<long>(i, static_cast<long>(array->array.size())),
                                            std::move(moved));
                    } else {
                        array->array.insert(array->array.begin() + (op == "move_after" ? j + 1 : j), std::move(moved));
                    }
                }
            }
        }
    }

    uint64_t UiLibrary::Stamp(const std::string& path) {
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (ec)
            return 0;
        const uint64_t size = static_cast<uint64_t>(fs::file_size(path, ec));
        return static_cast<uint64_t>(time.time_since_epoch().count()) * 1315423911ull ^ size;
    }

    void UiLibrary::SetRoots(std::vector<std::string> r) {
        roots = std::move(r);
    }

    bool UiLibrary::Scan() {
        files.clear();
        namespaces.clear();
        documents.clear();
        globals.clear();
        globalsLoaded = false;
        globalsPath.clear();
        problems.clear();
        stamps.clear();
        watched.clear();

        for (size_t r = roots.size(); r-- > 0;) {
            const fs::path root(roots[r]);
            std::error_code ec;
            if (!fs::is_directory(root, ec))
                continue;
            watched.push_back(roots[r]);
            std::vector<std::pair<std::string, std::string>> found;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                    continue;
                found.emplace_back(fs::relative(it->path(), root, ec).generic_string(), it->path().string());
            }
            std::sort(found.begin(), found.end());
            for (auto& entry : found) {
                File file;
                file.rel = entry.first;
                file.path = entry.second;
                file.root = r;
                file.stamp = Stamp(file.path);
                stamps.emplace_back(file.path, file.stamp);
                if (file.rel == "_global_variables.json") {
                    globalsPath = file.path;
                    continue;
                }
                if (file.rel == "_ui_defs.json")
                    continue;
                file.ns = QuickNamespace(file.path);
                if (file.ns.empty())
                    continue;
                files.push_back(std::move(file));
            }
        }
        for (size_t i = 0; i < files.size(); i++)
            namespaces[files[i].ns].files.push_back(i);
        return !files.empty();
    }

    bool UiLibrary::Changed() const {
        for (const auto& s : stamps)
            if (Stamp(s.first) != s.second)
                return true;
        size_t count = 0;
        for (const std::string& root : watched) {
            std::error_code ec;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".json")
                    count++;
        }
        return count != stamps.size();
    }

    const Vars& UiLibrary::GlobalVariables() {
        if (!globalsLoaded) {
            globalsLoaded = true;
            for (size_t r = roots.size(); r-- > 0;) {
                const std::string path = (fs::path(roots[r]) / "_global_variables.json").string();
                std::string text;
                if (!ReadFile(path, text))
                    continue;
                Json root;
                std::string error;
                if (!Json::Parse(text, root, error)) {
                    problems.push_back(path + ": " + error);
                    continue;
                }
                for (const auto& member : root.object)
                    if (!member.first.empty() && member.first[0] == '$') {
                        std::string name = member.first;
                        const size_t bar = name.find('|');
                        if (bar != std::string::npos)
                            name = name.substr(0, bar);
                        globals[name] = member.second;
                    }
            }
        }
        return globals;
    }

    void UiLibrary::LoadNamespace(const std::string& nsName, Namespace& ns) {
        ns.loaded = true;
        for (const size_t index : ns.files) {
            const File& file = files[index];
            std::string text;
            if (!ReadFile(file.path, text))
                continue;
            Json root;
            std::string error;
            if (!Json::Parse(text, root, error)) {
                problems.push_back(file.rel + ": " + error);
                if (!root.IsObject())
                    continue;
            }
            documents.push_back(std::move(root));
            const Json& doc = documents.back();
            for (const auto& member : doc.object) {
                if (member.first == "namespace" || !member.second.IsObject())
                    continue;
                const std::string name = SplitName(member.first);
                Def def;
                def.json = &member.second;
                def.key = member.first;
                def.ns = nsName;
                const Json* mods = member.second.Find("modifications");
                auto existing = ns.defs.find(name);
                if (mods && mods->IsArray() && existing != ns.defs.end()) {
                    Json patched = *existing->second.json;
                    for (const Json& mod : mods->array)
                        ApplyModification(patched, mod);
                    for (const auto& prop : member.second.object)
                        if (prop.first != "modifications" && prop.first != "controls")
                            patched.Set(prop.first, prop.second);
                    documents.push_back(std::move(patched));
                    def.json = &documents.back();
                    def.key = existing->second.key;
                }
                ns.defs[name] = def;
            }
        }
    }

    const UiLibrary::Def* UiLibrary::Find(const std::string& ns, const std::string& name) {
        auto it = namespaces.find(ns);
        if (it == namespaces.end())
            return nullptr;
        if (!it->second.loaded)
            LoadNamespace(ns, it->second);
        auto def = it->second.defs.find(name);
        return def == it->second.defs.end() ? nullptr : &def->second;
    }

    namespace {
        bool IsNameChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        std::string Literal(const Json& v, bool expression) {
            switch (v.type) {
            case Json::Type::Bool: return v.boolean ? "true" : "false";
            case Json::Type::Number: return v.Dump();
            case Json::Type::String: return expression ? "'" + v.string + "'" : v.string;
            case Json::Type::Null: return expression ? "null" : "";
            default: return v.Dump();
            }
        }

        Json SubstituteImpl(const Json& value, const Vars& vars, std::unordered_set<std::string>* unresolved, int depth) {
            if (depth > 16)
                return value;
            switch (value.type) {
            case Json::Type::String: {
                const std::string& s = value.string;
                if (s.find('$') == std::string::npos)
                    return value;
                if (s.size() > 1 && s[0] == '$' &&
                    std::all_of(s.begin() + 1, s.end(), [](char c) { return IsNameChar(c); })) {
                    auto it = vars.find(s);
                    if (it == vars.end()) {
                        if (unresolved) unresolved->insert(s);
                        return value;
                    }
                    return SubstituteImpl(it->second, vars, unresolved, depth + 1);
                }
                const bool expression = IsExpression(s);
                std::string out;
                for (size_t i = 0; i < s.size();) {
                    if (s[i] != '$') {
                        out += s[i++];
                        continue;
                    }
                    size_t j = i + 1;
                    while (j < s.size() && IsNameChar(s[j]))
                        j++;
                    const std::string name = s.substr(i, j - i);
                    auto it = vars.find(name);
                    if (name.size() > 1 && it != vars.end()) {
                        out += Literal(SubstituteImpl(it->second, vars, unresolved, depth + 1), expression);
                    } else {
                        if (unresolved && name.size() > 1) unresolved->insert(name);
                        out += name;
                    }
                    i = j;
                }
                return Json(out);
            }
            case Json::Type::Array: {
                Json out = Json::MakeArray();
                out.array.reserve(value.array.size());
                for (const Json& v : value.array)
                    out.array.push_back(SubstituteImpl(v, vars, unresolved, depth));
                return out;
            }
            case Json::Type::Object: {
                Json out = Json::MakeObject();
                for (const auto& member : value.object) {
                    if (!member.first.empty() && member.first[0] == '$') {
                        out.object.emplace_back(member.first, member.second);
                        continue;
                    }
                    out.object.emplace_back(member.first, SubstituteImpl(member.second, vars, unresolved, depth));
                }
                return out;
            }
            default:
                return value;
            }
        }
    }

    Json Substitute(const Json& value, const Vars& vars, std::unordered_set<std::string>* unresolved) {
        return SubstituteImpl(value, vars, unresolved, 0);
    }

    namespace {
        struct Layer {
            const Json* json;
            std::string ns;
        };

        struct ChildEntry {
            std::string key;
            Json value;
            std::string ns;
        };

        class Builder {
        public:
            Builder(UiLibrary& library, BuildReport& report) : library(library), report(report) {}

            std::unique_ptr<Node> Build(const std::string& key, const Json& overrides, const std::string& ns,
                                        const Vars& parentVars, const std::string& parentPath, int depth) {
                if (depth > 64)
                    return nullptr;
                std::string ref;
                std::string name = SplitName(key, &ref);
                if (!ref.empty() && ref.find('$') != std::string::npos) {
                    const Json resolved = Substitute(Json(ref), parentVars, &report.unresolvedVariables);
                    ref = resolved.StringOr("");
                }
                if (!ref.empty() && ref[0] == '@')
                    ref.erase(0, 1);

                std::vector<Layer> layers;
                if (!ref.empty())
                    ResolveChain(ref, ns, layers, 0);
                layers.push_back({&overrides, ns});

                Vars vars = parentVars;
                for (const Layer& layer : layers) {
                    if (!layer.json->IsObject())
                        continue;
                    for (const auto& member : layer.json->object) {
                        if (member.first.empty() || member.first[0] != '$')
                            continue;
                        AssignVariable(vars, member.first, member.second);
                    }
                    if (const Json* conditional = layer.json->Find("variables")) {
                        if (!conditional->IsArray())
                            continue;
                        for (const Json& entry : conditional->array) {
                            if (!entry.IsObject())
                                continue;
                            bool apply = true;
                            if (const Json* requiresJson = entry.Find("requires")) {
                                Json condition = Substitute(*requiresJson, vars, &report.unresolvedVariables);
                                if (condition.IsString() && IsExpression(condition.string)) {
                                    ExprEnv env;
                                    Json result;
                                    apply = EvalExpression(condition.string, env, result) && Truthy(result);
                                } else {
                                    apply = Truthy(condition);
                                }
                            }
                            if (!apply)
                                continue;
                            for (const auto& member : entry.object)
                                if (!member.first.empty() && member.first[0] == '$')
                                    AssignVariable(vars, member.first, member.second);
                        }
                    }
                }

                Json props = Json::MakeObject();
                std::vector<Json> bindingList;
                std::vector<ChildEntry> childList;
                for (const Layer& layer : layers) {
                    if (!layer.json->IsObject())
                        continue;
                    for (const auto& member : layer.json->object) {
                        const std::string& k = member.first;
                        if (k.empty() || k[0] == '$' || k == "variables" || k == "modifications" || k == "namespace")
                            continue;
                        if (k == "controls") {
                            MergeChildren(childList, member.second, layer.ns);
                        } else if (k == "bindings") {
                            if (member.second.IsArray())
                                for (const Json& b : member.second.array)
                                    bindingList.push_back(b);
                        } else {
                            props.Set(k, member.second);
                        }
                    }
                }

                auto node = std::make_unique<Node>();
                node->name = name;
                node->path = parentPath.empty() ? name : parentPath + "/" + name;
                node->ns = ns;
                report.nodes++;

                Json substituted = Substitute(props, vars, &report.unresolvedVariables);
                if (const Json* ignored = substituted.Find("ignored")) {
                    Json value = *ignored;
                    if (value.IsString() && IsExpression(value.string) && !UsesBindings(value.string)) {
                        ExprEnv env;
                        Json result;
                        if (EvalExpression(value.string, env, result))
                            value = result;
                    }
                    if (value.IsBool() || value.IsNumber() || (value.IsString() && !IsExpression(value.string) && !value.string.empty() && value.string[0] != '#')) {
                        if (Truthy(value))
                            return nullptr;
                        substituted.Erase("ignored");
                    }
                }
                node->props = std::move(substituted);
                if (const Json* type = node->props.Find("type"))
                    node->type = type->StringOr("panel");
                else
                    node->type = "panel";

                for (const Json& b : bindingList)
                    node->bindings.push_back(Substitute(b, vars, &report.unresolvedVariables));

                for (const ChildEntry& entry : childList) {
                    auto child = Build(entry.key, entry.value, entry.ns, vars, node->path, depth + 1);
                    if (child)
                        node->children.push_back(std::move(child));
                }

                if (node->type == "grid") {
                    if (const Json* tmpl = node->props.Find("grid_item_template"); tmpl && tmpl->IsString() && !tmpl->string.empty()) {
                        std::string base = tmpl->string;
                        if (base[0] == '@') base.erase(0, 1);
                        const size_t dot = base.find('.');
                        const std::string simple = dot == std::string::npos ? base : base.substr(dot + 1);
                        node->gridTemplate = Build(simple + "@" + base, Json::MakeObject(), ns, vars, node->path, depth + 1);
                    }
                }
                if (node->type == "factory") {
                    if (const Json* ids = node->props.Find("control_ids"); ids && ids->IsObject()) {
                        for (const auto& member : ids->object) {
                            if (!member.second.IsString())
                                continue;
                            std::string r;
                            std::string n = SplitName(member.second.string, &r);
                            if (r.empty() && !n.empty()) {
                                r = n;
                                const size_t dotAt = n.find('.');
                                if (dotAt != std::string::npos)
                                    n = n.substr(dotAt + 1);
                            }
                            if (n.empty())
                                n = member.first;
                            auto template2 = Build(n + "@" + r, Json::MakeObject(), ns, vars, node->path, depth + 1);
                            if (template2)
                                node->factoryControls.emplace_back(member.first, std::move(template2));
                        }
                    }
                    if (const Json* name2 = node->props.Find("control_name"); name2 && name2->IsString()) {
                        std::string refName = name2->string;
                        if (!refName.empty() && refName[0] == '@') refName.erase(0, 1);
                        const size_t dot = refName.find('.');
                        const std::string simple = dot == std::string::npos ? refName : refName.substr(dot + 1);
                        auto template2 = Build(simple + "@" + refName, Json::MakeObject(), ns, vars, node->path, depth + 1);
                        if (template2)
                            node->factoryControls.emplace_back(simple, std::move(template2));
                    }
                }
                if (const Json* factory = node->props.Find("factory"); factory && factory->IsObject()) {
                    if (const Json* ids = factory->Find("control_ids"); ids && ids->IsObject()) {
                        for (const auto& member : ids->object) {
                            if (!member.second.IsString())
                                continue;
                            std::string value = member.second.string;
                            std::string r;
                            std::string n = SplitName(value, &r);
                            if (r.empty() && !n.empty()) {
                                r = n;
                                const size_t dotAt = n.find('.');
                                if (dotAt != std::string::npos)
                                    n = n.substr(dotAt + 1);
                            }
                            if (n.empty())
                                n = member.first;
                            auto template2 = Build(n + "@" + r, Json::MakeObject(), ns, vars, node->path, depth + 1);
                            if (template2)
                                node->factoryControls.emplace_back(member.first, std::move(template2));
                        }
                    }
                    if (const Json* name2 = factory->Find("control_name"); name2 && name2->IsString() && node->type != "factory") {
                        std::string refName = name2->string;
                        if (!refName.empty() && refName[0] == '@') refName.erase(0, 1);
                        const size_t dot = refName.find('.');
                        const std::string simple = dot == std::string::npos ? refName : refName.substr(dot + 1);
                        auto template2 = Build(simple + "@" + refName, Json::MakeObject(), ns, vars, node->path, depth + 1);
                        if (template2)
                            node->factoryControls.emplace_back(simple, std::move(template2));
                    }
                }
                return node;
            }

        private:
            UiLibrary& library;
            BuildReport& report;

            void AssignVariable(Vars& vars, const std::string& rawName, const Json& value) {
                std::string name = rawName;
                bool onlyDefault = false;
                const size_t bar = name.find('|');
                if (bar != std::string::npos) {
                    onlyDefault = name.substr(bar + 1) == "default";
                    name = name.substr(0, bar);
                }
                if (onlyDefault && vars.count(name))
                    return;
                if (value.IsString() && value.string.find('$') != std::string::npos) {
                    vars[name] = Substitute(value, vars, nullptr);
                    return;
                }
                vars[name] = value;
            }

            void MergeChildren(std::vector<ChildEntry>& list, const Json& controls, const std::string& ns) {
                if (!controls.IsArray())
                    return;
                for (const Json& element : controls.array) {
                    if (!element.IsObject())
                        continue;
                    for (const auto& member : element.object) {
                        ChildEntry entry{member.first, member.second, ns};
                        const std::string name = SplitName(member.first);
                        bool replaced = false;
                        for (ChildEntry& existing : list)
                            if (SplitName(existing.key) == name) {
                                existing = entry;
                                replaced = true;
                                break;
                            }
                        if (!replaced)
                            list.push_back(std::move(entry));
                    }
                }
            }

            void ResolveChain(const std::string& ref, const std::string& currentNs, std::vector<Layer>& out, int depth) {
                if (depth > 32)
                    return;
                std::string ns = currentNs;
                std::string name = ref;
                const size_t dot = ref.find('.');
                if (dot != std::string::npos) {
                    ns = ref.substr(0, dot);
                    name = ref.substr(dot + 1);
                }
                const UiLibrary::Def* def = library.Find(ns, name);
                if (!def && dot == std::string::npos)
                    def = library.Find("common", name);
                if (!def) {
                    report.missingControls.insert(ns + "." + name);
                    return;
                }
                std::string parent;
                SplitName(def->key, &parent);
                if (!parent.empty()) {
                    if (parent[0] == '@')
                        parent.erase(0, 1);
                    if (parent.find('$') == std::string::npos)
                        ResolveChain(parent, def->ns, out, depth + 1);
                }
                out.push_back({def->json, def->ns});
            }
        };
    }

    std::unique_ptr<Node> BuildTree(UiLibrary& library, const std::string& reference, const Vars& rootVars,
                                    BuildReport& report) {
        Builder builder(library, report);
        const size_t dot = reference.find('.');
        const std::string ns = dot == std::string::npos ? "" : reference.substr(0, dot);
        const std::string name = dot == std::string::npos ? reference : reference.substr(dot + 1);
        Vars vars = library.GlobalVariables();
        for (const auto& entry : rootVars)
            vars[entry.first] = entry.second;
        return builder.Build(name + "@" + reference, Json::MakeObject(), ns, vars, {}, 0);
    }
}
