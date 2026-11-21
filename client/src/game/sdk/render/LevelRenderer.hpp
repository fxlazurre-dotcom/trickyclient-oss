// ModCore — game SDK: frame-transform capture (Solstice pipeline port).
// Hook ScreenView::setupAndRender — inside it the game state matches the
// frame being BUILT: read the view matrix (ci+0x420), walk
// ci->vt[187]()->+0x468 (LevelRendererPlayer) for the camera position
// (+0x9CC) and the fov pair (+0xF58/+0xF6C). A short DELAY QUEUE (depth 3 =
// swapchain buffer count) aligns the transform with the frame actually being
// PRESENTED — kills the one-frame lag wobble.
#pragma once
#include "utils/RenderUtil.hpp"

namespace mc {
namespace sdk {

// hook install (called from game::install_all)
void install_render_hook();

// diagnostics: setupAndRender call counter (FPS hunt)
long render_sar_calls();
// the transform matching the currently presented frame (render thread)
render::ViewTransform render_frame_transform();

} // namespace sdk
} // namespace mc
