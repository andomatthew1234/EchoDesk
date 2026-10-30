# The EchoDesk Project

**ESP-32 and CYD based operating system and utility management**

EchoDesk OS is a free, open-source touchscreen interface designed for the **Freenove FNK0114A 2.4-inch ESP32 display**.
Transform an inexpensive CYD (Cheap Yellow DIsplay) into a compact desktop monitor.


Built for makers, developers and anyone who wants a little more information on their desk.

![EchoDesk OS](assets/echodesk-logo.png)

## Utilities

### Home Dashboard

The launcher to launch applications and utilities.

- **Codex Usage** — Monitor your OpenAI Codex limits. Support for Claude Code and Gemini coming soon.
- **Clock** — A bold, minimal clock with customisability.
- **Settings** — Adjust brightness and restart the device, with more settings coming soon.

### Codex Usage Monitor

View your Codex usage directly on your desk without opening a browser.

Support for:
- 5-hour session usage
- Weekly usage
- Usage percentages and progress bars
- Reset countdowns
- Live updates through the Windows companion application

EchoDesk builds on the open-source AI Monitor project and uses Win-CodexBar to retrieve Codex usage information.

### Clock

A distinctive two-line clock inspired by EchoDesk's logo, with customisability coming soon.

The hour is displayed in large outlined typography, while the minutes appear in solid, bold lettering.

Supports the Australia/Melbourne timezone and daylight-saving time with more time zones coming soon.

### Settings

Keep things simple.

- **Brightness:** Adjust the screen's backlight using a touchscreen slider.
- **Restart:** Safely restart EchoDesk OS.

## Required Hardware

## Required Materials

EchoDesk OS is designed to be affordable and easy to assemble. You only need a touchscreen, a USB-C cable and an optional 3D-printed enclosure.

| Component | Requirement | Cost |
|---|---|---|
| Display / Microcontroller | Freenove 2.4" CYD | A$26 |
| Connection | USB-C cable | Included with CYD |
| Enclosure | 3D-printed case | ~A$2 (at A$30/kg PLA) |
| **Total** | | **~A$28** |

*Prices are approximate and may vary depending on the retailer, filament and printing settings. A Windows 10 or 11 computer is also required to run the companion software.*

### Display / Microcontroller

<img src="assets/display.jpg" alt="Freenove 2.4-inch ESP32 touchscreen" width="350">

**[Purchase the Freenove 2.4" CYD on Amazon Australia](https://www.amazon.com.au/dp/B0H39PKRN2?ref=ppx_yo2ov_dt_b_fed_asin_title)**

This device provides the processing power for EchoDesk OS and includes a built-in **2.4-inch IPS touchscreen**.

No electronics assembly is required. Simply connect the device to your computer using the included USB-C cable and install the EchoDesk firmware.

**Specifications:**
- ESP32 dual-core microcontroller
- 2.4-inch IPS touchscreen
- 240 × 320 resolution
- Wi-Fi and Bluetooth connectivity
- USB-C programming and power
- ST7789 display controller
- XPT2046 touch controller


**Important:** EchoDesk is specifically designed for the Freenove FNK0114A_2P4. Standard 2.8-inch Cheap Yellow Display boards have different hardware configurations and are not automatically compatible. Check compatiblity with other displays before buying, or use our reccomended one.

### 3D-Printed Enclosure

EchoDesk can operate without an enclosure, but we recommend using one for a cleaner desktop setup and better airflow.

The custom enclosure is designed to house the Freenove touchscreen at a comfortable viewing angle.

**You have two options:**

1. **Print it yourself:** Download the model files (coming soon) and print the enclosure using your own 3D printer. Estimated material cost is approximately A$2 using PLA at A$30/kg including waste in one color.
2. **Order a printed enclosure:** Don't have a 3D printer? Contact **[matthew@andomail.com](mailto:matthew@andomail.com)** to enquire about ordering a pre-printed enclosure.

The enclosure is optional and does not affect EchoDesk OS functionality, but we strongly recomend it.


No soldering or additional electronic components are required for the standard setup.

## Required Software

Install the following applications on your Windows PC.

### 1. Win-CodexBar (Codex usage monitor utility)

Win-CodexBar retrieves Codex usage information from your existing Codex authentication. Support for Claude Code and Gemini is coming soon.

Download:

https://github.com/nesszer/Win-CodexBar/releases

Alternatively, install it using Windows Terminal:

```powershell
winget install Finesssee.Win-CodexBar
```

After installation:

1. Launch Win-CodexBar.
2. Open its system tray menu.
3. Enable the Codex provider.
4. Complete authentication if prompted.
5. Confirm your Codex usage appears correctly.

### 2. AI Monitor for Windows (Codex usage monitor utility)

AI Monitor is the companion application responsible for sending usage data to EchoDesk over USB.

Download:

https://github.com/tobymarks/esp32-ai-monitor/releases

Install the Windows companion application from an appropriate `win-v*` release.

Keep AI Monitor running whenever you want live Codex usage updates.

### 3. Python and esptool

For manual firmware installation, install Python and Espressif's flashing utility.

Python:

https://www.python.org/downloads/

Then run:

```powershell
py -m pip install esptool
```

You do not need Arduino IDE to install a precompiled EchoDesk firmware image.

## Let's install 🚀

### Step 1 — Download the Firmware

Confirm all of the above applications are installed. Then:

Open the **Releases** section of this GitHub repository.

Download the firmware image for your board:

`echodesk-freenove-fnk0114a-2p4.bin`

*If no precompiled release is available yet, build the firmware from source using the instructions below.*

### Step 2 — Connect Your Screen

Connect the Freenove display to your Windows PC using a USB-C data cable.

Open **Device Manager → Ports (COM & LPT)**.

Find the device named something similar to:

`USB-SERIAL CH340 (COM5)`

Your COM port number may be different.

### Step 3 — Install the Firmware

**Close any applications currently using the display's serial port (eg. AIMonitor)**

Open Windows Terminal in the folder containing the downloaded firmware (CD to the path of the folder)

For a verified, merged ESP32 firmware image intended to be written at address `0x0`, use:

```powershell
py -m esptool --chip esp32 --port COM5 --baud 460800 write-flash 0x0 echodesk-freenove-fnk0114a-2p4.bin
```

Replace `COM5` with your actual port number.

**Important:** Only use this command with a release explicitly documented as a merged image for the original ESP32. Do not use it for application-only firmware binaries. Check the release's flashing instructions before proceeding.

Wait for flashing to finish successfully.

Do not disconnect the device during flashing.

### Step 4 — Start EchoDesk

After flashing, restart the display using its reset button or by reconnecting USB.

EchoDesk should boot to the Home Dashboard.

You can now open the Clock and Settings applications using the touchscreen.

### Step 5 — Connect Codex Usage

1. Start Win-CodexBar.
2. Confirm that Codex usage is available.
3. Start AI Monitor for Windows.
4. Connect AI Monitor to the display's serial port.
5. Open **Codex Usage** on EchoDesk.

Your usage information should appear once the companion successfully communicates with the device.

The Windows computer must remain connected for live USB usage updates.

## Building From Source

EchoDesk uses PlatformIO for firmware development.

Requirements:

- Git
- Python
- PlatformIO Core or the PlatformIO VS Code extension

Clone the repository:

```bash
git clone https://github.com/YOUR_USERNAME/EchoDesk-OS.git
cd EchoDesk-OS
```

Build for the Freenove touchscreen:

```bash
pio run -e freenove-fnk0114a-2p4
```

The compiled application firmware will appear in the corresponding `.pio/build/` directory.

**Note:** The application firmware generated by PlatformIO is not necessarily a merged flash image. Follow the repository's firmware packaging instructions before flashing.

## 3D-Printed Enclosure

EchoDesk can be used without an enclosure, but a custom 3D-printed desktop housing is recommended.

The enclosure design includes:

- Angled desktop viewing position
- USB-C cable access
- Rear ventilation
- Removable rear cover
- Internal PCB mounting points

Designed for printing on the Bambu Lab A1 Mini or another compatible FDM printer.

Enclosure files and print instructions will be published in the `hardware/` directory once the physical design has been validated.

## Technical Specifications

| Property | Value |
|---|---|
| Microcontroller | ESP32 |
| Display controller | ST7789 |
| Display resolution | 240 × 320 |
| Interface orientation | 320 × 240 landscape |
| Touch controller | XPT2046 |
| Backlight | GPIO21, active-high PWM |
| LCD SPI | HSPI |
| USB serial | CH340C |
| Serial baud rate | 115200 |
| Primary data source | Win-CodexBar |
| Companion application | AI Monitor for Windows |

## Troubleshooting

**Screen stays black:** Check USB power, firmware compatibility and backlight initialisation.

**Screen is white or corrupted:** Verify the ST7789 display configuration and SPI pin assignments.

**Touchscreen doesn't respond:** Check touch calibration and rotation settings.

**Codex usage isn't appearing:** Verify that Win-CodexBar is receiving data, AI Monitor is running, and the correct COM port is selected.

**Device cannot be flashed:** Close other applications using the serial port, confirm the COM number, and retry the documented flashing procedure.

## Credits

EchoDesk OS is built upon and inspired by excellent open-source projects (I strongly reccomend trying them):

- [AI Monitor](https://github.com/tobymarks/esp32-ai-monitor) — ESP32 usage display and Windows companion.
- [Win-CodexBar](https://github.com/nesszer/Win-CodexBar) — Windows Codex usage tracking.
- [Freenove](https://github.com/Freenove/Freenove_ESP32_Display) — Hardware documentation and examples.
- [PlatformIO](https://platformio.org/) — Embedded firmware build tools.

EchoDesk is an independent community project and is not affiliated with or endorsed by OpenAI.

## Licence

The licence for EchoDesk OS will be published after confirming compatibility with the licences of its upstream dependencies.

Until then, no additional licence is implied for EchoDesk-specific code.

---

**EchoDesk OS — A little screen. A lot of possibilities.**