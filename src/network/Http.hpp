#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace perlin::http {
    struct Response {
        int status = 0;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    Response Request(const std::string& host, uint16_t port, bool tls, const std::string& method,
                     const std::string& path, const std::string& body = {},
                     const std::string& contentType = {},
                     const std::map<std::string, std::string>& headers = {}, int timeoutSecs = 15);

    // Where the system keeps its trusted CA certificates (file and/or directory), for HTTPS.
    // Release builds link OpenSSL statically, so its compiled-in default (Ubuntu's /usr/lib/ssl)
    // is wrong on most other distributions (Arch, Fedora, openSUSE...): every HTTPS request then
    // fails certificate verification. Checks SSL_CERT_FILE / SSL_CERT_DIR, then the usual paths.
    // Returns false where the TLS library finds the system store itself (Windows, macOS).
    bool SystemCaPaths(std::string& file, std::string& dir);

#if defined(__ANDROID__)
    // Every certificate from the system trust store (the conscrypt module's, else /system's) as one
    // PEM text. Android keeps them one per file under old-style hash names, and loading them through
    // OpenSSL's directory lookup is unreliable, so they are read here and handed over in memory.
    const std::string& AndroidCaBundle();
#endif

    // Points an httplib client (Client or SSLClient) at the system CA certificates.
    template <class HttpClient>
    void UseSystemCa(HttpClient& cli) {
#if defined(__ANDROID__)
        const std::string& pem = AndroidCaBundle();
        if (!pem.empty()) {
            cli.load_ca_cert_store(pem.c_str(), pem.size());
            return;
        }
#endif
        std::string file, dir;
        if (SystemCaPaths(file, dir))
            cli.set_ca_cert_path(file, dir);
    }

    // One line describing why an httplib request failed (error kind plus the TLS error codes).
    template <class HttpResult>
    std::string Describe(const HttpResult& res);

    Response GetUrl(const std::string& url, const std::map<std::string, std::string>& headers = {}, int timeoutSecs = 60);
}
