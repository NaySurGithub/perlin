#include "network/Http.hpp"
#include <httplib.h>

#include <cctype>
#include <cstdlib>

namespace perlin::http {
    Response Request(const std::string& host, uint16_t port, bool tls, const std::string& method,
                     const std::string& path, const std::string& body, const std::string& contentType,
                     const std::map<std::string, std::string>& extra, int timeoutSecs) {
        auto run = [&](auto& cli) -> Response {
            cli.set_connection_timeout(timeoutSecs);
            cli.set_read_timeout(timeoutSecs);
            cli.set_write_timeout(timeoutSecs);
            cli.set_follow_location(true);
            httplib::Headers headers;
            for (auto& [k, v] : extra)
                headers.emplace(k, v);
            httplib::Result res =
                method == "GET" ? cli.Get(path, headers) : cli.Post(path, headers, body, contentType.c_str());
            if (!res)
                return {};
            Response out{res->status, res->body, {}};
            for (auto& [k, v] : res->headers) {
                std::string key = k;
                for (char& c : key)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                out.headers.emplace(std::move(key), v);
            }
            return out;
        };
        if (tls) {
            httplib::SSLClient cli(host, port);
#ifdef __ANDROID__
            cli.set_ca_cert_path(std::string(), "/system/etc/security/cacerts");
#endif
            return run(cli);
        }
        httplib::Client cli(host, port);
        return run(cli);
    }

    Response GetUrl(const std::string& url, const std::map<std::string, std::string>& headers, int timeoutSecs) {
        size_t start = url.find("://");
        const bool tls = url.compare(0, 5, "https") == 0;
        start = start == std::string::npos ? 0 : start + 3;
        const size_t slash = url.find('/', start);
        std::string host = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
        uint16_t port = tls ? 443 : 80;
        if (const size_t colon = host.find(':'); colon != std::string::npos) {
            port = static_cast<uint16_t>(std::atoi(host.c_str() + colon + 1));
            host.resize(colon);
        }
        std::map<std::string, std::string> extra = headers;
        extra.emplace("User-Agent", "Perlin");
        return Request(host, port, tls, "GET", path, {}, {}, extra, timeoutSecs);
    }
}
