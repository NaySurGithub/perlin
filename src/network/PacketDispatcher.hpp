#pragma once
#include "network/PacketStream.hpp"
#include <functional>
#include <iostream>
#include <unordered_map>

namespace perlin::net {

    class PacketDispatcher {
    public:
        template <class P, class Fn> void On(Fn&& fn) {
            entries[PacketTraits<P>::kId] = {PacketTraits<P>::kName,
                                             [fn = std::forward<Fn>(fn)](const uint8_t* d, size_t n) {
                                                 P pkt;
                                                 if (!Decode(d, n, pkt))
                                                     return false;
                                                 fn(pkt);
                                                 return true;
                                             }};
        }
        template <class Fn> void OnRaw(uint32_t id, const char* name, Fn&& fn) {
            entries[id] = {name, [fn = std::forward<Fn>(fn)](const uint8_t* d, size_t n) {
                               fn(d, n);
                               return true;
                           }};
        }

        bool Dispatch(uint32_t id, const uint8_t* data, size_t size) const {
            auto it = entries.find(id);
            if (it == entries.end())
                return false;
            if (!it->second.fn(data, size))
                std::cout << "[net] failed to decode " << it->second.name << " (id " << id << ", " << size
                          << " bytes)" << std::endl;
            return true;
        }

    private:
        struct Entry {
            const char* name = "";
            std::function<bool(const uint8_t*, size_t)> fn;
        };
        std::unordered_map<uint32_t, Entry> entries;
    };

}
