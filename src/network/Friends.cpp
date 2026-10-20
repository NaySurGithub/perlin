#include "network/Friends.hpp"

#include "network/Http.hpp"
#include "util/Json.hpp"

#include <algorithm>

namespace perlin::friends {
    namespace {
        constexpr const char* kMinecraftTitle = "1739947436"; // Minecraft (Android), the title Perlin signs in as

        std::string TextOf(const json::View& v) {
            if (v.isString())
                return v.str();
            if (v.isNumber())
                return std::to_string(static_cast<long long>(v.number()));
            return {};
        }

        std::string Field(const json::View& v, const char* lower, const char* upper) {
            const std::string a = TextOf(v[lower]);
            return a.empty() ? TextOf(v[upper]) : a;
        }
    }

    bool List(xbox::Account& account, std::vector<Friend>& out, std::string& error) {
        out.clear();
        std::string authorization, xuid;
        if (!account.AuthorizationHeader("http://xboxlive.com", authorization, xuid, error))
            return false;
        const http::Response r =
            http::Request("peoplehub.xboxlive.com", 443, true, "GET",
                          "/users/me/people/social/decoration/detail,presenceDetail", {}, {},
                          {{"Authorization", authorization},
                           {"x-xbl-contract-version", "5"},
                           {"Accept-Language", "en-US"},
                           {"User-Agent", "libhttpclient/1.0.0.0"}},
                          20);
        auto doc = r.status == 200 ? json::Document::Parse(r.body) : std::nullopt;
        if (!doc) {
            error = r.status == 0 ? "could not reach the Xbox friends service"
                                  : "the Xbox friends service answered HTTP " + std::to_string(r.status);
            return false;
        }
        for (const json::View p : doc->root()["people"].items()) {
            Friend f;
            f.xuid = TextOf(p["xuid"]);
            f.gamertag = TextOf(p["gamertag"]);
            if (f.gamertag.empty())
                f.gamertag = TextOf(p["displayName"]);
            if (f.xuid.empty() || f.gamertag.empty())
                continue;
            f.online = p["presenceState"].str() == "Online";
            f.presence = TextOf(p["presenceText"]);
            for (const json::View d : p["presenceDetails"].items()) {
                if (Field(d, "titleId", "TitleId") == kMinecraftTitle) {
                    f.playingMinecraft = true;
                    f.online = true;
                    const std::string text = Field(d, "presenceText", "PresenceText");
                    if (!text.empty())
                        f.presence = text;
                }
            }
            out.push_back(std::move(f));
        }
        std::stable_sort(out.begin(), out.end(), [](const Friend& a, const Friend& b) {
            if (a.playingMinecraft != b.playingMinecraft)
                return a.playingMinecraft;
            if (a.online != b.online)
                return a.online;
            return a.gamertag < b.gamertag;
        });
        return true;
    }

}
