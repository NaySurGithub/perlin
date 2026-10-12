#pragma once
#include <string>

namespace perlin {
    class ClientIdentity;

    // The "Identity" JSON of the Login packet (certificate, authentication type, token).
    //  - `onlineChainJson` null: offline login (self-signed multiplayer token, AuthenticationType 2).
    //  - otherwise: the legacy chain Mojang's service issued ({"chain":[...]}) gets a first link signed by the client
    //    key that points at the key which signed the chain; AuthenticationType 0, Token = the OIDC multiplayer token.
    // Returns an empty string and fills `error` on failure.
    std::string BuildIdentityJson(ClientIdentity& identity, const std::string* onlineChainJson, std::string& error);
}
