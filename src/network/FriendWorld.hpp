#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    namespace xbox {
        class Account;
    }

    // Joining a friend's world: the host publishes the world in Xbox's session directory, the joiner
    // joins that session (over Xbox's real-time-activity socket) to receive a per-player nonce, then
    // WebRTC is negotiated through Minecraft's signalling service (NetherNet).
    namespace friendworld {

        struct IceServerInfo {
            std::string username;
            std::string password;
            std::vector<std::string> urls;
        };

        // A signalling channel to the Minecraft signalling service (plain WebSocket or JSON-RPC).
        class Signaling {
        public:
            virtual ~Signaling() = default;
            // Delivers "TYPE <connectionId> <data>" to the peer. With waitDelivery the call returns once the service
            // reports delivery (or an error).
            virtual bool Send(const std::string& toNetworkId, const std::string& text, bool waitDelivery, std::string& error) = 0;
            // Incoming signals: the sender's network id and the "TYPE <connectionId> <data>" text.
            virtual void SetHandler(std::function<void(const std::string& from, const std::string& text)> handler) = 0;
            // TURN/STUN servers the service hands out for the connection.
            virtual bool Credentials(std::vector<IceServerInfo>& servers, std::string& error) = 0;
            virtual bool Alive() const = 0;
            virtual void Close() = 0;
        };

        // Everything a joined friend world needs to be dialled; keeps the Xbox session and the signalling
        // connection alive until released.
        struct Lease {
            std::shared_ptr<Signaling> signaling;
            std::string hostNetworkId; // where signals to the host are addressed
            std::string nonce;         // goes into the login's client data
            std::string issuer;        // identity provider of the multiplayer token
            std::string hostName;
            std::string worldName;
            std::string version;
            int protocol = 0;
            std::function<void()> leave; // leaves the Xbox session and closes the real-time socket

            Lease() = default;
            Lease(const Lease&) = delete;
            Lease& operator=(const Lease&) = delete;
            ~Lease() { Release(); }
            void Release() {
                if (signaling)
                    signaling->Close();
                signaling.reset();
                if (leave) {
                    auto fn = std::move(leave);
                    leave = nullptr;
                    fn();
                }
            }
        };

        using ProgressFn = std::function<void(const std::string&)>;

        // Finds the world the friend hosts, joins its Xbox session, waits for the host's nonce and opens the
        // signalling connection. Blocking (tens of seconds at most): call it from a worker thread.
        bool Join(xbox::Account& account, const std::string& friendXuid, const std::string& gameVersion,
                  std::shared_ptr<Lease>& out, const ProgressFn& progress, std::string& error);

    }
}
