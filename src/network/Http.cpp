#include "network/Http.hpp"
#include <httplib.h>

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
            return {res->status, res->body};
        };
        if (tls) {
            httplib::SSLClient cli(host, port);
            return run(cli);
        }
        httplib::Client cli(host, port);
        return run(cli);
    }
}
