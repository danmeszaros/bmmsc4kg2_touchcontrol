# Podklady pro ovladač (Micro Studio Camera 4K G2, REST API přes USB)

Shrnutí z průzkumu 2026-11-21. Podrobnosti a naměřené hodnoty jsou v `camera-quirks.md`, síťová vrstva v `network-layer.md`, API kamery v `api-yaml/`.

Všechny cesty jsou relativní k `/control/api/v1/`. Kamera dostane IP z DHCP serveru na desce (10.0.7.16).

## Síť a HTTP

| Pravidlo | Proč |
|---|---|
| `LWIP_IP_ACCEPT_UDP_PORT(port) ((port) == PP_NTOHS(67))` v `lwipopts.h` | Bez toho lwIP zahodí DHCP DISCOVER z 0.0.0.0 a kamera skončí na 169.254.x.x |
| Jedno keep-alive spojení, reuse | Kamera spojení sama nikdy nezavře (ani s `Connection: close`). Reuse ~2,5 ms vs nové ~3,3 ms |
| Posílat přesně `Content-Length` bajtů, nic za body | Extra `\r\n` za body PUTu: další request na spojení dostane jen FIN |
| Při `closed_early` / RST / timeoutu na reused spojení: jednou zopakovat na novém | Obrana proti výše uvedenému a proti restartu kamery |
| Timeout requestu ~500 ms | Běžně 2–5 ms, občas jeden PUT 17–44 ms |
| Úspěch PUTu ověřit GETem | `video/gain` s neplatnou hodnotou vrátí 204 a nic nezmění |

Odpovědi: PUT vrací 204 bez body. Některé GETy vracejí holou hodnotu (`transports/0/stop` → `true`, `presets/active` → `"default"`). Chyby: 400/404/501 s JSON `{"error": …}`, neznámá cesta HTML 404.

## Funkce

### Expozice: vždy manuální
- Při startu ověřit `video/autoExposure` → `{"mode":"Off"}`, jinak `PUT {"mode":"Off","type":""}`.
- `lens/iris.continuousApertureAutoExposure` a `video/shutter.continuousShutterAutoExposure` musí být `false`.

### Base gain (přepínač low/high)
- Low: `PUT video/gain {"gain": 0}` (ISO 400). High: `{"gain": 18}` (ISO 3200). Dual native gain podle specifikace Blackmagic.
- Hodnota se projeví hned. GET hned po PUTu už vrací novou.
- Platné jsou jen sudé hodnoty −12 … +36, jiné kamera tiše ignoruje.

### White balance (jen náhled, záznam je BRAW)
- `PUT video/whiteBalance {"whiteBalance": K}`, krok podle volby, např. 100 K. **Omezit na 2500–10000 v ovladači**, kamera přijme cokoliv.
- Tint (pokud bude potřeba): `video/whiteBalanceTint`, omezit na ±50.
- Auto WB: `PUT video/whiteBalance/doAuto` (bez body). Výsledek (WB i tint) je v GETu za ~0,2 s, pak ho přečíst a zobrazit.

### Iris
- Krok ±1/3 EV: `PUT lens/iris {"apertureStop": f}` podle řady 3.5, 4, 4.5, 5, 5.6, 6.3, 7.1, 8, 9, 10, 11, 13, 14, 16, 18, 20, 22. Nebo pracovat s `apertureNumber` (64 na 1 EV).
- Dorazy číst z GETu. Nejširší clona závisí na zoomu (f/5.5 na 34 mm). Mimo rozsah se ořízne bez chyby.
- Dojetí ~0,27 s na krok. GET hned po PUTu ještě vrací starou hodnotu.

### Momentary AF (tlačítko)
- `PUT lens/focus/doAutoFocus` (bez body) → 204. Contrast-detect, ustálí se za ~2–2,5 s. Konec se nehlásí, uživatel ho kontroluje sám.

### Record toggle (tlačítko)
- `PUT transports/0/record {"recording": true|false}`.
- Po START je `GET transports/0/record` hned `true`. **Po STOP zůstává `true` ještě ~1,2 s** (dokončení souboru). START v tomto okně kamera přijme a začne nový klip.
- Doporučení: ovladač si pamatuje poslední příkaz. Tlačítko po STOP zablokovat na ~1,5 s, pak převzít stav z GETu (mohl se změnit i tlačítkem na kameře).

### Stav pro displej / tally (polling)
| Údaj | Endpoint | Interval |
|---|---|---|
| Nahrávání | `transports/0/record` → `recording` | 250–500 ms |
| Timecode (při záznamu) | `transports/0/timecode` → `clip`, `timecode` (BCD: hex číslice = HHMMSSFF) | 250–500 ms při záznamu |
| Místo | `media/workingset` → `workingset[0].remainingRecordTime` (s), `remainingSpace` (B) | 5–10 s |

Jeden GET na desce ~3 ms, takže polling zátěž nedělá. WebSocket (`/control/api/v1/event/websocket`, podporovaný) by dal změny bez pollingu, zatím není potřeba.

### Shutter 180°
- Při startu `PUT video/shutter {"shutterAngle": 18000}`.
- GET vrací `shutterSpeed` nebo `shutterAngle` podle nastavení „shutter measurement“ v menu kamery. Ovladač musí přijmout obojí.
- Pod 50Hz světlem: 25 fps + 180° (= 1/50) je bez flickeru. 24 fps + 180° (1/48) a 1/60 flickerují.

### Audio
- Kanál 1 v kameře = `audio/channel/0`, kanál 2 = `audio/channel/1`.
- Interní mic ↔ jack: `PUT audio/channel/N/input {"input": "Camera - Left"}` / `{"input": "3.5mm Left - Mic"}` (seznam v `…/supportedInputs`).
- Úroveň: `PUT audio/channel/N/level {"gain": dB}` (i desetinné) nebo `{"normalised": 0..1}`. Rozsah podle vstupu z `…/input/description`: interní −6…+6, jack mic −10…+40. Mimo rozsah 400.
- Gain se pamatuje pro vstup, ne pro kanál. Po přepnutí vstupu znovu přečíst `level`.
- API nemá meter signálu. Phantom, low-cut a pad na této kameře nejsou.

### FPS (v menu, zřídka)
- GET `system/format`, změnit `frameRate` (string, např. `"25"`), **PUT celý objekt**. Částečné body → 400 „Format is not supported“.
- Povolené hodnoty v `system/supportedFormats` (23.98, 24, 25, 29.97, 30, 50, 59.94, 60).
- Po změně fps znovu nastavit shutter 180° (kamera drží čas, ne úhel).

## Mimo rozsah ovladače
Formát/kodek/rozlišení, formátování disku (`media/devices/*/doformat`), presety. Endpointy `system/codecFormat`, `videoFormat`, `supported*Formats` a `video/ndFilter` vracejí 501.

## Stav kamery po testech
25 fps, 180° (1/50), gain 0, WB 2600 K / tint 11 (výsledek auto WB), f/5.5, 34 mm, focus 0.814, audio ch1 = 3.5mm Left - Mic +20 dB, ch2 = Camera - Right 0 dB. Na disku testovací klipy A002 C006–C010.
