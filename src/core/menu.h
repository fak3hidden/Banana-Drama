#pragma once

namespace bd::menu {

void Open();
void Close();
void Toggle();
bool IsOpen();

// Draws the menu. Call between renderer::NewFrame() and renderer::Render().
void Render();

// True when the game should not see mouse and keyboard input.
bool ShouldBlockGameInput();

} // namespace bd::menu
