// A viewer's picture (view.cpp): PS1 primitives (vgpu.h XgPrim, from the
// game's own primitive builders through a sandbox's GPU, or a viewer's own)
// drawn by our Vulkan device into an image of the view's own, at the size the
// window gives it, with zoom and pan, and shown in an ImGui window. Never
// into the game's VRAM or its picture.
//
// The picture is drawn as the PS1 GPU would (view.frag): texture pages and
// CLUTs from the view's VRAM (its own copy: a sandbox's GPU's, or any 1024 x
// 512 halfwords), the texture window, texel 0 clear, modulation, the four
// semi-transparency modes for STP texels, each primitive kept in its draw
// area; but at any scale, in 8-bit colour, without dithering or mask bits.
//
//   std::unique_ptr<extras::View> view(new extras::View);   // in opened()
//   view->show(xgSandboxGpu(sb));         // its primitives and VRAM
//   view->setScene(0, 0, 320, 240);       // what Fit shows
//   view->draw();                         // in draw(): the rest of the window
//   view.reset();                         // in closed()
//
// Mouse: the wheel zooms at the cursor, a drag pans, a double click fits.
// Gamepad: the right stick pans, the triggers zoom; keyboard (view hovered or
// focused): +/- zoom, 0 fits.

#ifndef PORT_EXTRAS_VIEW_H
#define PORT_EXTRAS_VIEW_H

#include <stdint.h>
#include <vector>

#include "imgui.h"
#include "vgpu.h"

namespace extras {

class View {
public:
    View();
    ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    // What it draws: primitives (copied) and the VRAM they read (uploaded
    // again when `version` changes; nullptr: none, textures off).
    void setPrims(const XgPrim* prims, int count);
    void setVram(const uint16_t* vram, unsigned version);
    void show(XgVgpu* gpu); // both from a GPU
    // A cached RGBA picture up to 1024 x 2048, independent of PS1 VRAM.
    // draw(size) places it at that size; callers preserve its aspect ratio.
    void setPixels(const std::vector<uint32_t>& rgba, int w, int h);
    // CPU preview of the current viewport using the export rasterizer.
    // Bounded to 160 x 120; call after this frame's viewer draw.
    bool thumbnail(std::vector<uint32_t>& rgba) const;

    // The scene rectangle (VRAM or screen coordinates) Fit shows; without
    // one, the primitives' bounds.
    void setScene(float x0, float y0, float x1, float y1);
    void setBackground(ImU32 colour);
    void setTextures(bool on);   // off: textured primitives in their vertex colours
    void setWireframe(bool on);  // the polygons' edges over them
    void setKeepInArea(bool on); // keep each primitive inside its draw area (default on)
    void setFitOnChange(bool on);// fit when the scene rectangle changes (default on)
    void setWholeFit(bool on);   // Fit in whole window pixels per scene unit from 2 up (default on;
                                 // off for a picture rendered below the window's size: a 3D view)
    void setInteractive(bool on); // off: no zoom or pan of its own (a viewer that moves a camera
                                  // reads the mouse over it itself); hovered() and the item stay
    // The Enhanced look (replace.h): the installed packs' pictures drawn in place of the native
    // texels of what they cover, from the libraries asked for (XG_REPLACE_*; 0, the default:
    // the stock picture, no look-ups). poseParts: the primitives are exactly one sprite
    // actor's that many parts (the sprite viewer), for its pose (Enhanced engine).
    void setReplacements(unsigned libraries, int poseParts = 0);
    int replaced() const;  // primitives the last draw drew replaced
    int replacing() const; // look-ups still loading at the last draw (the picture changes when they land)

    // In the current ImGui window at the cursor: `size` window pixels (0:
    // the rest of the window's content region).
    void draw(ImVec2 size = ImVec2(0, 0));
    void fit();
    void setZoom(float pixelsPerUnit); // about the view's centre
    float zoom() const;                // window pixels per scene unit
    bool hovered() const;
    ImVec2 toScene(ImVec2 screen) const; // a window position in scene units

    struct Impl;

private:
    Impl* p;
};

// The shell's calls (shell.cpp): a frame's offscreen drawing begins before
// the viewers draw and is handed to the queue after; the device comes and
// goes with the extras' Vulkan backend.
void viewsBeginFrame();
void viewsEndFrame();
void viewsShutdown();

} // namespace extras

#endif
