# MiniBrowser and saved Web Apps — v0.2.7

## Goal

A user can save a local device page similarly to "Add to Home Screen":

1. Open `Web-Apps`.
2. Enter name, e.g. `IR Tracker`.
3. Enter URL, e.g. `http://192.168.178.50/`.
4. Press `Zum Home-Bildschirm`.
5. A new globe tile appears on the Home-Display desktop.

Up to 16 saved web apps are stored in compact binary records.

## Security defaults

- local HTTP allowed
- HTTPS allowed
- WAN/public hosts denied by default
- private IPv4, `.local`, localhost and simple LAN hostnames allowed
- page response capped to 128 KiB by default

This avoids accidentally turning the wall panel into a general internet browser.

## Important browser limit

Home-Display does not contain Chromium/WebKit or a JavaScript engine.

The MiniBrowser:
- downloads HTML
- reads the page title
- strips script/style blocks
- converts useful HTML text into a touch-friendly LVGL view

It is useful for simple local status/device pages.

For JavaScript-heavy pages, the preferred professional solution is a **native adapter**
that keeps the saved URL/account identity but renders the device's JSON/API data in LVGL.
The IR Tracker is an ideal candidate because Home-Display already has a native Power/IR tracker app.

This avoids embedding an unstable experimental browser engine.
