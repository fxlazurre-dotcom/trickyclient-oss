// ModCore — utils layer: world-to-screen (engine frame-transform version).
// Ported from the reference client (Solstice, same 1.26.x family): the game
// exposes the frame's VIEW MATRIX (ClientInstance+0x420), the camera position
// (LevelRendererPlayer+0x9CC) and the fov pair (LRP+0xF58/+0xF6C) — with
// those the projection is EXACT, no yaw/pitch math or calibration needed.
#pragma once

namespace mc {
namespace render {

// captured per frame by the game/sdk render hook (column-major, glm layout)
struct ViewTransform {
    float m[16];        // view matrix, column-major: m[col*4 + row]
    float origin[3];    // camera position (world space)
    float fov[2];       // fov.x / fov.y (aspect-corrected pair)
    bool  valid = false;
};

// exact port of the reference OWorldToScreen: pos is rebased by the camera
// origin, dotted against matrix rows; z > 0 means BEHIND the camera.
inline bool worldToScreen(const ViewTransform& t, float wx, float wy, float wz,
                          float W, float H, float& sx, float& sy) {
    if (!t.valid) return false;
    float px = wx - t.origin[0];
    float py = wy - t.origin[1];
    float pz = wz - t.origin[2];
    float x = t.m[0] * px + t.m[1] * py + t.m[2]  * pz + t.m[3];
    float y = t.m[4] * px + t.m[5] * py + t.m[6]  * pz + t.m[7];
    float z = t.m[8] * px + t.m[9] * py + t.m[10] * pz + t.m[11];
    if (z > 0.f) return false;                       // behind the camera
    float mX = W * 0.5f, mY = H * 0.5f;
    sx = mX + (mX * x / -z * t.fov[0]);
    sy = mY - (mY * y / -z * t.fov[1]);
    if (sx > W * 2.f || sy > H * 2.f || sx < -W || sy < -H) return false;  // way offscreen
    return true;
}

} // namespace render
} // namespace mc
