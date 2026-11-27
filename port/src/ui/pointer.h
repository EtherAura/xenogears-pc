#ifndef PORT_UI_POINTER_H
#define PORT_UI_POINTER_H

#include <SDL3/SDL.h>
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"

namespace uiPointer {

constexpr SDL_MouseID scriptId = static_cast<SDL_MouseID>(-3);

inline bool emulated(SDL_MouseID id) {
    return id == SDL_TOUCH_MOUSEID || id == scriptId;
}

inline void discardQueuedMouse(ImVector<ImGuiInputEvent>& queue, int first) {
    for (int i = queue.Size - 1; i >= first; --i) {
        if (queue[i].Source == ImGuiInputSource_Mouse) queue.erase(&queue[i]);
    }
}

inline void newFrame(bool mouseEnabled, bool cursorVisible) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (cursorVisible) {
        g.IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    } else {
        g.IO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    int first = g.InputEventsQueue.Size;
    ImGui_ImplSDL3_NewFrame();
    // SDL's backend can also poll the desktop pointer during NewFrame.
    // Keep the touch and script events already queued by the event pump.
    if (!mouseEnabled) discardQueuedMouse(g.InputEventsQueue, first);
    if (cursorVisible && !SDL_CursorVisible()) SDL_ShowCursor();
    if (!cursorVisible && SDL_CursorVisible()) SDL_HideCursor();
}

} // namespace uiPointer

#endif
