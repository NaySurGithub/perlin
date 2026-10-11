#include "network/ClientIdentity.hpp"

#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>

#include <nlohmann/json.hpp>

#include <cstring>
#include <cstdio>
#include <ctime>
#include <random>

namespace perlin {

    std::string B64Standard(const uint8_t* data, size_t len) {
        if (len == 0)
            return {};
        std::string out(4 * ((len + 2) / 3), '\0');
        int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data, (int)len);
        out.resize(n < 0 ? 0 : n);
        return out;
    }

    std::string B64Url(const uint8_t* data, size_t len) {
        std::string s = B64Standard(data, len);
        for (char& c : s) {
            if (c == '+')
                c = '-';
            else if (c == '/')
                c = '_';
        }
        while (!s.empty() && s.back() == '=')
            s.pop_back();
        return s;
    }

    std::string RandomUuid() {
        std::random_device rd;
        std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) | rd());
        uint64_t hi = rng(), lo = rng();
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx", (unsigned)(hi >> 32),
                 (unsigned)((hi >> 16) & 0xFFFF), (unsigned)(((hi >> 8) & 0xF0) | 0x40),
                 (unsigned)(((lo >> 48) & 0x3FFF) | 0x8000), (unsigned long long)(lo & 0xFFFFFFFFFFFFULL));
        return buf;
    }

    ClientIdentity::ClientIdentity() {
        key = EC_KEY_new_by_curve_name(NID_secp384r1);
        if (key && !EC_KEY_generate_key(key)) {
            EC_KEY_free(key);
            key = nullptr;
        }
    }

    ClientIdentity::~ClientIdentity() {
        if (key)
            EC_KEY_free(key);
    }

    std::string ClientIdentity::PublicKeyB64() const {
        if (!key)
            return {};
        EVP_PKEY* pkey = EVP_PKEY_new();
        if (!pkey)
            return {};
        EVP_PKEY_set1_EC_KEY(pkey, key);
        unsigned char* der = nullptr;
        int len = i2d_PUBKEY(pkey, &der);
        EVP_PKEY_free(pkey);
        std::string out = len > 0 ? B64Standard(der, len) : "";
        OPENSSL_free(der);
        return out;
    }

    bool ClientIdentity::Sign(const std::string& signingInput, std::vector<uint8_t>& rawSig) const {
        if (!key)
            return false;
        unsigned char digest[48];
        if (!EVP_Digest(signingInput.data(), signingInput.size(), digest, nullptr, EVP_sha384(), nullptr))
            return false;
        ECDSA_SIG* sig = ECDSA_do_sign(digest, sizeof(digest), key);
        if (!sig)
            return false;
        const BIGNUM* r = nullptr;
        const BIGNUM* s = nullptr;
        ECDSA_SIG_get0(sig, &r, &s);
        rawSig.assign(96, 0);
        BN_bn2binpad(r, rawSig.data(), 48);
        BN_bn2binpad(s, rawSig.data() + 48, 48);
        ECDSA_SIG_free(sig);
        return true;
    }

    static std::string BuildJwt(const ClientIdentity& id, const nlohmann::json& header,
                                const nlohmann::json& claims) {
        std::string h = B64Url(header.dump());
        std::string p = B64Url(claims.dump());
        std::vector<uint8_t> raw;
        if (!id.Sign(h + "." + p, raw))
            return {};
        return h + "." + p + "." + B64Url(raw.data(), raw.size());
    }

    std::string ClientIdentity::BuildMultiplayerToken(const std::string& playerName,
                                                      const std::string& uuid) {
        mpToken.clear();
        if (!key)
            return mpToken;
        std::string cpk = PublicKeyB64();
        if (cpk.empty())
            return mpToken;
        int64_t now = (int64_t)time(nullptr);
        nlohmann::json header = {{"alg", "ES384"}, {"x5u", cpk}};
        nlohmann::json claims = {{"cpk", cpk},          {"xid", "0"},
                                 {"xname", playerName}, {"identity", uuid},
                                 {"leguuid", uuid},     {"mid", ""},
                                 {"iss", "self"},       {"aud", "api://auth-minecraft-services/multiplayer"},
                                 {"iat", now},          {"nbf", now},
                                 {"exp", now + 3600}};
        mpToken = BuildJwt(*this, header, claims);
        if (mpToken.empty())
            return mpToken;
        return mpToken;
    }

    std::string ClientIdentity::AssertOffer(const std::string& sdp) const {
        if (!key || mpToken.empty())
            return {};
        nlohmann::json entries = nlohmann::json::array();
        size_t pos = 0;
        const char* kPrefix = "a=fingerprint:";
        while ((pos = sdp.find(kPrefix, pos)) != std::string::npos) {
            size_t eol = sdp.find_first_of("\r\n", pos);
            std::string line = sdp.substr(pos, eol == std::string::npos ? eol : eol - pos);
            std::string rest = line.substr(strlen(kPrefix));
            size_t sp = rest.find(' ');
            entries.push_back({{"algorithm", rest.substr(0, sp)},
                               {"digest", sp == std::string::npos ? "" : rest.substr(sp + 1)}});
            pos = eol == std::string::npos ? sdp.size() : eol;
        }
        if (entries.empty())
            return {};
        std::string payloadB64 = B64Url(nlohmann::json({{"fingerprint", entries}}).dump());
        std::string headerB64 = B64Url(nlohmann::json({{"alg", "ES384"}}).dump());
        std::vector<uint8_t> raw;
        if (!Sign(headerB64 + "." + payloadB64, raw))
            return {};
        std::string fps = headerB64 + ".." + B64Url(raw.data(), raw.size());

        std::string assertion = nlohmann::json({{"fingerprints", fps}, {"token", mpToken}}).dump();
        std::string outer =
            nlohmann::json({{"assertion", assertion}, {"idp", {{"domain", ""}, {"protocol", "default"}}}})
                .dump();
        std::string env = B64Standard(outer);

        size_t mpos = sdp.find("m=");
        if (mpos == std::string::npos)
            return {};
        size_t bol = sdp.rfind('\n', mpos);
        bol = (bol == std::string::npos) ? 0 : bol + 1;
        return sdp.substr(0, bol) + "a=identity:" + env + "\r\n" + sdp.substr(bol);
    }

}
