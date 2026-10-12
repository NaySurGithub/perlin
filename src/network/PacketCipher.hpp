#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    class ClientIdentity;

    // Bedrock packet encryption (AES-256-CTR plus an 8 byte SHA-256 checksum per batch), switched on by the server's
    // ServerToClientHandshake. Public servers (RakNet) require it; NetherNet already sits inside DTLS.
    class PacketCipher {
    public:
        PacketCipher();
        ~PacketCipher();
        PacketCipher(const PacketCipher&) = delete;
        PacketCipher& operator=(const PacketCipher&) = delete;

        // `token` is the JWT of ServerToClientHandshake: its x5u header is the server's key, `salt` the claim.
        bool InitFromHandshake(const ClientIdentity& identity, const std::string& token, std::string& error);
        // Direct key, for tests.
        bool InitWithKey(const uint8_t key[32]);

        bool Active() const { return active; }

        // plain = compression byte + batch; returns it encrypted with the checksum appended.
        std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plain);
        // Decrypts in place semantics: returns false when the checksum does not match.
        bool Decrypt(const std::vector<uint8_t>& cipher, std::vector<uint8_t>& plain);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        bool active = false;
    };
}
