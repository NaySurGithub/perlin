#include "network/Realms.hpp"

#include "network/Http.hpp"
#include "network/Transport.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace perlin::realms {
    namespace {
        constexpr const char* kRelyingParty = "https://pocket.realms.minecraft.net/";
        // The endpoint gophertunnel falls back to when service discovery does not name another.
        constexpr const char* kHost = "bedrock.frontendlegacy.realms.minecraft-services.net";

        std::string Q(const std::string& s) { return "\"" + s + "\""; }

        http::Response Call(const std::string& method, const std::string& path, const std::string& body,
                            const std::string& authorization) {
            std::map<std::string, std::string> headers{
                {"User-Agent", "libhttpclient/1.0.0.0"},
                {"Client-Version", kBedrockVersion},
                {"X-ClientPlatform", "Android"},
                {"X-NetworkProtocolVersion", std::to_string(kBedrockProtocol)},
                {"Authorization", authorization},
            };
            return http::Request(kHost, 443, true, method, path, body, body.empty() ? std::string() : "application/json",
                                 headers, 20);
        }

        std::string Failure(const http::Response& r, const char* what) {
            if (r.status == 0)
                return std::string("could not reach ") + what;
            std::string text = std::string(what) + " answered HTTP " + std::to_string(r.status);
            if (auto doc = json::Document::Parse(r.body)) {
                const std::string why = doc->root()["errorMsg"].isString() ? doc->root()["errorMsg"].str()
                                        : doc->root()["message"].isString() ? doc->root()["message"].str()
                                                                              : std::string();
                if (!why.empty())
                    text += ": " + why;
            }
            return text;
        }
    }

    bool List(xbox::Account& account, std::vector<Realm>& out, std::string& error) {
        out.clear();
        std::string authorization, xuid;
        if (!account.AuthorizationHeader(kRelyingParty, authorization, xuid, error))
            return false;
        const http::Response r = Call("GET", "/worlds", {}, authorization);
        auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
        if (!doc) {
            error = Failure(r, "Realms");
            return false;
        }
        for (const json::View s : doc->root()["servers"].items()) {
            Realm realm;
            realm.id = s["id"].isNumber() ? static_cast<int64_t>(s["id"].number()) : 0;
            realm.name = s["name"].str();
            realm.owner = s["owner"].str();
            realm.motd = s["motd"].str();
            realm.state = s["state"].str();
            realm.expired = s["expired"].boolean() || s["expiredTrial"].boolean();
            realm.member = s["ownerUUID"].str() != xuid;
            realm.maxPlayers = s["maxPlayers"].isNumber() ? static_cast<int>(s["maxPlayers"].number()) : 0;
            if (realm.id != 0)
                out.push_back(std::move(realm));
        }
        return true;
    }

    bool Join(xbox::Account& account, int64_t id, Address& out, std::string& error) {
        std::string authorization, xuid;
        if (!account.AuthorizationHeader(kRelyingParty, authorization, xuid, error))
            return false;
        const std::string body = "{\"joinIntention\":\"VANILLA\",\"pingRegions\":[]}";
        const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        while (true) {
            const http::Response r = Call("POST", "/worlds/" + std::to_string(id) + "/join", body, authorization);
            if ((r.status == 503 || r.status == 277) && std::chrono::steady_clock::now() < giveUp) {
                // The Realm is starting: wait as long as the service asks (5 s when it does not say).
                int wait = 5;
                const auto it = r.headers.find("Retry-After");
                if (it != r.headers.end())
                    wait = std::clamp(std::atoi(it->second.c_str()), 1, 15);
                std::cout << "[realms] Realm " << id << " is starting, retrying in " << wait << "s" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(wait));
                continue;
            }
            if (r.status == 403) {
                error = "you are not a member of this Realm";
                return false;
            }
            if (r.status == 404) {
                error = "this Realm no longer exists";
                return false;
            }
            auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
            if (!doc || !doc->root()["address"].isString()) {
                error = Failure(r, "Realms");
                return false;
            }
            const std::string address = doc->root()["address"].str();
            const size_t colon = address.rfind(':');
            out.host = colon == std::string::npos ? address : address.substr(0, colon);
            out.port = colon == std::string::npos ? uint16_t(19132) : static_cast<uint16_t>(std::atoi(address.c_str() + colon + 1));
            out.protocol = doc->root()["networkProtocol"].isString() ? doc->root()["networkProtocol"].str() : "DEFAULT";
            for (char& c : out.protocol)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            std::cout << "[realms] Realm " << id << " is at " << out.host << ":" << out.port << " (" << out.protocol << ")" << std::endl;
            return !out.host.empty();
        }
    }

}
