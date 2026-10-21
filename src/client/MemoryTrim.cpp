#include "client/MemoryTrim.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <malloc.h>
#include <windows.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

namespace perlin {
    void TrimMemory(bool aggressive) {
#if defined(_WIN32)
        _heapmin();
        // Deliberately no SetProcessWorkingSetSize(-1, -1) here, even when `aggressive`. Emptying the working set
        // while the window is in the background pages the whole game out, and every page then has to be faulted
        // back in when the window is focused again, which is the FPS crash on returning to the window.
        (void)aggressive;
#elif defined(__GLIBC__)
        (void)aggressive;
        malloc_trim(0);
#else
        (void)aggressive;
#endif
    }

    bool ProcessInBackground() {
#if defined(_WIN32)
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return true;
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        return pid != GetCurrentProcessId();
#else
        return false;
#endif
    }
}
