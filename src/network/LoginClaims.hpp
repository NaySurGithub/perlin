#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::claims {

    struct Header {
        std::string alg = "ES384";
        std::string x5u;
    };

    struct Algorithm {
        std::string alg = "ES384";
    };

    struct Multiplayer {
        std::string cpk;
        std::string xid = "0";
        std::string xname;
        std::string identity;
        std::string leguuid;
        std::string mid;
        std::string iss = "self";
        std::string aud = "api://auth-minecraft-services/multiplayer";
        int64_t iat = 0;
        int64_t nbf = 0;
        int64_t exp = 0;
    };

    struct Fingerprint {
        std::string algorithm;
        std::string digest;
    };

    struct Fingerprints {
        std::vector<Fingerprint> fingerprint;
    };

    struct Assertion {
        std::string fingerprints;
        std::string token;
    };

    struct IdentityProvider {
        std::string domain;
        std::string protocol = "default";
    };

    struct Envelope {
        std::string assertion;
        IdentityProvider idp;
    };

    struct Chain {
        std::vector<std::string> chain{""};
    };

    // First link of the online certificate chain: signed by the client key, vouching for the key that signed the
    // chain Mojang's service returned.
    struct ChainLink {
        int64_t exp = 0;
        int64_t nbf = 0;
        std::string identityPublicKey;
        bool certificateAuthority = true;
    };

    struct Identity {
        std::string Certificate;
        int AuthenticationType = 2;
        std::string Token;
    };

    struct ClientData {
        std::vector<std::string> AnimatedImageData;
        std::string ArmSize = "wide";
        std::string CapeData;
        std::string CapeId;
        bool CapeOnClassicSkin = false;
        int CapeImageHeight = 0;
        int CapeImageWidth = 0;
        int ClientEditorConnectionIntent = 0;
        bool ClientIsEditorCapable = false;
        int64_t ClientRandomId = 0;
        bool CompatibleWithClientSideChunkGen = false;
        int CurrentInputMode = 1;
        int DefaultInputMode = 1;
        std::string DeviceId;
        std::string DeviceModel = "Windows";
        int DeviceOS = 12;
        bool FilterProfanity = false;
        std::string GameVersion;
        int GraphicsMode = 1;
        int GuiScale = -1;
        bool IsEditorMode = false;
        std::string LanguageCode = "en_US";
        int MaxViewDistance = 32;
        int MemoryTier = 0;
        bool OverrideSkin = false;
        std::vector<std::string> PersonaPieces;
        bool PersonaSkin = false;
        std::vector<std::string> PieceTintColors;
        std::string PlatformOfflineId;
        std::string PlatformOnlineId;
        int PlatformType = 0;
        std::string PlayFabId;
        bool PremiumSkin = false;
        std::string SelfSignedId;
        std::string ServerAddress;
        std::vector<std::string> SkinAnimationData;
        std::string SkinColor = "#0";
        std::string SkinData;
        std::string SkinGeometryName = "geometry.humanoid.custom";
        std::string SkinGeometry;
        std::string SkinGeometryDataEngineVersion;
        std::string SkinId = "Standard_Steve";
        int SkinImageHeight = 64;
        int SkinImageWidth = 64;
        std::string SkinResourcePatch = "{}";
        std::string ThirdPartyName;
        bool TrustedSkin = false;
        int UIProfile = 0;
        bool ServerAuthoritativeMovement = false;
        bool ServerAuthoritativeBlockBreaking = false;
    };

}
