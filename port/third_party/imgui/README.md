# Dear ImGui (vendored)

The in-game settings menu (port/src/ui) is drawn with Dear ImGui.

- Source: https://github.com/ocornut/imgui, tag `v1.92.9b-docking`
  (commit b48d1afbe8ee8b238e2961dc363a949dd7304e23), copied from the local
  checkout in tools/toolchain-src/pcsx-redux/third_party/imgui.
- Licence: MIT, LICENSE.txt.
- Files: the core (imgui*.cpp/h, imconfig.h, imstb_*.h) and two backends,
  backends/imgui_impl_sdl3 and backends/imgui_impl_vulkan. Nothing else
  (no demo, no examples, no misc/).
- Unmodified. Configuration is by compile definitions in port/CMakeLists.txt
  (IMGUI_IMPL_VULKAN_NO_PROTOTYPES: the Vulkan functions come from SDL's
  loader, as in port/src/gpu/vk.c).

To update: copy the same files from a newer tag and change the tag above.
