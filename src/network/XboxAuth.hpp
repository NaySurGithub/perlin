#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace perlin::xbox {

    // Everything the Bedrock login needs from an online account.
    struct Credentials {
        std::string multiplayerToken; // OIDC token from the Minecraft authorization service ("Token" in the login)
        std::string chainJson;        // legacy certificate chain from multiplayer.minecraft.net: {"chain":[...]}
        std::string gamertag;
        std::string xuid;
    };

    // Called when the user has to approve the sign-in in a browser.
    using PromptFn = std::function<void(const std::string& url, const std::string& code)>;

    // A Microsoft / Xbox Live account. Signs in with the Microsoft device-code flow (the same one consoles use),
    // remembers the refresh token in `cachePath` and then walks the Xbox services (device, title, user, XSTS tokens)
    // to the Minecraft authorization service, the way the Android Bedrock client does.
    class Account {
    public:
        explicit Account(std::string cachePath);
        ~Account();
        Account(const Account&) = delete;
        Account& operator=(const Account&) = delete;

        bool HasSavedLogin() const;
        // Aborts a pending SignIn() from another thread (used on shutdown).
        void Cancel();

        // Obtains a Microsoft access token: refreshes the saved login, or runs the device-code flow and calls `prompt`.
        // Blocks until the user finishes (or the code expires). On failure `error` says why.
        bool SignIn(const PromptFn& prompt, std::string& error);

        // Needs SignIn first. `identityPublicKeyB64` is the DER public key (standard base64) of this session's
        // login key; `gameVersion` is the Bedrock version string, for the service discovery.
        bool LoginMinecraft(const std::string& identityPublicKeyB64, const std::string& gameVersion,
                            Credentials& out, std::string& error);

        // Deletes the saved login (the next online login asks for the code again).
        static void SignOut(const std::string& cachePath);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    // Pure helpers, exposed for tests.
    namespace detail {
        // Xbox request signature (policy version 1): base64(version|windowsTime|r|s). See go-xsapi xal/internal.
        std::string SignRequest(void* ecKeyP256, const std::string& method, const std::string& pathAndQuery,
                                const std::string& authorization, const std::string& body, int64_t unixNanos,
                                size_t maxBodyBytes = 0);
        // The 32-byte digest that SignRequest signs, for verification in tests.
        std::string RequestDigest(const std::string& method, const std::string& pathAndQuery,
                                  const std::string& authorization, const std::string& body, int64_t unixNanos,
                                  size_t maxBodyBytes = 0);
        // Tests: send every request to http://host:port/<original host><original path> instead of the internet.
        void SetTestEndpoint(const std::string& host, uint16_t port);
        int64_t ParseIsoTime(const std::string& iso);   // "2026-10-26T12:34:56.1234567Z" -> unix seconds, 0 on error
        int64_t ParseHttpDate(const std::string& date); // "Sun, 06 Nov 1994 08:49:37 GMT" -> unix seconds, 0 on error
    }

}
