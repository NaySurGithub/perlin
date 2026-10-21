#include "client/renderer/bgfx/GpuSelect.hpp"

#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#endif

namespace perlin::gpu {
#if defined(_WIN32)
    namespace {
        constexpr uint16_t kVendorMicrosoft = 0x1414;

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
        };

        using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);

        bool Better(const Adapter& a, const Adapter& b) {
            if (a.dedicatedVram != b.dedicatedVram)
                return a.dedicatedVram > b.dedicatedVram;
            return a.sharedMem > b.sharedMem;
        }
    }

    Choice ChooseRenderer() {
        Choice choice;
        choice.type = bgfx::RendererType::Direct3D11;
        choice.reason = "Direct3D 11";

        HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
        if (!dxgi)
            return choice;
        const auto createFactory = reinterpret_cast<CreateFactory1Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
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
                std::cout << "[gpu] adapter " << i << ": " << a.name << " vendor=0x" << std::hex << a.vendor << " device=0x"
                          << a.device << std::dec << " vram=" << (a.dedicatedVram >> 20) << "MB" << std::endl;
                if (!haveBest || Better(a, best)) {
                    best = a;
                    haveBest = true;
                }
            }
            adapter->Release();
        }
        factory->Release();
        if (!haveBest)
            return choice;
        choice.vendorId = best.vendor;
        choice.deviceId = best.device;
        choice.adapterName = best.name;
        choice.reason = "fastest hardware adapter, Direct3D 11";
        return choice;
    }
#else
    Choice ChooseRenderer() { return {}; }
#endif

}
