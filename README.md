# TaskPal for M5Paper

TaskPal is a calm, picture-based checklist for kids. It shows their routines (Morning, After school, Bedtime) on an M5Paper e-ink tablet, and parents edit the lists in a PIN-protected parent area. For how to use it, see the **[User guide](docs/USER_GUIDE.md)**.

<p>
<img src="docs/img/picklist.png" width="200" alt="Pick a list">
<img src="docs/img/checklist_1.png" width="200" alt="Checklist">
<img src="docs/img/celebrate_13.png" width="200" alt="All done, with the dancing corgi">
</p>

This page explains how to build the firmware and install it on the device.

## 1. What you need

| Item | Details |
|---|---|
| Device | **M5Paper V1.1** (ESP32, 16 MB flash, 8 MB PSRAM, IT8951 e-ink 960×540, GT911 touch, BM8563 clock) |
| Cable | USB-C data cable (not charge-only) |
| Computer | Windows 10/11 (the commands below are PowerShell). macOS and Linux work too: use the same `pio` commands and your own serial port name. |
| PlatformIO | [PlatformIO Core](https://platformio.org/install/cli), or VS Code with the PlatformIO extension (it installs Core). Everything else (the ESP32 platform, compiler and libraries) downloads on the first build. |
| conda (optional) | Only for running the unit tests and the asset tools in `tools/` (see §6). Building and installing don't need it. |

**USB driver:** the M5Paper V1.1 has a CH9102 USB-serial chip. Windows 10/11 usually installs a driver automatically ("USB-Enhanced-SERIAL CH9102" in Device Manager). If no COM port appears when the device is plugged in and switched on, install the WCH **CH343SER** driver.

**Find the serial port:** open Device Manager → *Ports (COM & LPT)*, or run `pio device list`. This project assumes **COM3**. If yours differs, change `upload_port` and `monitor_port` in [`platformio.ini`](platformio.ini), or add `--upload-port COMx` to the upload command.

In the commands below, `pio` means the PlatformIO executable. If it isn't on your PATH, use its full path (on Windows usually `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`):
```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio --version
```

## 2. Get the code
```powershell
git clone <repository-url> TaskPal
cd TaskPal
```

## 3. Build
```powershell
& $pio run -e m5paper
```
- The first build takes several minutes: it downloads the pioarduino ESP32 platform (Arduino-ESP32 3.3.8), M5Unified 0.2.18, M5GFX 0.2.30 and ArduinoJson 7.4.3. All versions are pinned in `platformio.ini`.
- It ends with `[SUCCESS]`. The output lands in `.pio\build\m5paper\`:

| File | Flash address | What it is |
|---|---|---|
| `bootloader.bin` | `0x1000` | ESP32 bootloader |
| `partitions.bin` | `0x8000` | Partition table (from [`partitions.csv`](partitions.csv)) |
| `firmware.bin` | `0x10000` | TaskPal |
| `firmware.factory.bin` | `0x0` | All of the above (plus `boot_app0`) in one image, for flashing without PlatformIO |

The fonts and pictures are compiled into the firmware, so there is no separate file-system image to upload.

## 4. Install on the device

### 4.1 First time only: back up the factory firmware
The M5Paper ships with M5Stack's factory app. Installing TaskPal overwrites it, so **save a copy first** if you may want it back. With the device on and connected, read the whole 16 MB flash (about 6 minutes) with esptool 5.x:
```powershell
esptool --chip esp32 --port COM3 --baud 460800 read-flash 0 0x1000000 m5paper_factory.bin
```
Reading it twice and comparing the two files' hashes (`Get-FileHash`) confirms a clean copy. To restore it later:
```powershell
esptool --chip esp32 --port COM3 --baud 460800 write-flash 0 m5paper_factory.bin
```
Keep the `.bin` file somewhere safe. The `backup/` folder is git-ignored, so it's a good place for a local copy.

### 4.2 Flash
1. Connect the M5Paper with the USB-C cable and switch it on: press the **side button** (the rocker on the right edge) **in**.
2. Run:
   ```powershell
   & $pio run -e m5paper -t upload
   ```
3. The upload takes about 45 seconds and ends with `Hash of data verified` and `[SUCCESS]`. The device restarts into TaskPal by itself.

**Without PlatformIO** (e.g. installing a saved release image): use esptool 5.x, which comes with the Arduino ESP32 core, or `pip install esptool`:
```powershell
esptool --chip esp32 --port COM3 --baud 460800 write-flash 0x0 firmware.factory.bin
```

**Your data is kept across updates.** Lists, progress, settings and the power log live in the LittleFS partition (`0x410000`), and the WiFi password in NVS (`0x9000`). Neither the upload nor the factory image writes there. To start completely fresh, erase the flash first (`esptool erase-flash`), then flash again.

### 4.3 First start
- TaskPal opens on **Pick a list** with three example lists (Morning, After school, Bedtime).
- The clock isn't set yet. Open the parent area (hold the gear for 3 seconds) and use **Device settings → Date & time** or **WiFi & time sync**. On first entry it asks you to create a 4-digit PIN.
- Then edit the lists for your child. The [User guide](docs/USER_GUIDE.md) walks through everything.

## 5. Serial monitor and test commands
```powershell
& $pio device monitor            # 115200 baud; DTR/RTS are off so the monitor doesn't reset the device
```
The boot line reports the state of the display, storage, clock and fonts. The firmware also accepts one-letter commands, typed in the monitor and ended with Enter (for testing; they need the USB cable):

| Command | Does |
|---|---|
| `S` | Print settings, lists and progress (JSON) |
| `V` | Battery voltage and level |
| `G` | Print the power log (sleep, wake, battery, restarts) |
| `T HH:MM` / `D YYYY-MM-DD` | Set the time / date |
| `C` | Clear today's checks (stars are kept) |
| `Y` | Go to sleep now (deep sleep after 60 s instead of 30 min) |
| `K` | Show the low-battery screen and switch off |
| `Z` | Clear the clock (to test "clock not set") |
| `R`, `P`, `A`, `X`, `W` | Display recovery, reload Pick a list, add/remove test tasks, save stress test |

## 6. For developers

### Python tools (conda)
The tools run in a conda environment called `taskpal`:
```powershell
conda create -n taskpal python=3.11 pip
conda run -n taskpal python -m pip install -r tools/requirements.txt
conda install -n taskpal -c conda-forge ucrt64-gcc-toolchain     # compiler for the unit tests (Windows)
```

| Task | Command |
|---|---|
| Unit tests (`src/core`, on the PC) | `conda run -n taskpal $pio test -e native` |
| Rasterize the icon SVGs in `assets/icons/` into `src/ui/icons_data.*` | `conda run -n taskpal python tools/gen_icons.py` |
| Regenerate fonts and `fonts.ini` | `conda run -n taskpal python tools/gen_vlw.py` |
| Power-cut test (resets the device mid-save 20 times) | `conda run -n taskpal python tools/pwr06_cut_test.py COM3 20` |

### Repository layout
```
TaskPal/
├─ docs/               USER_GUIDE.md
│  └─ img/             screenshots of the device screens (used by the README and user guide)
├─ src/
│  ├─ core/            pure C++ logic (lists, days, stars, PIN, JSON), unit-tested on the PC
│  ├─ hal/             display, touch, clock, storage, power, WiFi
│  ├─ ui/              screen manager, widgets, keyboard, fonts, generated icons
│  ├─ screens/         kid screens, parent area
│  └─ app/             app state, sleep and battery logic, version
├─ test/test_core/     Unity tests for src/core
├─ tools/              Python tools (assets, tests); extract_mockup.py needs the design mockups, which aren't in this repo
├─ assets/             source fonts and icon SVGs; vlw/ holds the fonts compiled into the firmware
├─ boards/m5paper.json board definition
├─ partitions.csv      4 MB app, ~12 MB LittleFS, coredump
├─ platformio.ini
├─ fonts.ini           list of embedded fonts (generated by tools/gen_vlw.py)
└─ LICENSE             GPL-3.0
```

## 7. Troubleshooting

| Problem | What to do |
|---|---|
| No COM port appears | Use a data cable, switch the device on (side button in), install the CH343SER driver, try another USB port. |
| Upload fails with "Failed to connect" or "port busy" | Close any serial monitor or tool using the port, check that the device is switched on, then retry. |
| The screen doesn't respond to touch or the button | The battery is probably empty. Plug in USB; see the User guide, "Battery". |
| Garbled screen right after an upload | The display controller can hang after flashing; TaskPal detects this and restarts by itself (up to twice). |
| Wrong time | Parent area → Device settings → WiFi & time sync → Sync now, or set it by hand under Date & time. |

## 8. License
TaskPal is free software under the **GNU General Public License v3.0**; see [LICENSE](LICENSE).

The fonts in `assets/fonts/` (and the VLW fonts made from them in `assets/vlw/`) are not covered by the GPL. Fredoka and Atkinson Hyperlegible are under the SIL Open Font License 1.1; see the `-OFL.txt` files next to them.
