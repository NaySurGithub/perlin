#include "util/Paths.hpp"
#include "network/NetherNet.hpp"

#include "network/Http.hpp"
#include "client/Options.hpp"

#include <rtc/rtc.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include "util/Json.hpp"
#include <random>
#include <sstream>
#include <thread>

namespace perlin {

    namespace {

        std::pair<int, std::string> HttpRequest(const std::string& host, uint16_t port,
                                                const std::string& verb, const std::string& path,
                                                const std::string& body, const std::string& contentType) {
            http::Response r = http::Request(host, port, false, verb, path, body, contentType);
            return {r.status, std::move(r.body)};
        }

        std::string StripIdentityLines(const std::string& sdp) {
            std::istringstream in(sdp);
            std::string line, out;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.rfind("a=identity", 0) == 0)
                    continue;
                out += line + "\r\n";
            }
            return out;
        }
    }

    struct NetherNet::Impl {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> reliable;
        std::shared_ptr<rtc::DataChannel> unreliable;
        std::atomic<bool> gatheringDone{false};
        std::atomic<bool> reliableOpen{false};
        std::atomic<bool> failed{false};
        std::string offerSdp;
    };

    NetherNet::NetherNet() = default;
    NetherNet::~NetherNet() {
        Disconnect();
    }

    bool NetherNet::Probe(const std::string& host, uint16_t port, NetherNetServerInfo& info,
                          std::string& error) {
        auto [status, body] = HttpRequest(host, port, "GET", "/v1/join", "", "");
        if (status == 0) {
            error = "HTTP connect failed (is the server running with transport=nethernet?)";
            return false;
        }
        if (status < 200 || status >= 300) {
            error = "GET /v1/join -> HTTP " + std::to_string(status) + " " + body;
            return false;
        }
        auto parsed = json::Read<NetherNetServerInfo>(body);
        if (!parsed) {
            error = "GET /v1/join returned non-JSON: " + body;
            return false;
        }
        info = std::move(*parsed);
        return true;
    }

    void NetherNet::OnReliableMessage(const uint8_t* data, size_t size) {
        if (size == 0)
            return;
        std::lock_guard<std::mutex> lk(reassemblyMutex);
        reassembly.insert(reassembly.end(), data + 1, data + size);
        if (data[0] == 0) {
            inbound.enqueue(std::move(reassembly));
            reassembly.clear();
        }
    }

    void NetherNet::OnUnreliableMessage(const uint8_t* data, size_t size) {
        if (size == 0)
            return;
        inbound.enqueue(std::vector<uint8_t>(data + 1, data + size));
    }

    bool NetherNet::Connect(const std::string& host, uint16_t port, const ClientIdentity& identity) {
        Disconnect();
        lastError.clear();

        if (!Probe(host, port, serverInfo, lastError))
            return false;
        std::cout << "[nethernet] probe ok: '" << serverInfo.name << "' protocol " << serverInfo.protocol
                  << " version " << serverInfo.version << std::endl;

        std::random_device rd;
        std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) | rd());
        uint64_t netId = rng();
        if (netId == 0)
            netId = 1;

        impl = std::make_unique<Impl>();
        try {
            rtc::Configuration config;
            impl->pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            lastError = std::string("PeerConnection create failed: ") + e.what();
            Disconnect();
            return false;
        }

        impl->pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
            if (state == rtc::PeerConnection::GatheringState::Complete)
                impl->gatheringDone = true;
        });
        impl->pc->onStateChange([this](rtc::PeerConnection::State state) {
            if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
                impl->failed = true;
        });

        try {
            impl->reliable = impl->pc->createDataChannel("ReliableDataChannel");

            rtc::DataChannelInit unrel;
            unrel.reliability.unordered = true;
            unrel.reliability.maxRetransmits = 0u;
            impl->unreliable = impl->pc->createDataChannel("UnreliableDataChannel", unrel);
        } catch (const std::exception& e) {
            lastError = std::string("DataChannel create failed: ") + e.what();
            Disconnect();
            return false;
        }

        impl->reliable->onOpen([this]() { impl->reliableOpen = true; });
        impl->reliable->onClosed([this]() { impl->failed = true; });
        impl->reliable->onMessage([this](rtc::message_variant msg) {
            if (auto* b = std::get_if<rtc::binary>(&msg))
                OnReliableMessage(reinterpret_cast<const uint8_t*>(b->data()), b->size());
        });
        impl->unreliable->onMessage([this](rtc::message_variant msg) {
            if (auto* b = std::get_if<rtc::binary>(&msg))
                OnUnreliableMessage(reinterpret_cast<const uint8_t*>(b->data()), b->size());
        });

        impl->pc->setLocalDescription();
        std::cout << "[nethernet] offer requested, gathering ICE..." << std::endl;

        bool gathered = false;
        {
            auto start = std::chrono::steady_clock::now();
            while (!impl->gatheringDone.load()) {
                if (impl->failed.load())
                    break;
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(15))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            gathered = impl->gatheringDone.load();
        }
        if (!gathered) {
            lastError = "ICE gathering did not complete (no usable host candidates?)";
            Disconnect();
            return false;
        }

        std::string offer;
        try {
            auto desc = impl->pc->localDescription();
            if (!desc) {
                lastError = "no local description after gathering";
                Disconnect();
                return false;
            }
            offer = std::string(*desc);
        } catch (const std::exception& e) {
            lastError = std::string("localDescription failed: ") + e.what();
            Disconnect();
            return false;
        }

        std::string offerClean;
        offerClean.reserve(offer.size());
        for (size_t i = 0; i < offer.size(); i++) {
            if (offer[i] == '\r' && i + 1 < offer.size() && offer[i + 1] == '\r')
                continue;
            offerClean.push_back(offer[i]);
        }
        offer.swap(offerClean);

        if (!identity.Valid() || identity.MultiplayerToken().empty()) {
            lastError = "no client identity for offer";
            Disconnect();
            return false;
        }
        std::string identified = identity.AssertOffer(offer);
        if (identified.empty()) {
            lastError = "failed to build offer identity assertion";
            Disconnect();
            return false;
        }
        offer.swap(identified);
        std::cout << "[nethernet] offer identity added (" << offer.size() << " bytes)" << std::endl;

        std::string netIdStr = std::to_string(netId);
        std::string wpath = "/v1/join/" + netIdStr;
        std::cout << "[nethernet] offer ready (" << offer.size() << " bytes), POSTing to /v1/join/"
                  << netIdStr << "..." << std::endl;
        auto [status, answer] = HttpRequest(host, port, "POST", wpath, offer, "application/sdp");
        std::cout << "[nethernet] POST status " << status << ", answer " << answer.size() << " bytes"
                  << std::endl;
        if (status == 0) {
            lastError = "POST offer: HTTP connect failed";
            Disconnect();
            return false;
        }
        if (status < 200 || status >= 300) {
            lastError = "POST /v1/join/" + netIdStr + " -> HTTP " + std::to_string(status) + " " +
                        answer.substr(0, 300);
            Disconnect();
            return false;
        }
        if (!answer.empty()) {
            std::cout << "[nethernet] answer head: ";
            for (size_t i = 0; i < answer.size() && i < 120; i++) {
                unsigned char c = answer[i];
                if (c >= 32 && c < 127)
                    std::cout << (char)c;
                else
                    std::cout << "\\x" << std::hex << (int)c << std::dec;
            }
            std::cout << std::endl;
        }
        if (answer.size() < 16 || answer.rfind("v=", 0) != 0) {
            lastError = "signaling refused the offer (session error " + answer.substr(0, 32) + ")";
            Disconnect();
            return false;
        }

        try {
            impl->pc->setRemoteDescription(
                rtc::Description(StripIdentityLines(answer), rtc::Description::Type::Answer));
        } catch (const std::exception& e) {
            lastError = std::string("setRemoteDescription failed: ") + e.what();
            Disconnect();
            return false;
        }

        std::cout << "[nethernet] remote description set, waiting for datachannel..." << std::endl;
        bool openFlag = false;
        {
            auto start = std::chrono::steady_clock::now();
            while (!impl->reliableOpen.load()) {
                if (impl->failed.load())
                    break;
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(20))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            openFlag = impl->reliableOpen.load();
        }
        if (!openFlag) {
            lastError = "ReliableDataChannel never opened (ICE/DTLS failed; "
                        "check server UDP reachability / firewall)";
            Disconnect();
            return false;
        }

        connected = true;
        std::cout << "[nethernet] ReliableDataChannel open" << std::endl;
        return true;
    }

    void NetherNet::Disconnect() {
        connected = false;
        if (impl) {
            Impl* doomed = impl.release();
            std::thread([doomed] {
                std::this_thread::sleep_for(std::chrono::milliseconds(750));
                delete doomed;
            }).detach();
        }
        std::vector<uint8_t> tmp;
        while (inbound.try_dequeue(tmp)) {}
        std::lock_guard<std::mutex> lk(reassemblyMutex);
        reassembly.clear();
    }

    static bool SendFramed(const std::shared_ptr<rtc::DataChannel>& dc, const std::vector<uint8_t>& data) {
        if (!dc || !dc->isOpen())
            return false;
        if (data.empty())
            return true;
        size_t total = data.size();
        size_t fragments = (total - 1) / NetherNet::kFragmentSize + 1;
        if (fragments > 256)
            return false;
        try {
            for (size_t i = 0; i < fragments; i++) {
                size_t off = i * NetherNet::kFragmentSize;
                size_t len = std::min(NetherNet::kFragmentSize, total - off);
                rtc::binary msg;
                msg.reserve(len + 1);
                msg.push_back(static_cast<std::byte>(fragments - 1 - i));
                msg.insert(msg.end(), reinterpret_cast<const std::byte*>(data.data() + off),
                           reinterpret_cast<const std::byte*>(data.data() + off + len));
                if (!dc->send(std::move(msg)))
                    return false;
            }
        } catch (...) {
            return false;
        }
        return true;
    }

    bool NetherNet::SendReliable(const std::vector<uint8_t>& data) {
        if (!connected || !impl)
            return false;
        return SendFramed(impl->reliable, data);
    }

    bool NetherNet::SendUnreliable(const std::vector<uint8_t>& data) {
        if (!connected || !impl)
            return false;
        if (data.size() > kFragmentSize)
            return false;
        return SendFramed(impl->unreliable, data);
    }

    bool NetherNet::TryReceive(std::vector<uint8_t>& out) {
        return inbound.try_dequeue(out);
    }

}
