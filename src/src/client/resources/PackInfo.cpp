#include "client/resources/PackInfo.hpp"

#include "client/ui/Json.hpp"

#include <miniz.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        struct Source {
            bool zip = false;
            fs::path folder;
            mz_zip_archive archive{};
            bool open = false;
            std::string prefix;

            ~Source() {
                if (open)
                    mz_zip_reader_end(&archive);
            }

            bool Read(const std::string& rel, std::string& out) {
                if (!zip) {
                    std::ifstream in(folder / rel, std::ios::binary);
                    if (!in)
                        return false;
                    out.assign(std::istreambuf_iterator<char>(in), {});
                    return true;
                }
                const int index = mz_zip_reader_locate_file(&archive, (prefix + rel).c_str(), nullptr, 0);
                if (index < 0)
                    return false;
                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&archive, static_cast<mz_uint>(index), &size, 0);
                if (!data)
                    return false;
                out.assign(static_cast<const char*>(data), size);
                mz_free(data);
                return true;
            }
        };

        std::string TrimExt(std::string name) {
            const size_t dot = name.rfind('.');
            return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
        }
    }

    PackMeta ReadPackMeta(const std::string& path) {
        PackMeta meta;
        const fs::path p(path);
        meta.name = p.filename().string();
        Source src;
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            src.folder = p;
        } else {
            if (!mz_zip_reader_init_file(&src.archive, path.c_str(), 0))
                return meta;
            src.zip = true;
            src.open = true;
            const mz_uint count = mz_zip_reader_get_num_files(&src.archive);
            for (mz_uint i = 0; i < count; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&src.archive, i, &st))
                    continue;
                const std::string file = Lower(st.m_filename);
                const size_t slash = file.find('/');
                if (file == "manifest.json" || file == "pack_icon.png" || file == "pack.png" || file == "pack.mcmeta")
                    break;
                if (slash != std::string::npos && (file.compare(slash + 1, std::string::npos, "manifest.json") == 0 ||
                                                   file.compare(slash + 1, std::string::npos, "pack_icon.png") == 0 ||
                                                   file.compare(slash + 1, std::string::npos, "pack.png") == 0 ||
                                                   file.compare(slash + 1, std::string::npos, "pack.mcmeta") == 0)) {
                    src.prefix = std::string(st.m_filename).substr(0, slash + 1);
                    break;
                }
            }
        }
        std::string text;
        if (src.Read("manifest.json", text)) {
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error))
                if (const ui::Json* header = json.Find("header")) {
                    if (const ui::Json* n = header->Find("name"))
                        meta.name = n->StringOr(meta.name);
                    if (const ui::Json* d = header->Find("description"))
                        meta.description = d->StringOr("");
                    if (const ui::Json* v = header->Find("version"))
                        if (v->IsArray()) {
                            for (size_t i = 0; i < v->array.size(); i++)
                                meta.version += (i ? "." : "") + std::to_string(static_cast<int>(v->array[i].NumberOr(0)));
                        }
                }
        } else if (src.Read("pack.json", text)) {
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error)) {
                if (const ui::Json* n = json.Find("name"))
                    meta.name = n->StringOr(meta.name);
                if (const ui::Json* d = json.Find("description"))
                    meta.description = d->StringOr("");
            }
        } else if (src.Read("pack.mcmeta", text)) {
            ui::Json json;
            std::string error;
            if (ui::Json::Parse(text, json, error))
                if (const ui::Json* pack = json.Find("pack"))
                    if (const ui::Json* d = pack->Find("description"))
                        meta.description = d->StringOr("");
        }
        if (meta.name.empty() || meta.name.find("pack.name") != std::string::npos)
            meta.name = TrimExt(p.filename().string());
        for (const char* icon : {"pack_icon.png", "pack.png", "icon.png"}) {
            std::string bytes;
            if (src.Read(icon, bytes) && LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), meta.icon)) {
                meta.hasIcon = true;
                break;
            }
        }
        return meta;
    }
}
