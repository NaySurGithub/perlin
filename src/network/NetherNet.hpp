#pragma once
#include "network/ClientIdentity.hpp"
#include <atomic>
#include <concurrentqueue.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin {

    struct NetherNetServerInfo {
        std::string name;
        int protocol = 0;
        std::string version;
        std::string level;
        int players = 0;
        int maxPlayers = 0;
        int gameType = 0;
    };

    class NetherNet {
    public:
        static constexpr size_t kFragmentSize = 262143;

        using PacketQueue = moodycamel::ConcurrentQueue<std::vector<uint8_t>>;

        NetherNet();
        ~NetherNet();

        NetherNet(const NetherNet&) = delete;
        NetherNet& operator=(const NetherNet&) = delete;

        static bool Probe(const std::string& host, uint16_t port, NetherNetServerInfo& info,
                          std::string& error);

        bool Connect(const std::string& host, uint16_t port, const ClientIdentity& identity);
        void Disconnect();

        bool SendReliable(const std::vector<uint8_t>& data);
        bool SendUnreliable(const std::vector<uint8_t>& data);
        bool SendPacket(const std::vector<uint8_t>& data) { return SendReliable(data); }

        bool TryReceive(std::vector<uint8_t>& out);

        bool IsConnected() const { return connected; }
        std::string LastError() const { return lastError; }
        const NetherNetServerInfo& Info() const { return serverInfo; }

    private:
        void OnReliableMessage(const uint8_t* data, size_t size);
        void OnUnreliableMessage(const uint8_t* data, size_t size);

        std::atomic<bool> connected{false};
        std::string lastError;
        NetherNetServerInfo serverInfo;

        struct Impl;
        std::unique_ptr<Impl> impl;

        PacketQueue inbound;
        std::vector<uint8_t> reassembly;
        std::mutex reassemblyMutex;
    };

}
