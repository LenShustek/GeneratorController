//file: generator_hw.h

//*****  hardware pin definitions

//... #define PCB13 // older PC board?

#define GEN_BUTTON_PIN 4    // pushbuttons, which need internal pullups
#define MENU_BUTTON_PIN 5
#define LEFT_BUTTON_PIN 0
#define RIGHT_BUTTON_PIN 1
#define DOWN_BUTTON_PIN 2
#define UP_BUTTON_PIN 3
#define ATHOME_BUTTON_PIN 25
#define NUM_BUTTONS 7

#define CONNECT_GEN_RELAY 23  // relay that commands "connect to generator"
#define RUN_GEN_RELAY 24      // relay that commands "run generator"
#define RELAY_ON HIGH
#define RELAY_OFF LOW

#define WIFI_CS 19            // WiFi chip select (aka SS)
#define WIFI_BUSY 20          // WiFi busy (aka ACK)
#define WIFI_RST 21           // WiFi reset
#define WIFI_SCK 13           // WiFi clock (implied by SPI0)
#define WIFI_MISO 12          // WiFi master in, slave out (implied by SPI0)
#define WIFI_MOSI 11          // WiFi master out, slave in (implied by SPI0)
#define WIFI_GPI00 30         // WIFI general purpose I/O (used for firmware update)
#define WIFI_SERIAL Serial4   // WiFi serial I/O  (used for firmware update)

#ifdef PCB13
#define WIFI_LED 10           // WiFi connected LED, old style, high=off
#define WIFI_LED_ON LOW
#define WIFI_LED_OFF HIGH
#else
#define WIFI_LED 27           // WiFi connected LED, high=on
#define WIFI_LED_ON HIGH
#define WIFI_LED_OFF LOW
#endif

#define ATHOME_LED 26         // "At home" LED, high=on
#define ATHOME_LED_ON HIGH
#define ATHOME_LED_OFF LOW

#define LCD_RS 39   // LCD display pins
#define LCD_EN 38
#define LCD_D4 37
#define LCD_D5 36
#define LCD_D6 35
#define LCD_D7 34

#define POWER_DISPLAY_WIFI 33 // low to power on the display and WiFi processor, else high-Z

// The following inputs now need internal pullups because they
// are connected to the open collectors of the optoisolatorss
#define GEN_CONNECTED_PIN 9   // input low if connected to generator
#define UTIL_CONNECTED_PIN 8  // input low if connected to utility
#define GEN_ON_PIN 7          // input low if generator is on
#define UTIL_ON_PIN 6         // input low if utility power is on

#define ANALOG_REF 3.30       // analog reference voltage is the supply voltage

#define BATT_VOLTAGE A0       // analog battery voltage, 0 to 3.02 VDC is 0 to 14 VDC
#define BATT_VOLTAGE_ADJ 0.17f  // adjustment to compensate for approx. drop across choke L1
#define BATT_EXAMPLE 14.0f
#define BATT_ANALOG 3.02f

#define LOAD_CURRENT1 A1      // analog load current phase 1, 0 to 3.15 VDC is 0 to 100A VAC
#define LOAD_CURRENT2 A2      // analog load current phase 2, 0 to 3.15 VDC is 0 to 100A VAC
#define CURRENT_EXAMPLE 100.0f
#define CURRENT_ANALOG 3.15f

#define UTIL_VOLTAGE A3       // analog utility   voltage, 0 to 2.88 VDC is 0 to 240 VAC
#define GEN_VOLTAGE A4        // analog generator voltage, 0 to 2.88 VDC is 0 to 240 VAC
#define VOLTAGE_EXAMPLE 250.0f
#define VOLTAGE_ANALOG 3.21f

//*
