#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace perlin::xbox {

    struct Credentials {
        std::string multiplayerToken;
        std::string chainJson;
        std::string gamertag;
        std::string xuid;
        std::string playFabId;
    };

    // What the Minecraft services hand a signed-in player for peer-to-peer play.
    struct ServiceInfo {
        std::string authorization;   // "MCToken <jwt>": the service token (signalling and other services)
        std::string playerMessagingId; // the "pmid" claim: this player's address for signalling messages
        std::string issuer;          // identity provider of the multiplayer token (NetherNet identity domain)
        std::string signalingUri;    // wss base of the plain WebSocket signalling service
        std::string signalingAfdUri; // https base that names the nearest JSON-RPC signalling service
    };

    using PromptFn = std::function<void(const std::string& url, const std::string& code)>;

    class Account {
    public:
        explicit Account(std::string cachePath);
        ~Account();
        Account(const Account&) = delete;
        Account& operator=(const Account&) = delete;

        bool HasSavedLogin() const;
        void Cancel();

        bool SignIn(const PromptFn& prompt, std::string& error);
        // Signs in with the saved login only (never asks for a device code); false if there is none
        // or it no longer works.
        bool SignInSaved(std::string& error);

        // After SignIn: the account's gamertag (one Xbox Live token request).
        bool FetchGamertag(std::string& gamertag, std::string& error);

        // An Xbox Live authorization header ("XBL3.0 x=<uhs>;<token>") for a relying party, for services such as
        // Realms (https://pocket.realms.minecraft.net/) and the friends list (http://xboxlive.com). Needs a signed-in
        // account. xuid is filled in when the token carries it.
        bool AuthorizationHeader(const std::string& relyingParty, std::string& header, std::string& xuid, std::string& error);

        // Logs in to PlayFab with the Xbox account and starts a Minecraft services session.
        bool ServiceSession(const std::string& gameVersion, ServiceInfo& out, std::string& error);

        bool LoginMinecraft(const std::string& identityPublicKeyB64, const std::string& gameVersion,
                            Credentials& out, std::string& error);

        static void SignOut(const std::string& cachePath);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    namespace detail {
        std::string SignRequest(void* ecKeyP256, const std::string& method, const std::string& pathAndQuery,
                                const std::string& authorization, const std::string& body, int64_t unixNanos,
                                size_t maxBodyBytes = 0);
        std::string RequestDigest(const std::string& method, const std::string& pathAndQuery,
                                  const std::string& authorization, const std::string& body, int64_t unixNanos,
                                  size_t maxBodyBytes = 0);
        void SetTestEndpoint(const std::string& host, uint16_t port);
        int64_t ParseIsoTime(const std::string& iso);
        int64_t ParseHttpDate(const std::string& date);
    }

}
