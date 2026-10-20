#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace perlin::mods {
    // A plugin's WebAssembly instance, run by WasmEdge's interpreter.
    //
    // Both 32-bit and 64-bit (memory64) modules load; the module's own memory type decides which.
    // Imports are linked by name with exactly the types the module declares, so pointer and size
    // parameters may be i32 or i64 (host functions see every integer argument widened to 64 bits).
    class WasmInstance {
    public:
        // A host import. Args/results are integers widened to 64 bits (i32 values zero-extended).
        // Returning false stops the running call; `error` says why.
        struct HostContext {
            WasmInstance& instance;
            std::string& error;
        };
        using HostFn = std::function<bool(HostContext& ctx, const uint64_t* args, uint32_t argCount, uint64_t* results, uint32_t resultCount)>;

        // Resolves an import (module, name) to a handler; return an empty function to reject it.
        using Resolver = std::function<HostFn(const std::string& module, const std::string& name)>;

        WasmInstance();
        ~WasmInstance();
        WasmInstance(const WasmInstance&) = delete;
        WasmInstance& operator=(const WasmInstance&) = delete;

        // Parses, validates, links and instantiates. Fails with a readable error.
        bool Load(const uint8_t* bytes, size_t size, const Resolver& resolver, std::string& error);

        bool Is64() const { return is64; }
        bool HasExport(const char* name) const;

        // Calls an exported function. Arguments are converted to the parameter types the export
        // declares; `result` (if any) gets the first return value widened to 64 bits.
        bool Call(const char* name, std::initializer_list<uint64_t> args, uint64_t* result, std::string& error);

        // Bounds-checked view of linear memory; nullptr if [ptr, ptr+len) is out of range.
        // Valid until the module runs again (memory may grow and move).
        uint8_t* Memory(uint64_t ptr, uint64_t len);

        // Stops whatever the instance is running, from any thread. The interrupted call fails.
        void Interrupt();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        bool is64 = false;
    };

    // Short name of the WebAssembly engine, for logs ("WasmEdge 0.18.0").
    std::string WasmEngineName();
}
