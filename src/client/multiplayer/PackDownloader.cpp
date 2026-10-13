#include "client/multiplayer/PackDownloader.hpp"
#include "network/Http.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace perlin {

    namespace {
        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        std::string BareId(const std::string& id) {
            const size_t us = id.find('_');
            return Lower(us == std::string::npos ? id : id.substr(0, us));
        }

        std::string SafeName(std::string s) {
            for (char& c : s)
                if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.'))
                    c = '_';
            return s;
        }

        std::vector<uint8_t> Sha256(const std::vector<uint8_t>& data) {
            std::vector<uint8_t> out(32);
            unsigned int len = 0;
            EVP_Digest(data.data(), data.size(), out.data(), &len, EVP_sha256(), nullptr);
            out.resize(len);
            return out;
        }

        bool SplitUrl(const std::string& url, bool& tls, std::string& host, uint16_t& port, std::string& path) {
            size_t p = 0;
            if (url.rfind("https://", 0) == 0) {
                tls = true;
                p = 8;
            } else if (url.rfind("http://", 0) == 0) {
                tls = false;
                p = 7;
            } else {
                return false;
            }
            const size_t slash = url.find('/', p);
            std::string authority = url.substr(p, slash == std::string::npos ? std::string::npos : slash - p);
            path = slash == std::string::npos ? "/" : url.substr(slash);
            port = tls ? 443 : 80;
            const size_t colon = authority.rfind(':');
            if (colon != std::string::npos) {
                port = static_cast<uint16_t>(std::atoi(authority.c_str() + colon + 1));
                authority = authority.substr(0, colon);
            }
            host = authority;
            return !host.empty();
        }
    }

    std::string PackDownloader::UuidString(const net::Uuid& id) {
        char buf[40];

        uint8_t b[16];
        for (int i = 0; i < 8; i++) {
            b[i] = id.bytes[7 - i];
            b[8 + i] = id.bytes[15 - i];
        }
        std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0],
                      b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return buf;
    }

    void PackDownloader::Reset() {
        awaiting.clear();
        fileFor.clear();
        active.clear();
        downloaded = cached = 0;
    }

    bool PackDownloader::Have(const std::string& fileName, uint64_t size) const {
        std::error_code ec;
        const auto path = std::filesystem::path(directory) / fileName;
        if (!std::filesystem::is_regular_file(path, ec))
            return false;
        return size == 0 || std::filesystem::file_size(path, ec) == size;
    }

    bool PackDownloader::Store(const std::string& fileName, const std::vector<uint8_t>& bytes) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        const auto path = std::filesystem::path(directory) / fileName;
        const auto tmp = std::filesystem::path(directory) / (fileName + ".part");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out)
                return false;
        }
        std::filesystem::rename(tmp, path, ec);
        return !ec;
    }

    std::vector<std::string> PackDownloader::Plan(const std::vector<proto::TexturePackInfosEntry>& offered) {
        std::vector<std::string> request;
        for (const auto& pack : offered) {
            const std::string uuid = UuidString(pack.uuid);
            const std::string fileName = SafeName(uuid + "_" + pack.version) + ".zip";
            if (Have(fileName, pack.size)) {
                cached++;
                std::cout << "[packs] " << uuid << " " << pack.version << " already downloaded" << std::endl;
                continue;
            }
            if (!pack.cdnUrl.empty()) {
                bool tls = true;
                std::string host, path;
                uint16_t port = 443;
                if (SplitUrl(pack.cdnUrl, tls, host, port, path)) {
                    std::cout << "[packs] " << uuid << " from " << pack.cdnUrl << std::endl;
                    auto resp = http::Request(host, port, tls, "GET", path, {}, {}, {}, 60);
                    if (resp.status >= 200 && resp.status < 300 && !resp.body.empty()) {
                        std::vector<uint8_t> bytes(resp.body.begin(), resp.body.end());
                        if (Store(fileName, bytes)) {
                            downloaded++;
                            std::cout << "[packs] " << uuid << " downloaded (" << bytes.size() << " bytes)"
                                      << std::endl;
                            continue;
                        }
                    }
                    std::cout << "[packs] CDN download failed (HTTP " << resp.status
                              << "), asking the server instead" << std::endl;
                }
            }
            awaiting.insert(Lower(uuid));
            fileFor[Lower(uuid)] = fileName;
            request.push_back(uuid + "_" + pack.version);
        }
        return request;
    }

    PackDownloader::Result PackDownloader::OnDataInfo(const proto::ResourcePackDataInfoPacket& info) {
        Result r;
        const std::string key = BareId(info.packId);
        auto want = awaiting.find(key);
        if (want == awaiting.end()) {
            r.message = "unrequested pack " + info.packId;
            return r;
        }
        awaiting.erase(want);
        Active a;
        a.packId = info.packId;
        a.chunkCount = info.chunkCount;
        a.size = info.size;
        a.hash = info.hash;
        a.fileName = fileFor.count(key) ? fileFor[key] : SafeName(key) + ".zip";
        a.data.reserve(static_cast<size_t>(std::min<uint64_t>(info.size, 512ull << 20)));
        std::cout << "[packs] " << info.packId << ": " << info.size << " bytes in " << info.chunkCount << " chunks"
                  << std::endl;
        if (info.size > (512ull << 20)) {
            r.step = Step::Failed;
            r.message = "pack " + info.packId + " is larger than 512 MB";
            return r;
        }
        if (info.chunkCount == 0) {
            r.step = Step::PackDone;
            r.packId = info.packId;
            Store(a.fileName, a.data);
            downloaded++;
            return r;
        }
        r.step = Step::RequestChunk;
        r.packId = a.packId;
        r.chunkIndex = 0;
        active[key] = std::move(a);
        return r;
    }

    PackDownloader::Result PackDownloader::OnChunk(const proto::ResourcePackChunkDataPacket& chunk) {
        Result r;
        const std::string key = BareId(chunk.packId);
        auto it = active.find(key);
        if (it == active.end())
            return r;
        Active& a = it->second;
        if (chunk.chunkIndex != a.next) {
            r.step = Step::Failed;
            r.message = "pack chunk " + std::to_string(chunk.chunkIndex) + " arrived, expected " + std::to_string(a.next);
            return r;
        }
        a.data.insert(a.data.end(), chunk.payload.begin(), chunk.payload.end());
        a.next++;
        if (a.next < a.chunkCount) {
            r.step = Step::RequestChunk;
            r.packId = a.packId;
            r.chunkIndex = a.next;
            return r;
        }
        if (a.size != 0 && a.data.size() != a.size)
            std::cout << "[packs] warning: " << a.packId << " is " << a.data.size() << " bytes, the server said "
                      << a.size << std::endl;
        if (!a.hash.empty() && Sha256(a.data) != a.hash)
            std::cout << "[packs] warning: " << a.packId << " does not match the SHA-256 the server announced"
                      << std::endl;
        if (!Store(a.fileName, a.data)) {
            r.step = Step::Failed;
            r.message = "could not write " + a.fileName + " to " + directory;
            return r;
        }
        std::cout << "[packs] " << a.packId << " saved as " << a.fileName << " (" << a.data.size() << " bytes)"
                  << std::endl;
        downloaded++;
        r.step = Step::PackDone;
        r.packId = a.packId;
        active.erase(it);
        return r;
    }

}
