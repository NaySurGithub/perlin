#pragma once

#include "client/player/BdsEntityMembership.hpp"

namespace perlin::physics::bds {
    enum class GroundJumpRequest : uint32_t {
        LocalPlayer = 0x3B2DFE4A,
        Other = 0x9D529862,
        Squid = 0x3B31D80A,
        LavaSlime = 0x83B7F546,
    };

    // JumpFromGroundFilterSystem callback 0x140EAF890. Classification tests
    // membership, not flag-component payloads; LocalPlayer has first priority.
    inline GroundJumpRequest SelectGroundJumpRequest(uint32_t entity,
            const EntityMembership& localPlayers, const EntityMembership& lavaSlimes,
            const EntityMembership& squids) {
        if (localPlayers.Contains(entity)) return GroundJumpRequest::LocalPlayer;
        if (lavaSlimes.Contains(entity)) return GroundJumpRequest::LavaSlime;
        if (squids.Contains(entity)) return GroundJumpRequest::Squid;
        return GroundJumpRequest::Other;
    }

    template<class Lookup, class Create>
    void ClassifyGroundJump(uint32_t entity, const EntityMembership& localPlayers,
            const EntityMembership& lavaSlimes, const EntityMembership& squids,
            Lookup&& lookup, Create&& create) {
        const auto request = SelectGroundJumpRequest(entity, localPlayers, lavaSlimes, squids);
        if (!lookup(request).Contains(entity))
            create(request, entity);
    }
}
