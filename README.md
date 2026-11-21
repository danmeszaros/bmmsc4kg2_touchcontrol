# bmmsc4kg2_touchcontrol

Handheld/rig controller for the Blackmagic Micro Studio Camera 4K G2 on a
Waveshare RP2040 Touch LCD 1.28 (round 240×240 GC9A01 LCD, CST816S touch).

The board is the camera's USB network device: USB-C to the camera, DHCP
server on the board, the camera's REST API over HTTP. How the camera behaves
was measured with [bmmsc4kg2_explorer](https://github.com/danmeszaros/bmmsc4kg2_explorer);
the result is `docs/controller-guide.md`.

## Status

- [x] USB network + DHCP + HTTP (from the explorer, verified on the camera)
- [x] Camera link: polling of record state, iris, WB, tint, gain, fps, remaining time; on connect auto exposure off + shutter 180°
- [x] REC toggle and momentary AF on physical buttons
- [x] Main screen (variant B from `docs/ui.md`), redrawn on change
- [ ] Touch (CST816S) and edit screens: iris, WB + auto WB, base gain
- [ ] Audio page, frame-rate page, swiping between pages
- [ ] Not yet run on hardware

## Wiring

| Board | |
|---|---|
| USB-C | camera |
| GP16 | REC button to GND |
| GP17 | AF button to GND |
| GP18, GP26 | spare buttons to GND |
| GP27 | debug log TX (921600 8N1) → RX of a 3.3 V USB-UART converter, plus GND |

The buttons use the internal pull-ups, no other parts needed.

## Build

```
PICO_SDK_PATH=/path/to/pico-sdk cmake -S . -B build && cmake --build build -j8
# flash build/touchcontrol_fw.uf2 (BOOT + USB-C to the PC)
test/run.sh            # host tests; renders screens to test/out/*.png
python3 tools/mkfont.py   # regenerate src/gfx/fonts_gen.* after changing sizes
python3 tools/render_mockups.py docs/mockups-new   # mockup PNGs
```

## Layout

```
src/main.cc        main loop: USB network, camera link, buttons, redraw
src/camera/        Camera (REST client, polling, commands), CamState, tiny JSON lookup
src/ui/            screens drawn from CamState, value formatting
src/gfx/           240×240 RGB565 canvas, 4-bit antialiased bitmap fonts (DejaVu Sans)
src/hw/            GC9A01 LCD (SPI1 + DMA), buttons, PIO UART log
src/net/           USB RNDIS/ECM + lwIP + DHCP server + HTTP client (from the explorer)
docs/              ui.md, mockups, controller-guide.md
```
