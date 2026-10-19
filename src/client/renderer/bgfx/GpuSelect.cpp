#include "client/renderer/bgfx/GpuSelect.hpp"

#include "util/Paths.hpp"

#include <fstream>
#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#endif

namespace perlin::gpu {
    namespace {
        std::string MarkerPath() { return paths::DataDir() + "/d3d12_disabled"; }
    }

    bool D3D12Disabled() {
        std::ifstream f(MarkerPath());
        return f.good();
    }

    void RememberD3D12Failure(const std::string& why) {
        paths::EnsureDir(paths::DataDir());
        std::ofstream out(MarkerPath(), std::ios::trunc);
        out << why << "\nDelete this file to let Perlin try Direct3D 12 again.\n";
    }

#if defined(_WIN32)
    namespace {
        constexpr uint16_t kVendorNvidia = 0x10DE;
        constexpr uint16_t kVendorAmd = 0x1002;
        constexpr uint16_t kVendorIntel = 0x8086;
        constexpr uint16_t kVendorMicrosoft = 0x1414; // Basic Render Driver / WARP

        std::string Narrow(const wchar_t* w) {
            if (!w || !*w)
                return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1)
                return {};
            std::string out(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
            return out;
        }

        struct Adapter {
            uint16_t vendor = 0;
            uint16_t device = 0;
            std::string name;
            uint64_t dedicatedVram = 0;
            uint64_t sharedMem = 0;
            bool d3d12 = false;        // D3D12CreateDevice at feature level 12_0 succeeds
            bool d3d12Level12_1 = false;
        };

        using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
        using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

        bool Better(const Adapter& a, const Adapter& b) {
            // Prefer the one with the most dedicated memory (a discrete GPU over an integrated one), then
            // one that can run Direct3D 12, then more shared memory.
            if (a.dedicatedVram != b.dedicatedVram)
                return a.dedicatedVram > b.dedicatedVram;
            if (a.d3d12 != b.d3d12)
                return a.d3d12;
            return a.sharedMem > b.sharedMem;
        }
    }

    Choice ChooseRenderer() {
        Choice choice;
        choice.type = bgfx::RendererType::Direct3D11;
        choice.reason = "Direct3D 11 default";

        HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        if (!dxgi)
            return choice;
        const auto createFactory = reinterpret_cast<CreateFactory1Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
        HMODULE d3d12Lib = LoadLibraryW(L"d3d12.dll"); // absent before Windows 10
        const auto createDevice =
            d3d12Lib ? reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d12Lib, "D3D12CreateDevice")) : nullptr;
        if (!createFactory)
            return choice;

        IDXGIFactory1* factory = nullptr;
        if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory)
            return choice;

        Adapter best;
        bool haveBest = false;
        for (UINT i = 0;; i++) {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
                break;
            if (!adapter)
                continue;
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || desc.VendorId == kVendorMicrosoft;
            if (!software) {
                Adapter a;
                a.vendor = static_cast<uint16_t>(desc.VendorId);
                a.device = static_cast<uint16_t>(desc.DeviceId);
                a.name = Narrow(desc.Description);
                a.dedicatedVram = desc.DedicatedVideoMemory;
                a.sharedMem = desc.SharedSystemMemory;
                if (createDevice) {
                    // A null device pointer only asks whether the adapter can create one.
                    a.d3d12 = SUCCEEDED(createDevice(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr));
                    a.d3d12Level12_1 =
                        a.d3d12 && SUCCEEDED(createDevice(adapter, D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr));
                }
                std::cout << "[gpu] adapter " << i << ": " << a.name << " vendor=0x" << std::hex << a.vendor << " device=0x"
                          << a.device << std::dec << " vram=" << (a.dedicatedVram >> 20) << "MB d3d12="
                          << (a.d3d12 ? (a.d3d12Level12_1 ? "12_1" : "12_0") : "no") << std::endl;
                if (!haveBest || Better(a, best)) {
                    best = a;
                    haveBest = true;
                }
            }
            adapter->Release();
        }
        factory->Release();
        if (!haveBest)
            return choice; // only software adapters: leave it to bgfx

        choice.vendorId = best.vendor;
        choice.deviceId = best.device;
        choice.adapterName = best.name;

        const bool remote = GetSystemMetrics(SM_REMOTESESSION) != 0;
        const bool intel = best.vendor == kVendorIntel;
        const bool modern = best.vendor == kVendorNvidia || best.vendor == kVendorAmd ||
                            (intel && best.d3d12Level12_1);
        if (D3D12Disabled()) {
            choice.reason = "Direct3D 12 failed on an earlier run (delete d3d12_disabled to retry)";
        } else if (remote) {
            choice.reason = "remote desktop session";
        } else if (!best.d3d12) {
            choice.reason = "adapter does not support Direct3D 12 feature level 12_0";
        } else if (!modern) {
            choice.reason = "adapter is not on the Direct3D 12 allow-list";
        } else {
            choice.type = bgfx::RendererType::Direct3D12;
            choice.reason = "hardware adapter supports Direct3D 12";
        }
        return choice;
    }
#else
    Choice ChooseRenderer() { return {}; }
#endif

}
