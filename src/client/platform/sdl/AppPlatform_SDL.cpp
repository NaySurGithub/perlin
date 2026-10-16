#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "client/renderer/bgfx/RenderContext_bgfx.hpp"
#include "client/input/Keys.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <thread>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
    int KeyFromScancode(SDL_Scancode sc) {
        namespace K = perlin::Key;
        if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
            return 'A' + (sc - SDL_SCANCODE_A);
        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
            return '1' + (sc - SDL_SCANCODE_1);
        if (sc == SDL_SCANCODE_0)
            return '0';
        if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
            return K::F1 + (sc - SDL_SCANCODE_F1);
        switch (sc) {
        case SDL_SCANCODE_SPACE:
            return K::Space;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
            return K::Shift;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            return K::Control;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
            return K::Alt;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_AC_BACK:
            return K::Escape;
        case SDL_SCANCODE_RETURN:
            return K::Enter;
        case SDL_SCANCODE_TAB:
            return K::Tab;
        case SDL_SCANCODE_BACKSPACE:
            return K::Backspace;
        case SDL_SCANCODE_PAGEUP:
            return K::PageUp;
        case SDL_SCANCODE_PAGEDOWN:
            return K::PageDown;
        case SDL_SCANCODE_HOME:
            return K::Home;
        case SDL_SCANCODE_END:
            return K::End;
        case SDL_SCANCODE_DELETE:
            return K::Delete;
        case SDL_SCANCODE_SLASH:
            return K::Slash;
        case SDL_SCANCODE_LEFT:
            return K::Left;
        case SDL_SCANCODE_UP:
            return K::Up;
        case SDL_SCANCODE_RIGHT:
            return K::Right;
        case SDL_SCANCODE_DOWN:
            return K::Down;
        default:
            return 0;
        }
    }

    int ButtonIndex(Uint8 button) {
        switch (button) {
        case SDL_BUTTON_LEFT:
            return 0;
        case SDL_BUTTON_RIGHT:
            return 1;
        case SDL_BUTTON_MIDDLE:
            return 2;
        default:
            return -1;
        }
    }
}

#ifdef __EMSCRIPTEN__
EM_JS(int, perlin_js_confirm, (const char* title, const char* message), {
    return confirm(UTF8ToString(title) + "\n\n" + UTF8ToString(message)) ? 1 : 0;
});
#endif

std::unique_ptr<perlin::RenderContext> AppPlatform_SDL::CreateRenderContext() {
    return std::make_unique<perlin::RenderContext_bgfx>();
}

NativeWindowInfo AppPlatform_SDL::GetNativeWindowInfo() {
    return native;
}

void AppPlatform_SDL::GetFramebufferSize(int& width, int& height) {
    width = framebufferWidth;
    height = framebufferHeight;
}

void AppPlatform_SDL::SetTitle(const std::string& title) {
    std::lock_guard lock(titleMutex);
    pendingTitle = title;
    titleDirty = true;
}

void AppPlatform_SDL::ShowSignInCode(const std::string& url, const std::string& code) {
    struct Job {
        std::string url, code;
    };
    SDL_RunOnMainThread(
        [](void* data) {
            std::unique_ptr<Job> job(static_cast<Job*>(data));
            SDL_SetClipboardText(job->code.c_str());
            SDL_OpenURL(job->url.c_str());
        },
        new Job{url, code}, false);
}

void AppPlatform_SDL::SetClipboard(const std::string& text) {
    SDL_RunOnMainThread(
        [](void* data) {
            std::unique_ptr<std::string> t(static_cast<std::string*>(data));
            SDL_SetClipboardText(t->c_str());
        },
        new std::string(text), false);
}

void AppPlatform_SDL::OpenUrl(const std::string& url) {
    SDL_RunOnMainThread(
        [](void* data) {
            std::unique_ptr<std::string> u(static_cast<std::string*>(data));
            SDL_OpenURL(u->c_str());
        },
        new std::string(url), false);
}

bool AppPlatform_SDL::ConfirmDialog(const std::string& title, const std::string& message) {
#ifdef __EMSCRIPTEN__
    return perlin_js_confirm(title.c_str(), message.c_str()) == 1;
#else
    SDL_MessageBoxButtonData buttons[2] = {};
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[0].buttonID = 1;
    buttons[0].text = "Yes";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[1].buttonID = 0;
    buttons[1].text = "No";
    SDL_MessageBoxData data = {};
    data.flags = SDL_MESSAGEBOX_INFORMATION;
    data.window = window;
    data.title = title.c_str();
    data.message = message.c_str();
    data.numbuttons = 2;
    data.buttons = buttons;
    int pressed = 0;
    if (!SDL_ShowMessageBox(&data, &pressed))
        return false;
    return pressed == 1;
#endif
}

void AppPlatform_SDL::DrawTouchOverlay(perlin::RenderContext& renderer) {
    if (!touchMode || !game)
        return;
    touch.SetViewport(framebufferWidth, framebufferHeight);
    touch.Draw(renderer, touch.Gameplay(game->GetInput()));
}

bool AppPlatform_SDL::Initialize() {
#ifdef __EMSCRIPTEN__
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_ASYNCIFY, "0");
#endif
#ifdef SDL_PLATFORM_ANDROID
    touchMode = true;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
    if (const char* t = std::getenv("PERLIN_TOUCH"))
        touchMode = touchMode || std::atoi(t) != 0;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cout << "[sdl] SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Perlin");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1280);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 720);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
#ifdef SDL_PLATFORM_ANDROID
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
#endif
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window) {
        std::cout << "[sdl] window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

#ifdef __EMSCRIPTEN__
    SDL_SetWindowFillDocument(window, true);
#endif

    SDL_PropertiesID wp = SDL_GetWindowProperties(window);
    const char* driver = SDL_GetCurrentVideoDriver();
    std::string drv = driver ? driver : "";
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    native.window = const_cast<char*>("#canvas");
#elif defined(SDL_PLATFORM_WIN32)
    native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
    native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_ANDROID)
    native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#else
    if (drv == "wayland") {
        native.wayland = true;
        native.display = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        native.display = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        native.window = reinterpret_cast<void*>(
            (uintptr_t)SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    }
#endif
    if (!native.window) {
        std::cout << "[sdl] no native window handle for video driver '" << drv << "'" << std::endl;
        return false;
    }
    std::cout << "[sdl] video driver: " << drv << std::endl;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    framebufferWidth = w > 0 ? w : 1280;
    framebufferHeight = h > 0 ? h : 720;

#ifndef __EMSCRIPTEN__
    bgfx::renderFrame();
#endif

    game = std::make_unique<perlin::Perlin>(args);
    if (!game->Initialize(this))
        return false;
#ifndef __EMSCRIPTEN__
    SDL_RaiseWindow(window);
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)
        SetCursorCaptured(true);
#endif
    return true;
}

void AppPlatform_SDL::SetCursorCaptured(bool captured) {
    if (!window || captured == cursorCaptured)
        return;
    if (SDL_SetWindowRelativeMouseMode(window, captured)) {
        cursorCaptured = captured;
        if (game)
            game->GetInput().SetCaptured(captured);
    }
}

void AppPlatform_SDL::HandleEvent(const SDL_Event& e) {
    perlin::Input* input = game ? &game->GetInput() : nullptr;
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        isRunning = false;
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.data)
            PushDropped(e.drop.data);
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (e.window.data1 > 0 && e.window.data2 > 0) {
            framebufferWidth = e.window.data1;
            framebufferHeight = e.window.data2;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
        if (input && (input->pauseOnLostFocus || e.type != SDL_EVENT_WINDOW_FOCUS_LOST))
            input->ReleaseAll();
        SetCursorCaptured(false);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (input) {
            const int k = KeyFromScancode(e.key.scancode);
            if (k && !e.key.repeat)
                input->OnKeyDown(k);
            else if (k && e.key.repeat && input->TextMode())
                input->OnKeyRepeat(k);
            if (k == 'V' && input->TextMode() && (e.key.mod & SDL_KMOD_CTRL)) {
                if (char* clip = SDL_GetClipboardText()) {
                    const char* p = clip;
                    while (*p) {
                        Uint32 cp = SDL_StepUTF8(&p, nullptr);
                        if (cp >= 32 && cp != 127)
                            input->OnChar(static_cast<int>(cp));
                    }
                    SDL_free(clip);
                }
            }
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (input && input->TextMode()) {
            const char* p = e.text.text;
            while (p && *p) {
                Uint32 cp = SDL_StepUTF8(&p, nullptr);
                if (cp == 0)
                    break;
                input->OnChar(static_cast<int>(cp));
            }
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (input)
            if (int k = KeyFromScancode(e.key.scancode))
                input->OnKeyUp(k);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (input) {
            if (cursorCaptured)
                input->OnMouseDelta(e.motion.xrel, e.motion.yrel);
            input->OnMouseMove(e.motion.x * SDL_GetWindowPixelDensity(window), e.motion.y * SDL_GetWindowPixelDensity(window));
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!cursorCaptured && !(input && input->TextMode())) {
            SetCursorCaptured(true);
            break;
        }
        if (input)
            if (int b = ButtonIndex(e.button.button); b >= 0)
                input->OnMouseDown(b);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (input)
            input->OnMouseWheel(e.wheel.y);
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (input && touchMode) {
            touch.SetViewport(framebufferWidth, framebufferHeight);
            const float w = static_cast<float>(framebufferWidth), h = static_cast<float>(framebufferHeight);
            const int64_t id = static_cast<int64_t>(e.tfinger.fingerID);
            const float x = e.tfinger.x * w, y = e.tfinger.y * h;
            if (e.type == SDL_EVENT_FINGER_DOWN)
                touch.FingerDown(*input, id, x, y);
            else if (e.type == SDL_EVENT_FINGER_MOTION)
                touch.FingerMove(*input, id, x, y, e.tfinger.dx * w, e.tfinger.dy * h);
            else if (e.type == SDL_EVENT_FINGER_UP)
                touch.FingerUp(*input, id, x, y);
            else
                touch.Cancel(*input);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (input)
            if (int b = ButtonIndex(e.button.button); b >= 0)
                input->OnMouseUp(b);
        break;
    default:
        break;
    }
}

void AppPlatform_SDL::PumpFrame() {
    SDL_Event e;
    while (SDL_PollEvent(&e))
        HandleEvent(e);

    {
        std::lock_guard lock(titleMutex);
        if (titleDirty) {
            SDL_SetWindowTitle(window, pendingTitle.c_str());
            titleDirty = false;
        }
    }

    if (game && touchMode)
        touch.Update(game->GetInput());

    if (game) {
        const bool text = game->GetInput().TextMode();
        if (text != textActive) {
            textActive = text;
            if (text) {
                recaptureAfterText = cursorCaptured;
                SetCursorCaptured(false);
                SDL_StartTextInput(window);
            } else {
                SDL_StopTextInput(window);
                if (recaptureAfterText)
                    SetCursorCaptured(true);
                recaptureAfterText = false;
            }
        }
    }

    if (cursorCaptured && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
        SetCursorCaptured(false);
}

void AppPlatform_SDL::Run() {
    while (isRunning) {
        PumpFrame();
        bgfx::renderFrame(8);
    }
}

void AppPlatform_SDL::Shutdown() {
    if (game) {
        std::atomic<bool> done{false};
        std::thread stopper([&] {
            game->Shutdown();
            done = true;
        });
        while (!done)
            bgfx::renderFrame(8);
        stopper.join();
        bgfx::renderFrame();
        game.reset();
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}
