# ELECHOUSE PN532 / PN5321 Arduino Library

A shared Arduino NFC library for ELECHOUSE PN532 and PN5321-compatible modules.
The same command API is used through I2C, SPI or HSU UART transports. Wiring,
power, logic levels and interface selection depend on the module.

## ELECHOUSE modules

| Product | Brief description |
| --- | --- |
| [PN532 V4](https://www.elechouse.com/product/pn532-nfc-rfid-module-v4/) | General-purpose module with an onboard PCB antenna. |
| [PN5321 MINI](https://www.elechouse.com/product/pn532-mini/) | Compact board with external ferrite-backed antennas. |
| [PN532 Evolution V1](https://www.elechouse.com/product/pn532-nfc-evolution-v1/) | Amplified reader with a large external antenna. |
| [PN532 with External Antenna](https://www.elechouse.com/product/pn532-nfc-rfid-module-w-external-antenna-updated-version/) | Separate antenna for enclosure or panel installation. |
| [PV532 V3.5](https://www.elechouse.com/product/pv532-nfc-rfid-module-v3-5/) | PN5321-compatible module with 3.3 V communication logic. |
| [PN532 USB Type-C](https://www.elechouse.com/product/pn532-nfc-usb-module/) | USB serial variant for PC applications; see note below. |

Follow each product page for specifications, pinouts and downloads. The USB
variant uses PC serial software or a USB-host transport; this Arduino library
does not provide a USB-host driver.

## Installation

Download [this repository's ZIP](https://github.com/wilson-elechouse/PN5321/archive/refs/heads/arduino.zip)
and install it as an Arduino library, or clone it into your sketchbook's
`libraries/PN532-Arduino` folder:

```sh
git clone --branch arduino https://github.com/wilson-elechouse/PN5321.git PN532-Arduino
```

The folder must contain `library.properties` and `src/`. Some examples require
[Don's NDEF library](https://github.com/don/NDEF), which is declared as a package
dependency. The `PN532-Arduino` package name and existing class/header names
are retained for software compatibility.

## Interface selection

- I2C: `PN532_I2C.h` with `NFC_INTERFACE_I2C`.
- SPI: `PN532_SPI.h` with `NFC_INTERFACE_SPI`.
- Hardware UART: `PN532_HSU.h` with `NFC_INTERFACE_HSU`.
- Software UART: `PN532_SWHSU.h` with `NFC_INTERFACE_SWHSU`, on supported boards.

Select the matching hardware mode on your module and use its specified signal
levels. The build definition must reach the transport `.cpp` files; a definition
only in the sketch does not enable separately compiled transport code. For a
new Arduino CLI sketch, for example:

```sh
arduino-cli compile --fqbn <your-board-fqbn> --build-property "compiler.cpp.extra_flags=-DNFC_INTERFACE_HSU" <your-sketch-folder>
```

The [existing library examples](examples) explicitly include their selected
transport implementation. Follow each example's interface-selection block and
adjust host pins for your board. Tag memory access, NDEF, authentication,
peer-to-peer operation and card emulation depend on the selected example and card.

## ESP32-C3 UART card range test

The optional [standalone test and guide](extras/esp32c3-range-test/README.md)
provide card identification, manual distance labels, 20-attempt statistics and
CSV output. This separate example includes its own HSU driver and needs no
external NFC or NDEF library. Its reference host pins and LED assignments are
specific to the documented ESP32-C3 setup; check the guide before using it
with another board or module.

## Attribution and license

This ELECHOUSE-maintained fork retains the work of
[Adafruit](https://github.com/adafruit/Adafruit_NFCShield_I2C),
[Seeed Studio](https://github.com/Seeed-Studio/PN532) and the original contributors,
including Baozhu Zuo, Yihui Xiong, awieser and Jiapeng Li. Don's NDEF library is
a separate dependency. See [LICENSE.md](LICENSE.md) for the BSD 3-Clause license
and original copyright notices.
