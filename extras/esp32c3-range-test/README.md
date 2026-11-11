# ESP32-C3 PN532 UART Card Range Test

A standalone card-identification and manual reading-distance test for
PN532 and PN5321-compatible readers using the HSU UART command interface.
It is shared test software; module specifications and electrical connections
remain specific to the selected hardware. See the [ELECHOUSE module table](../../README.md#elechouse-modules)
for product pages and manuals.

The complete sketch is in
[PN532_All_Cards_UART_Range_Test](PN532_All_Cards_UART_Range_Test).
Keep its `.ino`, `Pn532Hsu.cpp` and `Pn532Hsu.h` files together. It has its own
bounded HSU driver and does not require the general NFC library or NDEF.

## Reference hardware and wiring

The reference host is a **LuatOS CORE-ESP32-C3** with a CH343 USB-UART bridge.

| Reader signal | LuatOS CORE-ESP32-C3 |
| --- | --- |
| UART TX | GPIO4 / IO04, UART1 RX |
| UART RX | GPIO5 / IO05, UART1 TX |
| Supply input | Supply specified by the reader module |
| GND | GND |

Select HSU UART using the module's own mode-selection instructions, then power
cycle it. Connector positions and jumper names differ between products.
Use a supply within the module's rated range and signal levels compatible with
the 3.3 V ESP32-C3; add level conversion where required. This test does not
control a module reset pin.

Both serial links use **115200 baud, 8N1**. On the reference host, D4 is GPIO12
and lights on a successful result; D5 is GPIO13 and stays off. These LED pins
are board-specific. Adapt them before using a different ESP32-C3 board.

## Protocol entries and criteria

The program defaults to **AUTO + ID_ONLY**. AUTO scans Type A 106, Type B 106,
FeliCa 212, FeliCa 424, Jewel and the dedicated ST SR-family path in sequence.
A card found on one entry ends that scan cycle. NO_CARD is recorded only when
the complete cycle finds none. Unsupported Type B modes are skipped according
to the reader's firmware-support byte.

| Entry | Examples and returned identifiers |
| --- | --- |
| Type A, `a` | Classic/S50/S70, Ultralight, NTAG21x and compatible Type A cards; UID and available activation data |
| Type B, `b` | Standard REQB/ATTRIB activation for compatible Type B cards; ATQB and PUPI |
| FeliCa, `f` / `g` | 212 / 424 kbps polling; IDm, PMm and system code |
| Jewel/Topaz, `j` | Jewel ID and SENS_RES |
| ST SR family, `k` | Dedicated INITIATE/SELECT/GET_UID path for compatible SRI/SRT/ST25TB cards; real-card verification required |

Fix a protocol before comparing distances. Each attempt turns RF off for
20 ms, then on with a 10 ms wait, so a stationary card can be activated again.
For ISO-DEP cards, identification can include activation and ATS exchange.

The following success criteria are separate:

- `ID_ONLY`: parse an identifier such as UID, PUPI or IDm; no application keys needed.
- `CLASSIC_READ4`: authenticate with default Key A `FFFFFFFFFFFF`, then read block 4.
- `TYPE2_READ_PAGE4`: read pages 4-7, 16 bytes in total.
- `TYPE2_GET_VERSION`: query the card version; older cards may not implement it.

A wrong key, protected page or unsupported version command is not equivalent
to a failed identification. PUPI and some UIDs can change between activations.
Use one card at a time and label the physical sample. UID/SAK alone does not
guarantee exact chip identification. The program does not write card data.
The ST SR path temporarily changes reader registers and restores them afterward.

ISO15693 / NFC Type 5, 125 kHz LF, UHF RFID, peer-to-peer operation and card
emulation are outside this program's scope. A protocol entry does not imply
support for every proprietary variant or successful validation of every card.

## Serial commands

Send one command per line with CR or LF.

| Command | Action |
| --- | --- |
| `x` | AUTO scan, ID_ONLY |
| `a` / `b` | Fixed Type A / Type B at 106 kbps, ID_ONLY |
| `f` / `g` | Fixed FeliCa 212 / 424 kbps, ID_ONLY |
| `j` / `k` | Fixed Jewel/Topaz / dedicated ST SR-family entry |
| `u` | Return the current entry to ID_ONLY |
| `c` | Type A, Classic authentication and block 4 read |
| `t` / `v` | Type A, Type 2 pages 4-7 read / GET_VERSION |
| `n NTAG215_01` | Set a sample label, up to 39 ASCII characters |
| `d 30` / `d -1` | Set manually measured distance in mm / distance unknown |
| `r` | Clear statistics and reinitialize |
| `s` / `h` | Show statistics / help |

Changing protocol, criterion or sample resets statistics and returns distance
to unknown. Changing distance starts a new measurement window. Invalid commands,
invalid distances and oversized commands do not change the measurement point.

For an NTAG215 sample at a manually measured 30 mm, send these four lines:

```text
a
n NTAG215_01
u
d 30
```

Hold the card in place and inspect `[WINDOW20] pass=20 no_card=0 errors=0`.
Then try another measured distance, such as `d 35`, and collect another window.
Record card shape and dimensions, antenna-plane spacing, orientation, supply
and nearby metal. The `d` command records a ruler measurement; the reader does
not measure distance automatically. A 20-attempt window is an observation,
not a long-term reliability guarantee.

`[CARD]` reports identifiers and available raw activation information.
`[WINDOW20]` reports success/no-card/error counts for 20 attempts. `[STATS]`
reports cumulative results and the success streak. `RESULT,...` lines are CSV
records containing the sample, manually entered distance, protocol, criterion,
identifier, result and duration. Save the serial log for analysis.

## Build and upload

Use Arduino ESP32 core **3.3.8** with **ESP32C3 Dev Module**. Reference settings:
DIO, 40 MHz, 4 MB flash, USB CDC On Boot disabled. The reference board uses
the CH343 bridge on UART0 for its computer connection.

From the repository root in PowerShell:

```powershell
.\tools\build-esp32c3-range.ps1
.\tests\esp32c3-range\run.cmd
```

The build script defaults to `D:\GreenSoftware\Arduino_cli\arduino-cli.exe`,
then checks PATH. Override it with `-ArduinoCli <path>` when needed. It compiles
only and does not access a serial port. Output is in `.artifacts/esp32c3-range`;
temporary build files are in `.build/esp32c3-range`.

Windows host-side tests require Visual Studio C++ Build Tools. On Linux/macOS:

```sh
sh tests/esp32c3-range/run.sh
```

To upload, replace COM8 with the actual port:

```powershell
arduino-cli upload --fqbn "esp32:esp32:esp32c3:FlashMode=dio,FlashFreq=40,FlashSize=4M,CDCOnBoot=default,UploadSpeed=115200" --port COM8 --input-dir .artifacts/esp32c3-range extras/esp32c3-range-test/PN532_All_Cards_UART_Range_Test
```

## Recorded validation scope

The original 11 November 2026 test record reports an ESP32 core 3.3.8 compile,
20 host-side simulated checks, and a live LuatOS CORE-ESP32-C3 test with a reader
reporting firmware 1.6. In AUTO + ID_ONLY, one Type A / SAK=18 sample was detected
42 times in 12 seconds with zero errors and zero initialization retries.

That record verifies the exercised Type A identification path. Type B, FeliCa,
Jewel/Topaz and ST SR paths still need the appropriate real cards. Simulated
PN532 frames do not substitute for card-distance measurements. The GPIO12 LED
drive was checked in code/logs, without camera confirmation of the physical LED.
These recorded checks do not validate every module, card or protocol combination.

## References

- [ELECHOUSE modules and product documentation](../../README.md#elechouse-modules)
- [Compatible PN532 command reference](https://www.nxp.com/docs/en/user-guide/141520.pdf)
- [NXP MIFARE card identification AN10833](https://www.nxp.com/docs/en/application-note/AN10833.pdf)
- [NTAG213/215/216 READ and GET_VERSION](https://www.nxp.com/docs/en/data-sheet/NTAG213_215_216.pdf)
- [ST SRI/SRT/ST25TB tool reference](https://www.st.com/resource/en/user_manual/um1084-cr95hf-development-software-stmicroelectronics.pdf)
- [libnfc PN53x implementation used for protocol cross-checking](https://github.com/nfc-tools/libnfc/blob/master/libnfc/chips/pn53x.c)

The test driver implements its protocol handling independently; libnfc source
was not copied into the firmware. Chip identity and hardware capabilities are
defined by each module's product documentation.
