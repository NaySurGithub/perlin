#include "network/Http.hpp"
#include <httplib.h>

#include <cctype>

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
}
