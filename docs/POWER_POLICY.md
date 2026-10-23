# HomePanel power policy — v0.2.7

| State | CPU | Network |
|---|---|---|
| UI/touch activity | 40..360/400 MHz burst | current route |
| normal low load | 40..90/100 MHz | LAN preferred |
| dimmed | 40..90/100 MHz | LAN preferred |
| screensaver | 40 MHz until work | WLAN modem sleep if used |
| display off | 40 MHz until work | WLAN modem sleep if used |
| heavy task | 360/400 MHz guarded section | unchanged |
| LAN stable | adaptive CPU | Wi-Fi OFF |
| LAN lost | adaptive burst | Wi-Fi fallback starts |

Eco remains enabled by default. Full performance is available automatically when
needed, without leaving Eco mode.
