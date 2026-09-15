#pragma once

namespace bd {

// Where the on-screen HUD window is parked.
enum class OverlayCorner {
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3,
};

inline const char* OverlayCornerName(OverlayCorner corner)
{
    switch (corner) {
    case OverlayCorner::TopLeft:     return "Top left";
    case OverlayCorner::TopRight:    return "Top right";
    case OverlayCorner::BottomLeft:  return "Bottom left";
    case OverlayCorner::BottomRight: return "Bottom right";
    }
    return "Top right";
}

// Global settings: the things that are not owned by a single module.
// Add a field here, then teach config.cpp how to read and write it.
struct Settings {
    // --- input -----------------------------------------------------------
    int  menuKey = 0x2D;   // VK_INSERT
    int  unloadKey = 0x23; // VK_END
    bool blockGameInputWhileOpen = true;

    // --- menu ------------------------------------------------------------
    bool  menuOpenOnInject = true;
    bool  showWatermark = true;
    float uiScale = 1.0f;   // 0.75 .. 2.0
    float uiAlpha = 0.97f;  // 0.4 .. 1.0
    float accent[4] = { 0.99f, 0.79f, 0.15f, 1.0f }; // banana yellow

    // --- diagnostics -----------------------------------------------------
    bool consoleEnabled = true;
    bool logToFile = true;
    bool showDemoWindow = false;
};

} // namespace bd
