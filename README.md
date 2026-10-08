# Pip-Boy 3000 wrist HUD

A Fallout 3 style Pip-Boy for an Arduino Uno with a 3.5" touch screen. It boots like a CRT, shows
the STATS, ITEMS and DATA pages from the game, and animates everything that should move: a
Geiger counter, a live radio oscilloscope, a ticking clock, sliding selection boxes and a rolling
CRT scan band. There is a lock screen, four display colours and a hidden falling blocks game.

The page artwork is the original set of hand made screen designs (`sdcard/`), compiled into a
compact layout format and packed bitmaps by `tools/build_assets.py`.

## Hardware

| Part | Detail |
|---|---|
| Board | Arduino Uno R3 (ATmega328P, 32 KB flash, 2 KB RAM) |
| Screen | 3.5" 480x320 ILI9486 shield, 8-bit parallel (controller ID `0x9486`) |
| Touch | Resistive, on the same shield |
| Storage | microSD slot on the shield (FAT16 or FAT32) |

The shield uses every Uno pin except A5 (and D0/D1, which carry USB serial):

| Function | Pins |
|---|---|
| LCD data | D2-D7 (PORTD), D8-D9 (PORTB) |
| LCD control | A0 RD, A1 WR, A2 CD, A3 CS, A4 RESET |
| Touch | XP D7, YM D6, YP A2, XM A1 (shared with the LCD) |
| SD card | D10 CS, D11 MOSI, D12 MISO, D13 SCK |
| Free | A5 |

There are no motion sensors, no speaker and no vibration motor. The backlight is wired on
permanently, so "screen off" shows black with the backlight still lit.

## Using it

It starts by itself whenever it has power (USB, a power bank, 7-12 V on the barrel jack, or 5 V on
the 5V pin). Tap during the boot sequence to skip it.

**First boot** shows a touch calibration screen. Tap the centre of each of the three crosshairs.
Calibration is saved and never asked for again. Redo it any time from Settings.

| To | Do |
|---|---|
| Open the menu | Tap the tab title (STATS, ITEMS or DATA) at the top left |
| Change tab, open Settings, or turn the screen off | Pick it from the menu. Tap outside the menu to close it |
| Change page | Tap a label along the bottom (eg Skills, Apparel, Radio) |
| Select a list item | Tap the row. The selection box grows out from your finger |
| Status page views | Tap CND, RAD or EFF on the left |
| Identify a map location | DATA > World Map, tap a marker |
| Change radio station | DATA > Radio, tap a station |
| Wake from screen off | Touch the screen, then drag the knob to the right |
| Play the secret game | STATS > General, tap the Vault Boy 5 times within 3 seconds |

**Settings**: display colour (green, amber, blue, white), scanlines, orientation (flip 180°),
standby after 1 minute, 5 minutes or never, and touch calibration.

The default green is `#1AFF80`. Amber is `#FFB642`, the "Default Amber" value from
FalloutPrefs.ini. Blue and white are approximations.

### Vault-Tec Stacker (the secret game)

Falling blocks with the standard piece colours, a ghost piece, a next piece preview and a saved
high score.

| Control | Action |
|---|---|
| `<` and `>` | Move. Hold to keep moving |
| ROTATE, or tap the board | Rotate |
| DROP | Drop the piece straight down |
| [EXIT] | Back to the Pip-Boy |

The speed goes up every 10 lines. After a game over, tap anywhere to play again.

## Building and flashing

You need [arduino-cli](https://arduino.github.io/arduino-cli/) and the AVR core. The firmware uses
no third party libraries.

```bash
arduino-cli core update-index
arduino-cli core install arduino:avr

arduino-cli compile -b arduino:avr:uno pipboy
arduino-cli upload  -b arduino:avr:uno -p /dev/ttyACM0 pipboy    # COM3 on Windows
```

Flash use is about 31.9 of 32.2 KB, so there is almost no room left. Anything substantial needs
something else removed, or an Arduino Mega (the shield fits it, and it has 8 times the flash).

## Setting up the SD card

The firmware reads its page layouts and pictures from a `PIPART` folder in the root of the card.
The ready made files are in `build/PIPART/`.

The simplest way is to copy the folder across with a card reader, so the card has
`PIPART/ART00.PIP` ... `PIPART/LAY17.PIP`.

Or load it over USB without removing the card:

```bash
arduino-cli upload -b arduino:avr:uno -p /dev/ttyACM0 tools/sdloader   # temporary loader sketch
python3 tools/load_sd.py /dev/ttyACM0                                  # writes and reads back each file
arduino-cli upload -b arduino:avr:uno -p /dev/ttyACM0 pipboy           # put the HUD back
```

The loader only writes inside `PIPART`. Other files on the card are left alone.

Without the card the HUD still boots and its boot check shows `SD STORAGE ... NOT FOUND`. The pages
drawn in code (maps, RAD, EFF) still work, but the tab frames and the designed pages are empty.

## Editing the screens

`sdcard/` holds the original screen designs: `FRAMEn.DAT` is the frame for each tab and
`SCREENtp.DAT` is page `p` of tab `t`. Each file is a list of draw commands. Every command is an
opcode byte followed by little endian 16-bit values and a colour byte (0 dim, 1 bright):

| Opcode | Command | Values |
|---|---|---|
| 1 | Horizontal line | x, y, width |
| 2 | Vertical line | x, y, height |
| 3 | Filled rectangle | x, y, width, height |
| 4 | Line | x0, y0, x1, y1 |
| 5 | Filled triangle | x0, y0, x1, y1, x2, y2 |
| 6 | Text | x, y, then colour, a length byte and the characters |
| 7 | Text size | one byte, no colour |
| 8 | Rectangle outline | x, y, width, height |

After changing a design, rebuild and reload the card:

```bash
pip install pillow pyserial
python3 tools/build_assets.py
```

This splits each page into its Vault Boy picture (the region listed in `ART_BOXES` in
`tools/split_art.py`) and the rest of the layout. It writes `build/PIPART/` and
`pipboy/layout.h`, then decodes everything again and compares it with the design pixel by pixel.
It prints the difference per page and exits with an error if anything differs.

Two deliberate fixes to the original art live in `repair()` in `tools/split_art.py`: the Vault Boy
on the Status page gets the boot missing from his right leg, and the torso condition bar moves
below his feet so it no longer crosses his chest.

`tools/render_dat.py` renders the original `.DAT` files to PNG for previewing.

## Serial debug interface

The firmware listens on USB serial at **1,000,000 baud**. Opening the port resets the Uno, which
then reboots through the boot sequence. `tools/pip.py` wraps the commands.

| Command | Effect |
|---|---|
| `S` | Screenshot read back from the panel's memory (RGB565, sent in acknowledged 60 byte chunks) |
| `T` x y | Inject a tap (x and y as 16-bit little endian) |
| `H` x y | Inject a hold |
| `R` | Raw touch reading: x, y, pressure and the two pressure channels |
| `G` | Start the game |
| `Y` | Play the "HELLO, PETER" wake animation |
| `Z` / `W` | Screen off / wake |
| `C` | Run touch calibration |
| `Q` | Current row of the CRT roll band (-1 when idle) |
| `K` | Skip calibration during testing. **This discards the saved calibration** |

The firmware also prints `TAP x y` and `HOLD x y` for every real touch.

```bash
python3 tools/pip.py shot screen.png       # about 22 s for a full screenshot
python3 tools/pip.py tap 160 303           # tap S.P.E.C.I.A.L.
SHOTS=/tmp python3 tools/tour.py tap:60,20 wait:0.5 shot:menu   # scripted steps
```

## How it works

**Display driver** (`pipboy/lcd.h`). It writes straight to the AVR ports instead of using a
library. A fill whose two colour bytes match only toggles the write strobe, so screen clears are
fast. That is why every theme's scanline colour has matching bytes. The init values match
MCUFRIEND_kbv for the 9486. The panel can be read back, which the screenshots and the CRT roll
band rely on.

**Pages** (`pipboy/pipboy.ino`). A page layout is a bit packed list of lines, boxes and text, one
SD sector each, so loading a page costs one read. Text that sits on a filled box is drawn
transparently and everything else uses a fast opaque glyph path. Vault Boy pictures are run length
encoded 1-bit bitmaps streamed straight to the panel. A page switch, including its animations,
takes about 85 to 130 ms.

**Flicker free animation.** Nothing that moves is erased and redrawn. The radio wave is updated
column by column with each pixel written once in its final colour. The Geiger needle is drawn in
its new position before the old pixels it no longer covers are cleared. The lock screen knob, the
tab slider and the game pieces are updated by their changed area only.

**CRT roll band.** Every few seconds each row in turn is read back from the panel, brightened and
restored a few frames later. Pages only use five theme colours, so every lit colour maps back to
exactly one original, and anything drawn over the band meanwhile is left as it is.

**Touch** (`pipboy/io.h`). The touch panel shares pins with the LCD, so the LCD is deselected while
it is read. Pressure at rest drifts between power ups (about 158 to 176), and light taps in the
top left corner only read about 210. So a press counts as a rise of 25 over the tracked resting
level, not a fixed threshold. Calibration and settings are stored in EEPROM.

## Restoring the original firmware

`backup/` holds what was on the Arduino before this project:

| File | Contents |
|---|---|
| `original_sketch.hex` | The original sketch (31,040 bytes). **Upload this one** |
| `original_flash.hex` | The full 32 KB flash dump, including the bootloader |
| `original_eeprom.hex` | **Not a real EEPROM backup**, see below |

```bash
arduino-cli upload -b arduino:avr:uno -p /dev/ttyACM0 --input-file backup/original_sketch.hex
```

The original firmware reads `SCREENtp.DAT` and `FRAMEn.DAT` from the root of the card, which this
project never changes, and it ignores `PIPART`.

`original_flash.hex` is kept for completeness. Uploading it through the bootloader would try to
overwrite the bootloader itself, which the bootloader refuses, so use `original_sketch.hex`.

The Uno's bootloader cannot read EEPROM, so the attempt to back it up returned flash contents
instead. `original_eeprom.hex` is kept only as a record of that and should not be written back.
The original EEPROM contents were not saved, and this firmware stores its settings at the start of
EEPROM.

## Troubleshooting

| Problem | Fix |
|---|---|
| `Permission denied: /dev/ttyACM0` (Linux or WSL) | `sudo chmod 666 /dev/ttyACM0`, or join the `dialout` group and log in again |
| No `/dev/ttyACM0` in WSL | Attach the USB device with `usbipd attach --wsl --busid <id>` from Windows |
| Taps land in the wrong place | Settings > CALIBRATE TOUCH |
| Pages are empty | The card is missing the `PIPART` folder. See [Setting up the SD card](#setting-up-the-sd-card) |
| Upload fails with "not in sync" | Close anything else using the serial port and try again |
| Screen upside down | Settings > ORIENTATION |

## Repository layout

```
pipboy/            firmware (open pipboy/pipboy.ino)
  lcd.h            ILI9486 driver and panel read back
  gfx.h            drawing, fonts, themes, scanlines
  sd.h             read only SD and FAT reader for PIPART
  io.h             touch, settings in EEPROM, serial
  game.h           Vault-Tec Stacker
  layout.h         bottom tab labels (generated)
  font.h           5x7 font (from Adafruit GFX glcdfont)
build/PIPART/      files for the SD card (generated)
sdcard/            original screen designs
tools/             asset compiler, SD loader, serial and preview tools
backup/            the Arduino's original firmware
```
