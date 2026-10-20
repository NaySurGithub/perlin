// Built with WasmEdge's own settings (see CMakeLists.txt): reaches the executor behind the C API
// handle to interrupt a plugin that runs too long.
#include "executor/executor.h"

#include <wasmedge/wasmedge.h>

extern "C" void PerlinWasmEdgeStop(WasmEdge_ExecutorContext* executor) noexcept {
    if (executor)
        reinterpret_cast<WasmEdge::Executor::Executor*>(executor)->stop();
}
