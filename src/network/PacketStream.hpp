#pragma once
#include "network/VarInt.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace perlin::net {

    struct Vec3 {
        float x = 0, y = 0, z = 0;
    };
    struct BlockPos {
        int32_t x = 0;
        uint32_t y = 0;
        int32_t z = 0;
    };
    struct Uuid {
        std::array<uint8_t, 16> bytes{};
    };
    template <class T> struct LE {
        T value{};
        operator T() const { return value; }
        LE& operator=(T v) {
            value = v;
            return *this;
        }
    };
    template <class T> struct BE {
        T value{};
        operator T() const { return value; }
        BE& operator=(T v) {
            value = v;
            return *this;
        }
    };
    template <class T> struct Opt {
        T value{};
        bool present = false;
        Opt& operator=(const T& v) {
            value = v;
            present = true;
            return *this;
        }
    };
    struct Rest {
        std::vector<uint8_t> bytes;
    };

    constexpr size_t kMaxListLen = 1u << 16;

    class ReadStream {
    public:
        static constexpr bool kReading = true;
        ReadStream(const uint8_t* data, size_t size) : r(data, size) {}
        bool ok = true;
        bool more() const { return ok && r.Remaining() > 0; }

        void u8(uint8_t& v) { Do(r.ReadByte(v)); }
        void i8(int8_t& v) { Do(r.ReadInt8(v)); }
        void boolean(bool& v) {
            uint8_t b = 0;
            Do(r.ReadByte(b));
            v = b != 0;
        }
        void le16(uint16_t& v) { Do(r.ReadLE16(v)); }
        void le32(uint32_t& v) { Do(r.ReadLE32(v)); }
        void le64(uint64_t& v) { Do(r.ReadLE64(v)); }
        void be32(int32_t& v) { Do(r.ReadBE32(v)); }
        void f32(float& v) { Do(r.ReadLEFloat(v)); }
        void varu32(uint32_t& v) { Do(r.ReadVarUint32(v)); }
        void varu64(uint64_t& v) { Do(r.ReadVarUint64(v)); }
        void zz32(int32_t& v) { Do(r.ReadZigzag32(v)); }
        void zz64(int64_t& v) { Do(r.ReadZigzag64(v)); }
        void str(std::string& v) { Do(r.ReadString(v)); }
        void bytes(std::vector<uint8_t>& v) { Do(r.ReadByteArray(v)); }
        void raw(uint8_t* dst, size_t n) {
            std::vector<uint8_t> tmp;
            if (Do(r.ReadBytes(tmp, n)))
                for (size_t i = 0; i < n; i++)
                    dst[i] = tmp[i];
        }
        void rest(std::vector<uint8_t>& v) {
            if (ok)
                r.ReadBytes(v, r.Remaining());
        }
        bool count(uint32_t& n) {
            varu32(n);
            if (ok && n > kMaxListLen)
                ok = false;
            return ok;
        }
        wire::Reader& reader() { return r; }

    private:
        bool Do(bool good) {
            if (!good)
                ok = false;
            return ok;
        }
        wire::Reader r;
    };

    class WriteStream {
    public:
        static constexpr bool kReading = false;
        explicit WriteStream(std::vector<uint8_t>& out) : o(out) {}
        bool ok = true;
        bool more() const { return true; }

        void u8(const uint8_t& v) { o.push_back(v); }
        void i8(const int8_t& v) { o.push_back((uint8_t)v); }
        void boolean(const bool& v) { o.push_back(v ? 1 : 0); }
        void le16(const uint16_t& v) { wire::WriteLE16(o, v); }
        void le32(const uint32_t& v) { wire::WriteLE32(o, v); }
        void le64(const uint64_t& v) {
            for (int i = 0; i < 8; i++)
                o.push_back((uint8_t)(v >> (8 * i)));
        }
        void be32(const int32_t& v) { wire::WriteBE32(o, v); }
        void f32(const float& v) { wire::WriteLEFloat(o, v); }
        void varu32(const uint32_t& v) { wire::WriteVarUint32(o, v); }
        void varu64(const uint64_t& v) { wire::WriteVarUint64(o, v); }
        void zz32(const int32_t& v) { wire::WriteZigzag32(o, v); }
        void zz64(const int64_t& v) { wire::WriteZigzag64(o, v); }
        void str(const std::string& v) { wire::WriteString(o, v); }
        void bytes(const std::vector<uint8_t>& v) {
            wire::WriteVarUint32(o, (uint32_t)v.size());
            o.insert(o.end(), v.begin(), v.end());
        }
        void raw(const uint8_t* src, size_t n) { o.insert(o.end(), src, src + n); }
        void rest(const std::vector<uint8_t>& v) { o.insert(o.end(), v.begin(), v.end()); }
        bool count(uint32_t n) {
            wire::WriteVarUint32(o, n);
            return true;
        }
        std::vector<uint8_t>& buffer() { return o; }

    private:
        std::vector<uint8_t>& o;
    };

    namespace detail {
        struct Any {
            template <class T> operator T&() const;
        };
        template <class T, class Seq, class = void> struct CanInit : std::false_type {};
        template <class T, size_t... I>
        struct CanInit<T, std::index_sequence<I...>, std::void_t<decltype(T{(void(I), Any{})...})>>
            : std::true_type {};
        template <class T, size_t N = 0> constexpr size_t FieldCount() {
            if constexpr (CanInit<T, std::make_index_sequence<N + 1>>::value)
                return FieldCount<T, N + 1>();
            else
                return N;
        }
        template <class T> auto ToTuple(T& v) {
            constexpr size_t N = FieldCount<T>();
            static_assert(N <= 24, "packet has more than 24 fields; split it into nested structs");
            if constexpr (N == 0) {
                return std::tuple<>{};
            } else if constexpr (N == 1) {
                auto& [a0] = v;
                return std::tie(a0);
            } else if constexpr (N == 2) {
                auto& [a0, a1] = v;
                return std::tie(a0, a1);
            } else if constexpr (N == 3) {
                auto& [a0, a1, a2] = v;
                return std::tie(a0, a1, a2);
            } else if constexpr (N == 4) {
                auto& [a0, a1, a2, a3] = v;
                return std::tie(a0, a1, a2, a3);
            } else if constexpr (N == 5) {
                auto& [a0, a1, a2, a3, a4] = v;
                return std::tie(a0, a1, a2, a3, a4);
            } else if constexpr (N == 6) {
                auto& [a0, a1, a2, a3, a4, a5] = v;
                return std::tie(a0, a1, a2, a3, a4, a5);
            } else if constexpr (N == 7) {
                auto& [a0, a1, a2, a3, a4, a5, a6] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6);
            } else if constexpr (N == 8) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7);
            } else if constexpr (N == 9) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8);
            } else if constexpr (N == 10) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
            } else if constexpr (N == 11) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
            } else if constexpr (N == 12) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
            } else if constexpr (N == 13) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
            } else if constexpr (N == 14) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
            } else if constexpr (N == 15) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
            } else if constexpr (N == 16) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
            } else if constexpr (N == 17) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
            } else if constexpr (N == 18) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                                a17);
            } else if constexpr (N == 19) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18] =
                    v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                                a17, a18);
            } else if constexpr (N == 20) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18,
                       a19] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                                a17, a18, a19);
            } else if constexpr (N == 21) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18,
                       a19, a20] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                                a17, a18, a19, a20);
            } else if constexpr (N == 22) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18,
                       a19, a20, a21] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                                a17, a18, a19, a20, a21);
            } else if constexpr (N == 23) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18,
                       a19, a20, a21, a22] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                                a17, a18, a19, a20, a21, a22);
            } else if constexpr (N == 24) {
                auto& [a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18,
                       a19, a20, a21, a22, a23] = v;
                return std::tie(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16,
                                a17, a18, a19, a20, a21, a22, a23);
            }
        }
        template <class T> struct IsVector : std::false_type {};
        template <class T, class A> struct IsVector<std::vector<T, A>> : std::true_type {};
    }

    template <class S, class T> void Field(S& s, T& v) {
        using D = std::remove_cv_t<T>;
        if constexpr (std::is_same_v<D, bool>)
            s.boolean(v);
        else if constexpr (std::is_same_v<D, uint8_t>)
            s.u8(v);
        else if constexpr (std::is_same_v<D, float>)
            s.f32(v);
        else if constexpr (std::is_same_v<D, int32_t>)
            s.zz32(v);
        else if constexpr (std::is_same_v<D, int64_t>)
            s.zz64(v);
        else if constexpr (std::is_same_v<D, uint32_t>)
            s.varu32(v);
        else if constexpr (std::is_same_v<D, uint64_t>)
            s.varu64(v);
        else if constexpr (std::is_same_v<D, std::string>)
            s.str(v);
        else if constexpr (std::is_same_v<D, Vec3>) {
            s.f32(v.x);
            s.f32(v.y);
            s.f32(v.z);
        } else if constexpr (std::is_same_v<D, BlockPos>) {
            s.zz32(v.x);
            s.varu32(v.y);
            s.zz32(v.z);
        } else if constexpr (std::is_same_v<D, Uuid>)
            s.raw(v.bytes.data(), v.bytes.size());
        else if constexpr (std::is_same_v<D, Rest>)
            s.rest(v.bytes);
        else if constexpr (std::is_same_v<D, LE<uint16_t>>)
            s.le16(v.value);
        else if constexpr (std::is_same_v<D, LE<uint32_t>>)
            s.le32(v.value);
        else if constexpr (std::is_same_v<D, LE<uint64_t>>)
            s.le64(v.value);
        else if constexpr (std::is_same_v<D, BE<int32_t>>)
            s.be32(v.value);
        else if constexpr (std::is_same_v<D, std::vector<uint8_t>>)
            s.bytes(v);
        else if constexpr (detail::IsVector<D>::value) {
            uint32_t n = (uint32_t)v.size();
            if (!s.count(n))
                return;
            if constexpr (S::kReading) {
                v.clear();
                v.reserve(n);
                for (uint32_t i = 0; i < n && s.ok; i++) {
                    typename D::value_type e{};
                    Field(s, e);
                    if (s.ok)
                        v.push_back(std::move(e));
                }
            } else {
                for (auto& e : v)
                    Field(s, e);
            }
        } else if constexpr (requires {
                                 v.present;
                                 v.value;
                             }) {
            if constexpr (S::kReading) {
                v.present = s.more();
                if (v.present)
                    Field(s, v.value);
            } else if (v.present) {
                Field(s, v.value);
            }
        } else if constexpr (std::is_aggregate_v<D>) {
            std::apply([&](auto&... f) { (Field(s, f), ...); }, detail::ToTuple(v));
        } else {
            static_assert(!sizeof(D*), "unsupported packet field type");
        }
    }

    template <class P> struct PacketTraits;

#define PERLIN_PACKET_ID(T, ID)                                                                              \
    template <> struct PacketTraits<T> {                                                                     \
        static constexpr uint32_t kId = ID;                                                                  \
        static constexpr const char* kName = #T;                                                             \
    }

    template <class P> struct PacketSerializer {
        static void Write(WriteStream& s, const P& packet) { Field(s, packet); }
        static bool Read(ReadStream& s, P& packet) {
            Field(s, packet);
            return s.ok;
        }
    };

    template <class P> bool Decode(const uint8_t* data, size_t size, P& out) {
        ReadStream rs(data, size);
        return PacketSerializer<P>::Read(rs, out) && rs.ok;
    }
    template <class P> bool Decode(const std::vector<uint8_t>& body, P& out) {
        return Decode(body.data(), body.size(), out);
    }
    template <class P> std::vector<uint8_t> Encode(const P& packet) {
        std::vector<uint8_t> body;
        WriteStream ws(body);
        PacketSerializer<P>::Write(ws, packet);
        return body;
    }

}
