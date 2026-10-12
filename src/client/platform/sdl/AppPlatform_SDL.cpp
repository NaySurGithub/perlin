#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "client/renderer/bgfx/RenderContext_bgfx.hpp"
#include "client/input/Keys.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>

#include <atomic>
#include <iostream>
#include <thread>

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
            return K::Escape;
        case SDL_SCANCODE_RETURN:
            return K::Enter;
        case SDL_SCANCODE_TAB:
            return K::Tab;
        case SDL_SCANCODE_BACKSPACE:
            return K::Backspace;
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

bool AppPlatform_SDL::ConfirmDialog(const std::string& title, const std::string& message) {
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
}

bool AppPlatform_SDL::Initialize() {
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
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window) {
        std::cout << "[sdl] window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_PropertiesID wp = SDL_GetWindowProperties(window);
    const char* driver = SDL_GetCurrentVideoDriver();
    std::string drv = driver ? driver : "";
#if defined(SDL_PLATFORM_WIN32)
    native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS)
    native.window = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
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

    bgfx::renderFrame();

    game = std::make_unique<perlin::Perlin>(args);
    if (!game->Initialize(this))
        return false;
    SDL_RaiseWindow(window);
    SetCursorCaptured(true);
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
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (e.window.data1 > 0 && e.window.data2 > 0) {
            framebufferWidth = e.window.data1;
            framebufferHeight = e.window.data2;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (input)
            input->ReleaseAll();
        SetCursorCaptured(false);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e.key.scancode == SDL_SCANCODE_ESCAPE && cursorCaptured)
            SetCursorCaptured(false);
        if (input && !e.key.repeat)
            if (int k = KeyFromScancode(e.key.scancode))
                input->OnKeyDown(k);
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
            input->OnMouseMove(e.motion.x, e.motion.y);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!cursorCaptured) {
            // The click that grabs the cursor is not forwarded as a game click.
            SetCursorCaptured(true);
            break;
        }
        if (input)
            if (int b = ButtonIndex(e.button.button); b >= 0)
                input->OnMouseDown(b);
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

void AppPlatform_SDL::Run() {
    while (isRunning) {
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
