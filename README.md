# RP2040 PS2-to-USB Adapter

A DIY PlayStation-style controller to USB HID gamepad adapter built with a **Raspberry Pi Pico (RP2040)**, a **Sipeed PS2 controller connector breakout**, and **Arduino**.

The goal is a small, inexpensive, open-source adapter that preserves the original controller and progressively adds support for its features. The firmware communicates directly with the controller over its SPI-like serial interface; no PS2 controller library is used.

> **Status:** Working USB HID gamepad with digital controls and physical ANALOG-button mode switching. **Full analog support is future work.** Basic analog-stick data is passed through in the current firmware, but analog support is not yet considered complete or finalized.

## Features

### Implemented and tested

- USB HID gamepad on RP2040, using **Adafruit TinyUSB**.
- Direct PS2 controller communication over GPIO (LSB-first, software-timed serial).
- Digital buttons, D-pad, Start/Select, and L3/R3 reported over USB.
- Physical **ANALOG** button switches the controller between digital and analog modes, without USB reconnection.
- Mode-aware polling (5-byte digital and 9-byte standard analog responses).
- Compatibility handling for both `0x5A` and `0x00` as the controller's third response byte after mode changes, as observed during testing.

### Experimental / future work

- **Full analog support**, including validation, calibration, dead zones, and mapping refinement for both sticks. The current firmware already contains basic four-axis HID reporting, but this is not yet a finished feature.
- Broader compatibility testing across controllers and operating systems.
- More efficient serial communication using the RP2040's PIO and/or DMA.
- Improved error handling and automatic controller recovery.

Pressure-sensitive buttons and vibration feedback are **not implemented**. The current six-pin Sipeed breakout does not expose a separate vibration-motor power connection; no rumble support is claimed.

## Hardware

- Raspberry Pi Pico or compatible **RP2040** board (tested with a USB-C Pico 1-compatible clone)
- Sipeed PS2 controller connector breakout with **GND, 3V3, CSN, MISO, MOSI, SCLK** pins
- Compatible PlayStation controller
- Jumper wires and USB data cable

### Wiring

| Sipeed breakout | RP2040 GPIO / supply | Pico physical pin |
| --- | --- | --- |
| GND | GND | 23 |
| 3V3 | 3V3(OUT) | 36 |
| CSN | GP17 | 22 |
| MISO | GP16 | 21 |
| MOSI | GP19 | 25 |
| SCLK | GP18 | 24 |

**Use 3.3 V logic and power**, not 5 V. Identify pins by the labels on your actual breakout; connector variants may have different pin arrangements.

## Building and flashing

1. Install **Arduino IDE** and the **Raspberry Pi Pico/RP2040/RP2350** board core by Earle F. Philhower III.
2. Install the **Adafruit TinyUSB Library**.
3. Select your RP2040 board (for a standard Pico-compatible clone, typically **Raspberry Pi Pico**).
4. Select **Tools → USB Stack → Adafruit TinyUSB**.
5. Open the project `.ino` sketch, compile, and upload to the Pico.
6. Connect the Sipeed breakout according to the wiring table above and plug the controller into it.

No separate PS2 controller library is needed: the PS2 serial driver is part of this project's firmware.

## Using the adapter

Connect the Pico to a computer via USB. It should enumerate as a **generic USB HID gamepad**. The controller's physical **ANALOG** button can switch its mode; the USB connection stays active.

On Linux, you can test inputs with `jstest` (from the `joystick` package) or another gamepad tester. Operating-system and application button labels may vary because this is a generic HID device, not an official Sony or Xbox controller.

The code contains a temporary `DEBUG_MODE_BUTTONS` option that exposes mode indicators as additional HID buttons. Disable this option for ordinary use.

## Implementation notes

The controller uses an SPI-like, LSB-first synchronous interface. The first implementation uses GPIO bit-banging with timing established through hardware testing. The USB report uses **four signed axes**, a **D-pad hat**, and **16 button bits**.

The firmware accepts these observed controller modes:

| Controller ID | Mode |
| --- | --- |
| `0x41` | Digital |
| `0x73` | Analog |
| `0x79` | Extended analog (basic handling only; not fully supported) |

This is an evolving hobby project; behavior may vary with controller models and third-party clones.

## AI assistance acknowledgment

**This project was developed with assistance from OpenAI's ChatGPT.** AI assistance was used to interpret the controller protocol, draft and refine Arduino firmware, troubleshoot communication timing and HID mappings, and prepare documentation. Hardware wiring, on-device testing, iterative validation, and decisions about the project's direction were carried out by the project author. AI-generated code and suggestions were tested and corrected during development.

## License

A license has not yet been selected. Add a `LICENSE` file before specifying reuse terms for the project.
