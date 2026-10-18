
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// =====================================================
// PS2 -> USB HID Gamepad
// Raspberry Pi Pico RP2040 + Sipeed PS2 breakout
// =====================================================

// Sipeed connections
#define PS2_MISO 16
#define PS2_CSN  17
#define PS2_SCLK 18
#define PS2_MOSI 19

// Set false once debugging is complete.
// Adds HID button 13 for analog and 16 for digital.
constexpr bool DEBUG_MODE_BUTTONS = false;

// =====================================================
// USB HID descriptor
// 4 axes + D-pad hat + 16 buttons
// =====================================================

uint8_t const hid_descriptor[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),

    // Analog axes
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_X),
    HID_USAGE(HID_USAGE_DESKTOP_Y),
    HID_USAGE(HID_USAGE_DESKTOP_RX),
    HID_USAGE(HID_USAGE_DESKTOP_RY),

    HID_LOGICAL_MIN(0x81),
    HID_LOGICAL_MAX(0x7F),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(4),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    // D-pad hat
    HID_USAGE(HID_USAGE_DESKTOP_HAT_SWITCH),
    HID_LOGICAL_MIN(1),
    HID_LOGICAL_MAX(8),
    HID_PHYSICAL_MIN(0),
    HID_PHYSICAL_MAX_N(315, 2),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_DATA | HID_VARIABLE |
              HID_ABSOLUTE | HID_NULL_STATE),

    // Buttons
    HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
    HID_USAGE_MIN(1),
    HID_USAGE_MAX(16),
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_PHYSICAL_MIN(0),
    HID_PHYSICAL_MAX(1),
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(16),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

    HID_COLLECTION_END
};

Adafruit_USBD_HID usb_hid;

// =====================================================
// PS2 controller state
// =====================================================

struct PS2State {
    uint16_t buttons = 0;

    uint8_t lx = 128;
    uint8_t ly = 128;
    uint8_t rx = 128;
    uint8_t ry = 128;

    uint8_t mode = 0;
};

PS2State pad;

// =====================================================
// PS2 commands
// =====================================================

const uint8_t POLL[21] = {
    0x01, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

const uint8_t ENTER_CONFIG[9] = {
    0x01, 0x43, 0x00, 0x01, 0x5A,
    0x5A, 0x5A, 0x5A, 0x5A
};

const uint8_t SET_ANALOG[9] = {
    0x01, 0x44, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00
};

const uint8_t EXIT_CONFIG[9] = {
    0x01, 0x43, 0x00, 0x00, 0x5A,
    0x5A, 0x5A, 0x5A, 0x5A
};

// =====================================================
// Low-level PS2 communication
// =====================================================

// LSB-first synchronous serial communication.
// Preserves our experimentally verified timing.

uint8_t transferPS2(uint8_t data) {
    uint8_t result = 0;

    for (int bit = 0; bit < 8; bit++) {
        digitalWrite(PS2_MOSI, (data >> bit) & 1);
        delayMicroseconds(3);

        digitalWrite(PS2_SCLK, LOW);
        delayMicroseconds(10);

        digitalWrite(PS2_SCLK, HIGH);
        delayMicroseconds(3);

        if (digitalRead(PS2_MISO)) {
            result |= (1u << bit);
        }

        delayMicroseconds(6);
    }

    return result;
}

void ps2Transaction(
    const uint8_t *tx,
    uint8_t *rx,
    size_t length
) {
    digitalWrite(PS2_CSN, LOW);
    delayMicroseconds(20);

    for (size_t i = 0; i < length; i++) {
        rx[i] = transferPS2(tx[i]);
        delayMicroseconds(20);
    }

    digitalWrite(PS2_CSN, HIGH);
}

// =====================================================
// Configure analog mode, unlocked
// =====================================================

void configurePS2() {
    uint8_t response[9];

    // Wake controller
    for (int i = 0; i < 3; i++) {
        ps2Transaction(POLL, response, 5);
        delay(20);
    }

    // Enter configuration
    ps2Transaction(ENTER_CONFIG, response, 9);
    delay(20);

    // Enable analog mode, unlocked
    ps2Transaction(SET_ANALOG, response, 9);
    delay(20);

    // Exit configuration
    ps2Transaction(EXIT_CONFIG, response, 9);
    delay(20);
}

// =====================================================
// Read controller
// =====================================================

bool pollPS2() {
    uint8_t response[21] = {};

    digitalWrite(PS2_CSN, LOW);
    delayMicroseconds(20);

    // Read standard 5-byte response
    for (int i = 0; i < 5; i++) {
        response[i] = transferPS2(POLL[i]);
        delayMicroseconds(20);
    }

    uint8_t mode = response[1];
    size_t length = 5;

    if (mode == 0x73) {
        length = 9;
    } else if (mode == 0x79) {
        length = 21;
    }

    // Read remaining analog bytes
    for (size_t i = 5; i < length; i++) {
        response[i] = transferPS2(POLL[i]);
        delayMicroseconds(20);
    }

    digitalWrite(PS2_CSN, HIGH);

    // -----------------------------------------------
    // FIX: Physical ANALOG button may cause 0x00
    // instead of 0x5A in the third response byte.
    // -----------------------------------------------

    bool validMode =
        mode == 0x41 ||
        mode == 0x73 ||
        mode == 0x79;

    bool validHeader =
        response[0] == 0xFF &&
        (response[2] == 0x5A ||
         response[2] == 0x00);

    if (!validMode || !validHeader) {
        return false;
    }

    pad.mode = mode;

    // Button bits are active-low
    uint16_t raw =
        uint16_t(response[3]) |
        (uint16_t(response[4]) << 8);

    pad.buttons = uint16_t(~raw);

    // Analog mode
    if (mode == 0x73 || mode == 0x79) {
        pad.rx = response[5];
        pad.ry = response[6];
        pad.lx = response[7];
        pad.ly = response[8];
    }
    // Digital mode
    else {
        pad.rx = 128;
        pad.ry = 128;
        pad.lx = 128;
        pad.ly = 128;
    }

    return true;
}

// =====================================================
// Button helpers
// =====================================================

bool pressed(uint8_t bit) {
    return (pad.buttons & (1u << bit)) != 0;
}

// D-pad -> HID hat

uint8_t getHat() {
    bool up    = pressed(4);
    bool right = pressed(5);
    bool down  = pressed(6);
    bool left  = pressed(7);

    if (up && right)   return 2;
    if (down && right) return 4;
    if (down && left)  return 6;
    if (up && left)    return 8;

    if (up)    return 1;
    if (right) return 3;
    if (down)  return 5;
    if (left)  return 7;

    return 0;
}

// Convert PS2 axis to signed HID axis

int8_t toHidAxis(uint8_t value) {
    int v = int(value) - 128;

    if (v < -127) v = -127;

    return (int8_t)v;
}

// =====================================================
// USB HID report
// =====================================================

void sendGamepadReport() {
    if (!usb_hid.ready()) return;

    uint16_t buttons = 0;

    // Face buttons - corrected mapping
    if (pressed(14)) buttons |= (1u << 0);  // Cross
    if (pressed(13)) buttons |= (1u << 1);  // Circle
    if (pressed(15)) buttons |= (1u << 4);  // Square
    if (pressed(12)) buttons |= (1u << 3);  // Triangle

    // Shoulder buttons
    if (pressed(10)) buttons |= (1u << 6);  // L1
    if (pressed(11)) buttons |= (1u << 7);  // R1
    if (pressed(8))  buttons |= (1u << 8);  // L2
    if (pressed(9))  buttons |= (1u << 9);  // R2

    // System buttons
    if (pressed(0)) buttons |= (1u << 10);  // Select
    if (pressed(3)) buttons |= (1u << 11);  // Start
    if (pressed(1)) buttons |= (1u << 13);  // L3
    if (pressed(2)) buttons |= (1u << 14);  // R3

    // Temporary indicators for debugging
    if (DEBUG_MODE_BUTTONS) {
        if (pad.mode == 0x73 || pad.mode == 0x79) {
            buttons |= (1u << 12);
        }

        if (pad.mode == 0x41) {
            buttons |= (1u << 15);
        }
    }

    // Exact 7-byte HID report
    uint8_t report[7] = {
        uint8_t(toHidAxis(pad.lx)),
        uint8_t(toHidAxis(pad.ly)),
        uint8_t(toHidAxis(pad.rx)),
        uint8_t(toHidAxis(pad.ry)),
        getHat(),
        uint8_t(buttons & 0xFF),
        uint8_t(buttons >> 8)
    };

    usb_hid.sendReport(0, report, sizeof(report));
}

// =====================================================
// Setup
// =====================================================

void setup() {
    // PS2 GPIO
    pinMode(PS2_MISO, INPUT_PULLUP);
    pinMode(PS2_CSN, OUTPUT);
    pinMode(PS2_SCLK, OUTPUT);
    pinMode(PS2_MOSI, OUTPUT);

    digitalWrite(PS2_CSN, HIGH);
    digitalWrite(PS2_SCLK, HIGH);
    digitalWrite(PS2_MOSI, HIGH);

    // Initialize USB HID
    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
    }

    usb_hid.setPollInterval(2);
    usb_hid.setReportDescriptor(
        hid_descriptor,
        sizeof(hid_descriptor)
    );
    usb_hid.begin();

    // Initialize controller
    delay(500);
    configurePS2();
}

// =====================================================
// Main loop
// =====================================================

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
    TinyUSBDevice.task();
#endif

    static uint32_t lastPoll = 0;

    // Poll every 10 ms
    if (millis() - lastPoll < 10) return;
    lastPoll = millis();

    if (!pollPS2()) {
        // Neutral state on communication failure
        pad = PS2State{};
    }

    sendGamepadReport();
}
