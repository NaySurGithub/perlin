#pragma once
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include <functional>

namespace perlin {
    namespace web_detail {
        struct LoopState {
            std::function<bool()> frame;
            std::function<void()> finish;
        };

        inline LoopState*& State() {
            static LoopState* state = nullptr;
            return state;
        }

        inline void Tick() {
            LoopState* s = State();
            if (!s->frame()) {
                emscripten_cancel_main_loop();
                s->finish();
            }
        }
    }

    inline void RunWebMainLoop(std::function<bool()> frame, std::function<void()> finish) {
        web_detail::State() = new web_detail::LoopState{std::move(frame), std::move(finish)};
        emscripten_set_main_loop(web_detail::Tick, 0, false);
    }
}
#endif
