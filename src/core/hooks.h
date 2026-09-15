#pragma once

namespace bd::hooks {

// Finds the swap chain vtable (via a throwaway device) and patches
// Present + ResizeBuffers. MinHook is started here too, ready for game hooks.
bool Initialize();

// Puts the vtable and the window procedure back the way they were.
void Shutdown();

// Asks the render thread to unload. Blocks until it has finished tearing down.
// Asks the render thread to detach.
void RequestUnload();

// Blocks until the render thread has finished tearing down.
bool WaitForUnload(unsigned long timeoutMs);

bool IsAttached();

} // namespace bd::hooks
