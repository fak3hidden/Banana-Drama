#pragma once

namespace bd::ui {

// "(?)" with a tooltip.
void HelpMarker(const char* description);

// Checkbox with an optional help marker on the same line.
bool Toggle(const char* label, bool* value, const char* help = nullptr);

// "Label" on the left, dimmed value pushed against the right edge.
void KeyValue(const char* label, const char* value);

// A titled divider between groups of rows.
void Section(const char* label);

// Shows the current binding; click it, then press a key to rebind.
// Returns true on the frame the binding changed.
bool KeyBind(const char* label, int* vKey);

} // namespace bd::ui
