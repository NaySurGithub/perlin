#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace perlin::http {
    struct Response {
        int status = 0;
        std::string body;
    };

    Response Request(const std::string& host, uint16_t port, bool tls, const std::string& method,
                     const std::string& path, const std::string& body = {},
                     const std::string& contentType = {},
                     const std::map<std::string, std::string>& headers = {}, int timeoutSecs = 15);
}
