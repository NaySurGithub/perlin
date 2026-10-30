#include "world/vanilla/DataPack.hpp"

#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlin::server::vanilla {
    namespace fs = std::filesystem;

    struct DataPack::Impl {
        mz_zip_archive zip{};
        bool isZip = false;
        fs::path root;
        std::map<std::string, mz_uint> entries;
    };

    DataPack::~DataPack() {
        if (impl && impl->isZip)
            mz_zip_reader_end(&impl->zip);
    }

    std::unique_ptr<DataPack> DataPack::Open(const std::string& path, std::string& error) {
        auto pack = std::unique_ptr<DataPack>(new DataPack());
        pack->impl = std::make_unique<Impl>();
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            pack->impl->root = fs::path(path);
            return pack;
        }
        if (!mz_zip_reader_init_file(&pack->impl->zip, path.c_str(), 0)) {
            error = "cannot open " + path;
            return nullptr;
        }
        pack->impl->isZip = true;
        const std::string prefix = "data/minecraft/";
        const mz_uint count = mz_zip_reader_get_num_files(&pack->impl->zip);
        for (mz_uint i = 0; i < count; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&pack->impl->zip, i, &st) || st.m_is_directory)
                continue;
            const std::string name = st.m_filename;
            if (name.rfind(prefix, 0) == 0)
                pack->impl->entries[name.substr(prefix.size())] = i;
        }
        return pack;
    }

    bool DataPack::Has(const std::string& relative) const {
        if (impl->isZip)
            return impl->entries.count(relative) != 0;
        std::error_code ec;
        return fs::is_regular_file(impl->root / "data/minecraft" / relative, ec);
    }

    std::string DataPack::Read(const std::string& relative) const {
        if (impl->isZip) {
            const auto it = impl->entries.find(relative);
            if (it == impl->entries.end())
                return {};
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&impl->zip, it->second, &size, 0);
            if (!data)
                return {};
            std::string out(static_cast<const char*>(data), size);
            mz_free(data);
            return out;
        }
        std::ifstream in(impl->root / "data/minecraft" / relative, std::ios::binary);
        std::stringstream buf;
        buf << in.rdbuf();
        return buf.str();
    }

    std::vector<std::string> DataPack::List(const std::string& prefix) const {
        std::vector<std::string> out;
        if (impl->isZip) {
            for (const auto& [name, index] : impl->entries)
                if (name.rfind(prefix, 0) == 0)
                    out.push_back(name);
            return out;
        }
        std::error_code ec;
        const fs::path base = impl->root / "data/minecraft";
        for (const auto& e : fs::recursive_directory_iterator(base / prefix, ec))
            if (e.is_regular_file())
                out.push_back(fs::relative(e.path(), base).generic_string());
        return out;
    }
}
