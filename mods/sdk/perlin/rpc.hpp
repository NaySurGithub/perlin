#pragma once
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "perlin/abi.hpp"

namespace perlin {
    using abi::Op;
    using abi::Status;

    struct Error {
        Status status = Status::Failed;

        constexpr std::string_view message() const {
            switch (status) {
            case Status::Ok: return "ok";
            case Status::UnknownOp: return "this game version does not provide that operation";
            case Status::Denied: return "permission denied";
            case Status::BadArguments: return "bad arguments";
            case Status::Unavailable: return "not available right now";
            case Status::NotFound: return "not found";
            case Status::Failed: return "failed";
            }
            return "failed";
        }
        constexpr bool denied() const { return status == Status::Denied; }
        constexpr bool unavailable() const { return status == Status::Unavailable; }
        constexpr bool not_found() const { return status == Status::NotFound; }
    };

    template <class T>
    using Result = std::expected<T, Error>;

    namespace detail {
        extern "C" {
        __attribute__((import_module("perlin"), import_name("call"))) int32_t perlin_host_call(uint32_t op, const void* in, uint32_t inLen,
                                                                                               void* out, uint32_t outCap);
        }

        inline Result<std::vector<uint8_t>> call(Op op, const abi::Writer& args) {
            std::vector<uint8_t> out(2048);
            for (int attempt = 0; attempt < 4; attempt++) {
                const int32_t rc = perlin_host_call(static_cast<uint32_t>(op), args.Data(), static_cast<uint32_t>(args.Size()), out.data(),
                                                    static_cast<uint32_t>(out.size()));
                if (rc >= 0) {
                    out.resize(static_cast<size_t>(rc));
                    return out;
                }
                if (rc < -abi::kNeedMoreBase) {
                    out.resize(static_cast<size_t>(-rc - abi::kNeedMoreBase));
                    continue;
                }
                return std::unexpected(Error{static_cast<Status>(rc)});
            }
            return std::unexpected(Error{Status::Failed});
        }

        inline Result<std::vector<uint8_t>> call(Op op) { return call(op, abi::Writer{}); }

        template <class T>
        Result<T> call_as(Op op, const abi::Writer& args) {
            auto bytes = call(op, args);
            if (!bytes)
                return std::unexpected(bytes.error());
            abi::Reader r(bytes->data(), bytes->size());
            T value = T::Read(r);
            if (!r.Ok())
                return std::unexpected(Error{Status::Failed});
            return value;
        }

        template <class T>
        Result<T> call_as(Op op) {
            return call_as<T>(op, abi::Writer{});
        }

        inline Result<void> call_void(Op op, const abi::Writer& args) {
            auto bytes = call(op, args);
            if (!bytes)
                return std::unexpected(bytes.error());
            return {};
        }

        inline Result<void> call_void(Op op) { return call_void(op, abi::Writer{}); }
    }
}
