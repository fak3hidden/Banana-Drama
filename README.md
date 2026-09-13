# Banana Drama

A small Visual Studio 2022 project that builds a **dll** you inject into a game
(Banana Drama) and a **console injector** that does the injecting. Once loaded
the dll hooks the game's Direct3D 11 swap chain and draws a Dear ImGui menu on
top of it:

- tabbed menu (**Modules / Settings / Debug**)
- module system - every feature is a class with its own options
- settings saved to `%APPDATA%\BananaDrama\config.json` (autosaved ~0.5 s after
  a change, so a crash never loses them)
- rebindable keys, accent colour, UI scale, opacity
- a debug console plus a log file, and a Debug tab that tells you which
  graphics API the game is actually using
- `End` unloads the dll cleanly from inside the game

Everything it ships with is local-only: the two built-in modules read no game
memory. The Overlay HUD shows fps/clock, and the Sandbox module is a template
with one of every ImGui widget.

## Requirements

- Visual Studio 2022, workload **Desktop development with C++**
- The Windows 10 or 11 SDK (any recent version)
- Windows 10/11, x64

## Getting the code

The dependencies are git submodules, so clone with `--recurse-submodules`:

```bat
git clone --recurse-submodules https://github.com/fak3hidden/Banana-Drama.git
```

Already cloned it? Run:

```bat
git submodule update --init --recursive
```

| Submodule | Version | What for |
| --- | --- | --- |
| `vendor/imgui` | v1.92.9b | the UI (core + `win32` and `dx11` backends) |
| `vendor/minhook` | v1.3.4 | function hooking, ready for game hooks |

## Building

Open `BananaDrama.sln`, pick **Release | x64**, and build (Ctrl+Shift+B).
Output lands in `build\x64\Release\`:

```
build\x64\Release\BananaDrama.dll            <- the mod
build\x64\Release\BananaDrama.Injector.exe   <- the injector
```

Building **Release | Win32** gives `BananaDrama32.dll` for a 32-bit game.

## Using it

```bat
cd build\x64\Release
BananaDrama.Injector.exe BananaDrama.exe
```

```
BananaDrama.Injector.exe                        list running processes
BananaDrama.Injector.exe BananaDrama.exe        inject (dll next to the exe)
BananaDrama.Injector.exe 12345 C:\mods\BananaDrama.dll
BananaDrama.Injector.exe BananaDrama.exe --wait wait for the game to start
```

Then in game:

| Key | Action |
| --- | --- |
| `Insert` | open / close the menu |
| `End` | unload the dll |
| `Escape` | cancel a key rebind |

If the game was started as administrator, run the injector from an
administrator terminal too.

## Layout

```
src/core/
  dllmain.cpp        entry point: starts the worker thread, owns the main loop
  app.h/.cpp         startup, config load/save autosave, unload request
  hooks.cpp          swap chain Present + ResizeBuffers hooks, WndProc hook
  renderer.cpp       D3D11 device, ImGui setup, style and fonts, render target
  menu.cpp           the menu itself: tabs, watermark, keyboard blocking
  config.cpp         settings <-> config.json
  settings.h         the global Settings struct
  hotkey.cpp         key state polling and key names
  modules/           Module base class, registry, and the built-in modules
  ui/widgets.cpp     reusable ImGui helpers (toggle, key binder, key/value)
  util/              logging, paths, environment detection, tiny json module
src/injector/        the injector console app
tests/json_tests.cpp unit tests for the json module (not part of the solution)
```

## Adding a module

1. Copy `src/core/modules/sandbox.h/.cpp` and rename the class.
2. Give it a stable id in the constructor - that id is its config key.
3. Do the work in `OnFrame()`, draw in `OnDrawOverlay()`, expose knobs in
   `OnMenu()`, persist them in `OnSave()` / `OnLoad()`.
4. Register it in `RegisterBuiltinModules()` in `module_manager.cpp`.

The menu lists it, the config saves it, and it costs you nothing else.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Menu never appears | Debug tab shows the renderer the game uses. This build hooks Direct3D 11 only - if it says Vulkan/D3D12/OpenGL the game needs a different hook. |
| `Attached: no` | The game has not called `Present` yet. Wait, or alt-tab out and back. |
| Injector says "not found" | Make sure you built first; the dll path is relative to the injector. |
| 32-bit / 64-bit mismatch | Build the matching configuration; Task Manager > Details shows the bitness. |
| Game input is dead | The menu blocks input while it is open (Settings > Input can turn that off). |

Logs go to `%APPDATA%\BananaDrama\banana-drama.log`, and to the console window
if you leave it enabled. Config: `%APPDATA%\BananaDrama\config.json`.

## Scope

Built for **single-player / offline** use on your own machine, and it does not
touch or bypass any anti-cheat. Banana Drama has online PvP and co-op - keep it
out of those modes.
