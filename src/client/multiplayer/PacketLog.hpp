#pragma once
#include <cstddef>
#include <cstdint>

namespace perlin {
    // Runtime packet capture, switched on and off without restarting:
    //   create <data dir>/packetlog  -> every packet in and out is appended to <data dir>/packets.log as JSON
    //   delete it                    -> capture stops
    // The control file may list packet names (space, comma or newline separated) to keep only those;
    // a name starting with '-' is excluded instead (e.g. "-level_chunk -subchunk -player_auth_input").
    namespace packetlog {
        void Record(bool outgoing, uint32_t id, const uint8_t* data, size_t size);
    }
}
