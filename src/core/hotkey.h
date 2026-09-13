#pragma once

namespace bd::hotkey {

// Snapshots the keyboard. Call once per frame, before anything asks about keys.
void Update();

// True while the key is held down.
bool Down(int vKey);

// True exactly once, on the frame the key goes down.
bool Pressed(int vKey);

// Fills `outVKey` with the first key that went down this frame, Escape excluded
// so it can be used to cancel. Returns false while nothing is being pressed.
bool AnyPressed(int* outVKey);

// "Insert", "F5", "Mouse 1", ... Never returns null.
const char* Name(int vKey);

} // namespace bd::hotkey
