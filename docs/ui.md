# UI

Round 240×240 display, about 32 mm across (1 px ≈ 0.135 mm). Tap targets at
least ~68 px (9 mm). Mockups: `mockups/` (rendered by `tools/render_mockups.py`).

## Pages (swipe left/right)

1. **MAIN** (`mockups/1B_main_tiles.png`, recording: `1B_main_tiles_rec.png`)
   - Status line: STBY / ● REC, remaining record time. While recording: red ring at the edge.
   - Timecode is not shown (the operator looks into the viewfinder while recording).
   - Tiles: IRIS (large, top), WB | GAIN (below). Tap opens the edit screen.
2. **AUDIO** (`mockups/P2_audio.png`)
   - Per channel (CH1 = API channel 0, CH2 = channel 1): tap the source to cycle
     CAM L → CAM R → JACK L → JACK R (`Camera - Left/Right`, `3.5mm Left/Right - Mic`).
   - ± changes the gain of the selected channel. Gain range depends on the source
     (internal −6…+6 dB, jack mic −10…+40 dB); the camera remembers gain per source.
   - Typical use: RODE Wireless GO (Gen 3) on the jack; split mode = TX1 left, TX2 right.
3. **FRAME RATE** (`mockups/P3_fps.png`), rarely used. Shutter stays 180°.

## Edit screens (from MAIN, swipe down = back)

- **IRIS** (`mockups/E1_iris.png`): tap left/right half = −/+ 1/3 stop, arc shows the position between the lens's limits.
- **WB** (`mockups/E2_wb.png`): ±100 K (limited to 2500–10000), AUTO button (`doAuto`).
- **BASE GAIN** (`mockups/E3_gain.png`): LOW (0 dB, ISO 400) / HIGH (+18 dB, ISO 3200).

## Physical buttons

- GP16 REC toggle (locked 1.5 s after STOP, 0.5 s after START)
- GP17 momentary AF (`lens/focus/doAutoFocus`; the operator judges focus)
- GP18, GP26 spare

## Fixed behaviour

- Exposure is always manual: auto exposure is switched off on connect.
- Shutter 180° is set on connect. 25 fps is the default under 50 Hz light.
