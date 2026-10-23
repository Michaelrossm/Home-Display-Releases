# Native calculator

The firmware registers one native `calculator` app before discovering Lua
packages. Existing desktop/dock IDs continue to resolve to this app. An old
Lua package with the same ID is not registered or executed; its stored files
are not deleted. Native apps remain protected against package replacement.

The page is created under the existing AppManager UI-lock path. A single
LVGL grid implements the original 24-key arrangement without Lua, timer, network job or
additional task. The opaque root is destroyed on close. The existing vector
icon renderer draws a calculator with `1+1` on its screen for desktop and dock.

Operations: basic arithmetic, repeated equals, sign, decimal comma, percent,
square root, square, reciprocal, delete and clear. Advanced operations use
German labels instead of programming names; memory abbreviations are not
shown on the page. Colors come from UiTheme, including number/function and
accent-colored operator keys. Normal theme recoloring updates these objects
without a calculator-specific timer or theme engine.
Input is bounded to 14 digits. The C++ model uses 64-bit double, displayed
with 14 significant digits. It is not an arbitrary-precision financial engine.
Invalid/nonfinite results display `Fehler`; the next input recovers.

Host tests exercise the actual model and native integration. Build/test
success does not constitute touchscreen or physical-display validation.
