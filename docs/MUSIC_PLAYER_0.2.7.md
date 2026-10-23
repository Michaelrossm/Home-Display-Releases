# Home-Display music player — v0.2.7

## Architecture

Music is a first-class app but audio hardware and playback state are shared system services.

`MusicApp -> MusicPlayerService -> AudioHardwareService -> Waveshare ES8311/codec`

The app also publishes the shared `MediaSessionService`, therefore the system
screensaver can control:
- previous
- play/pause
- next
- volume
- mute/power state

## Open-source decoder

The project switches to PlatformIO hybrid `arduino, espidf` mode and declares:

- `chmorgan/esp-audio-player 1.1.0`
- `chmorgan/esp-libhelix-mp3 1.0.3`

Both selected versions are Apache-2.0.

This gives a lightweight MP3/WAV decoder foundation without pulling in the complete ESP-ADF stack.

## Music storage

Default scan locations:
- `/homepanel/media/music`
- `/Music`
- `/music`

Maximum indexed tracks: 256.
Only compact metadata is kept in RAM.

## Hardware status

Waveshare documents an onboard microphone, codec/amplifier and 8-ohm/2-W speaker output.
Its official P4 I2S example uses ES8311 / `esp_codec_dev`.

The exact D-board codec/I2S BSP is intentionally isolated in `AudioHardwareService`.
It is not marked `ready()` until the exact board revision has been imported/tested.
