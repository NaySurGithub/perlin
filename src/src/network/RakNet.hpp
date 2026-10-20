#pragma once
#include "network/Transport.hpp"

#include <atomic>
#include <concurrentqueue.h>
#include <memory>
#include <string>
#include <vector>

namespace perlin {

    class RakNet : public Transport {
    public:
        RakNet();
        ~RakNet() override;

        RakNet(const RakNet&) = delete;
        RakNet& operator=(const RakNet&) = delete;

        static bool Ping(const std::string& host, uint16_t port, std::string& advertisement, std::string& error,
                         int timeoutMs = 1500);

        bool Connect(const std::string& host, uint16_t port);
        void Disconnect() override;

        bool SendReliable(const std::vector<uint8_t>& data) override;
        bool SendUnreliable(const std::vector<uint8_t>& data) override { return SendReliable(data); }
        bool TryReceive(std::vector<uint8_t>& out) override;

        bool IsConnected() const override { return connected; }
        std::string LastError() const override;
        const NetherNetServerInfo& Info() const override { return info; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        std::atomic<bool> connected{false};
        NetherNetServerInfo info;
        mutable std::string lastError;
    };

}
