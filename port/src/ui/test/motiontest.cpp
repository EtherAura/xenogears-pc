// Perspective input must invert the visible geometry; scissored text must
// stay inside a tilted pane. Run under ASan/UBSan as well as the host build.
#include "ui/motion.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static int checks;
static void require(bool good) { ++checks; if (!good) { std::fprintf(stderr, "motion check %d failed\n", checks); std::abort(); } }
int main() {
    ImGui::CreateContext();
    for (float unit : {1.0f, 1.25f, 2.0f, 7.0f}) {
        ImVec2 center(240*unit, 160*unit);
        for (unsigned frame = 0; frame <= 40; frame++) {
            auto v = menuMotion::opening(center, unit, frame);
            for (float x : {-200.0f, 0.0f, 200.0f}) for (float y : {-140.0f, 0.0f, 140.0f}) {
                ImVec2 p(center.x + x*unit, center.y + y*unit), q;
                require(v.inverse(v.project(p), &q));
                require(std::fabs(q.x-p.x) < .02f && std::fabs(q.y-p.y) < .02f);
            }
            ImDrawList dl(ImGui::GetDrawListSharedData());
            dl._ResetForNewFrame(); dl.Flags = ImDrawListFlags_None;
            dl.PushClipRect(ImVec2(center.x-80*unit,center.y-60*unit), ImVec2(center.x+80*unit,center.y+60*unit));
            dl.AddRectFilled(ImVec2(center.x-200*unit,center.y-140*unit),ImVec2(center.x+200*unit,center.y+140*unit),IM_COL32_WHITE);
            menuMotion::drawList(&dl,v);
            require(dl.IdxBuffer.Size > 0);
            for (const auto& p : dl.VtxBuffer) {
                ImVec2 q; require(v.inverse(p.pos,&q));
                require(q.x >= center.x-80*unit-.02f && q.x <= center.x+80*unit+.02f &&
                        q.y >= center.y-60*unit-.02f && q.y <= center.y+60*unit+.02f);
            }
        }
        auto v = menuMotion::expanding(center, ImVec2(300*unit,200*unit),unit,12);
        ImVec2 p(10*unit,20*unit), q; require(v.inverse(v.project(p),&q));
        require(std::fabs(q.x-p.x)<.02f && std::fabs(q.y-p.y)<.02f);
    }
    // updateMenuTransforms: frame 0 of a sub-menu's coming in is at z 0x800,
    // not turned; frame 32 at z 0x200 turned 0xF80; frame 33 at rest. The
    // going shows frames 0 (at rest) to 21 (ry -0x7E0, z 0x740).
    auto first = menuMotion::opening(ImVec2(0,0), 1, 0);
    require(first.depth == 2048 && first.sine == 0 && first.cosine == 1);
    auto last = menuMotion::opening(ImVec2(0,0), 1, 32);
    require(last.depth == 512 && std::fabs(last.sine - std::sin(0xF80 * 6.28318530718f / 4096)) < 1e-5f);
    auto rest = menuMotion::opening(ImVec2(0,0), 1, menuMotion::OPENING_FRAMES);
    require(menuMotion::OPENING_FRAMES == 33 && rest.depth == 512 && rest.sine == 0);
    require(menuMotion::leaving(ImVec2(0,0), 1, 0).depth == 512 && menuMotion::leaving(ImVec2(0,0), 1, 0).sine == 0);
    auto exit = menuMotion::leaving(ImVec2(0,0), 1, menuMotion::LEAVING_FRAMES - 1);
    require(menuMotion::LEAVING_FRAMES == 22 && exit.depth == 0x740);
    for (unsigned frame = 0; frame <= menuMotion::LEAVING_FRAMES; frame++) {
        ImDrawList dl(ImGui::GetDrawListSharedData()); dl._ResetForNewFrame();
        dl.PushClipRect(ImVec2(-200,-140), ImVec2(200,140));
        dl.AddRectFilled(ImVec2(-200,-140), ImVec2(200,140), IM_COL32_WHITE);
        menuMotion::drawList(&dl,menuMotion::leaving(ImVec2(0,0),1,frame),true);
        for (const auto& v : dl.VtxBuffer) require(std::isfinite(v.pos.x) && std::isfinite(v.pos.y));
    }
    // A large command after clipping can exceed 16-bit indices: preserve
    // texture/callback order and split it into commands with vertex offsets.
    {
    ImDrawList dl(ImGui::GetDrawListSharedData()); dl._ResetForNewFrame();
    dl.Flags = ImDrawListFlags_AllowVtxOffset;
    dl.PushClipRect(ImVec2(0,0),ImVec2(100,100));
    for (int i=0;i<20000;i++) dl.AddRectFilled(ImVec2(1,1),ImVec2(99,99),IM_COL32_WHITE);
    menuMotion::drawList(&dl,menuMotion::opening(ImVec2(50,50),1,40));
    for (const auto& cmd : dl.CmdBuffer) for (unsigned i=0;i<cmd.ElemCount;i++)
        require(cmd.VtxOffset + dl.IdxBuffer[cmd.IdxOffset+i] < unsigned(dl.VtxBuffer.Size));
    }
    ImGui::DestroyContext();
    std::printf("menu motion: %d checks passed\n",checks);
}
