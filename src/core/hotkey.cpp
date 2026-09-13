#include "hotkey.h"

#include "core.h"

#include <cstdio>
#include <cstring>

namespace bd::hotkey {
namespace {

constexpr int kKeyCount = 256;

bool g_current[kKeyCount]{};
bool g_previous[kKeyCount]{};
bool g_primed = false;

bool InRange(int vKey)
{
    return vKey >= 0 && vKey < kKeyCount;
}

} // namespace

void Update()
{
    std::memcpy(g_previous, g_current, sizeof(g_current));
    for (int key = 0; key < kKeyCount; ++key)
        g_current[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
    g_primed = true;
}

bool Down(int vKey)
{
    return g_primed && InRange(vKey) && g_current[vKey];
}

bool Pressed(int vKey)
{
    return g_primed && InRange(vKey) && g_current[vKey] && !g_previous[vKey];
}

bool AnyPressed(int* outVKey)
{
    if (!g_primed)
        return false;

    for (int key = 0x01; key < kKeyCount; ++key) {
        if (key == VK_ESCAPE) // reserved for cancelling a rebind
            continue;
        if (g_current[key] && !g_previous[key]) {
            if (outVKey)
                *outVKey = key;
            return true;
        }
    }
    return false;
}

const char* Name(int vKey)
{
    // Rotating buffers: the result is only used until the next call.
    static thread_local char buffers[8][24];
    static thread_local int slot = 0;
    char* out = buffers[slot];
    slot = (slot + 1) % 8;

    const char* named = nullptr;
    switch (vKey) {
    case 0:                 named = "<none>";       break;
    case VK_LBUTTON:        named = "Mouse 1";      break;
    case VK_RBUTTON:        named = "Mouse 2";      break;
    case VK_CANCEL:         named = "Mouse 3";      break;
    case VK_MBUTTON:        named = "Mouse 4";      break;
    case VK_XBUTTON1:       named = "Mouse 5";      break;
    case VK_XBUTTON2:       named = "Mouse 6";      break;
    case VK_BACK:           named = "Backspace";    break;
    case VK_TAB:            named = "Tab";          break;
    case VK_RETURN:         named = "Enter";        break;
    case VK_SHIFT:          named = "Shift";        break;
    case VK_CONTROL:        named = "Ctrl";         break;
    case VK_MENU:           named = "Alt";          break;
    case VK_PAUSE:          named = "Pause";        break;
    case VK_CAPITAL:        named = "Caps Lock";    break;
    case VK_ESCAPE:         named = "Escape";       break;
    case VK_SPACE:          named = "Space";        break;
    case VK_PRIOR:          named = "Page Up";      break;
    case VK_NEXT:           named = "Page Down";    break;
    case VK_END:            named = "End";          break;
    case VK_HOME:           named = "Home";         break;
    case VK_LEFT:           named = "Left";         break;
    case VK_UP:             named = "Up";           break;
    case VK_RIGHT:          named = "Right";        break;
    case VK_DOWN:           named = "Down";         break;
    case VK_SNAPSHOT:       named = "Print Screen"; break;
    case VK_INSERT:         named = "Insert";       break;
    case VK_DELETE:         named = "Delete";       break;
    case VK_LWIN:           named = "Left Win";     break;
    case VK_RWIN:           named = "Right Win";    break;
    case VK_NUMPAD0:        named = "Num 0";        break;
    case VK_NUMPAD1:        named = "Num 1";        break;
    case VK_NUMPAD2:        named = "Num 2";        break;
    case VK_NUMPAD3:        named = "Num 3";        break;
    case VK_NUMPAD4:        named = "Num 4";        break;
    case VK_NUMPAD5:        named = "Num 5";        break;
    case VK_NUMPAD6:        named = "Num 6";        break;
    case VK_NUMPAD7:        named = "Num 7";        break;
    case VK_NUMPAD8:        named = "Num 8";        break;
    case VK_NUMPAD9:        named = "Num 9";        break;
    case VK_MULTIPLY:       named = "Num *";        break;
    case VK_ADD:            named = "Num +";        break;
    case VK_SUBTRACT:       named = "Num -";        break;
    case VK_DECIMAL:        named = "Num .";        break;
    case VK_DIVIDE:         named = "Num /";        break;
    case VK_NUMLOCK:        named = "Num Lock";     break;
    case VK_SCROLL:         named = "Scroll Lock";  break;
    case VK_LSHIFT:         named = "Left Shift";   break;
    case VK_RSHIFT:         named = "Right Shift";  break;
    case VK_LCONTROL:       named = "Left Ctrl";    break;
    case VK_RCONTROL:       named = "Right Ctrl";   break;
    case VK_LMENU:          named = "Left Alt";     break;
    case VK_RMENU:          named = "Right Alt";    break;
    case VK_OEM_1:          named = ";";            break;
    case VK_OEM_2:          named = "/";            break;
    case VK_OEM_3:          named = "~";            break;
    case VK_OEM_4:          named = "[";            break;
    case VK_OEM_5:          named = "\\";           break;
    case VK_OEM_6:          named = "]";            break;
    case VK_OEM_7:          named = "'";            break;
    case VK_OEM_COMMA:      named = ",";            break;
    case VK_OEM_PERIOD:     named = ".";            break;
    case VK_OEM_MINUS:      named = "-";            break;
    case VK_OEM_PLUS:       named = "=";            break;
    default: break;
    }

    if (named) {
        std::snprintf(out, 24, "%s", named);
        return out;
    }

    if (vKey >= VK_F1 && vKey <= VK_F24) {
        std::snprintf(out, 24, "F%d", vKey - VK_F1 + 1);
        return out;
    }
    if (vKey >= 'A' && vKey <= 'Z') {
        std::snprintf(out, 24, "%c", static_cast<char>(vKey));
        return out;
    }
    if (vKey >= '0' && vKey <= '9') {
        std::snprintf(out, 24, "%c", static_cast<char>(vKey));
        return out;
    }

    std::snprintf(out, 24, "Key %d", vKey);
    return out;
}

} // namespace bd::hotkey
