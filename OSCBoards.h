//
//  OSCBoards.h
//  
//
//  Created by AdrianFreed on 5/26/13.
//
//

#ifndef _OSCBoards_h
#define _OSCBoards_h


#if defined(__MK20DX128__) ||  defined(__MK20DX256__)  || defined(__MKL26Z64__) || defined(__MK66FX1M0__)
// Teensy 3.0  3.1  3.1LC 3.2 3.6
#define BOARD_HAS_CAPACITANCE_SENSING
#endif

#if defined(__AVR_ATmega32U4__) || defined(__MKL26Z64__) || defined(__MK20DX128__)||  defined(__MK20DX256__)   || defined(__MK66FX1M0__) || defined(__AVR_ATmega328_) ||   defined(__AVR_ATmega128__)

#define BOARD_HAS_DIE_TEMPERATURE_SENSOR
#endif

#if defined(__AVR_ATmega32U4__) ||  defined(__MK20DX128__) ||  defined(__MK20DX256__)  || defined(__MK66FX1M0__) || defined(__MK66FX1M0__) ||  defined(__MKL26Z64__) || defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__) || defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__) || defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)    || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega328_) ||   defined(__AVR_ATmega128__)


#define BOARD_HAS_DIE_POWER_SUPPLY_MEASUREMENT
#endif


#if defined(__AVR_ATmega32U4__)  || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__) || defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__) || defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)    || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega328_) ||   defined(__AVR_ATmega128__)
#define BOARD_HAS_ANALOG_PULLUP
#endif

// The Oscuino examples guard their /tone handler with #ifdef BOARD_HAS_TONE,
// but nothing in the library ever defined it, so /tone was dead code in every
// one of them. Define it where tone()/noTone() actually exist.
//
// Checked by compiling a tone()/noTone() probe against every installed core:
// arduino:avr, arduino:megaavr, arduino:renesas_uno, arduino:samd,
// adafruit:samd, adafruit:nrf52, teensy:avr, esp32:esp32 and rp2040:rp2040 all
// provide it. arduino:sam (SAM3X8E / Due) is the one that does not.
// Define OSC_NO_TONE before including the library to force it off elsewhere.
#if !defined(OSC_NO_TONE) && !defined(__SAM3X8E__)
#define BOARD_HAS_TONE
#endif

// missing specs for PIC32

#if (defined(__PIC32MX__) || defined(__PIC32MZ__))
#define NUM_ANALOG_INPUTS NUM_ANALOG_PINS
#define NUM_DIGITAL_INPUTS NUM_DIGITAL_PINS
#define LED_BUILTIN PIN_LED1

#endif

// The Zephyr core (Arduino UNO Q and the zephyr_main boards) spells the pin
// count NUM_OF_DIGITAL_PINS and gives the analog pins an enum with no count
// sentinel at all, so the Oscuino-style examples that iterate pins do not
// compile there. Both counts come from the board's own devicetree rather than
// from a table here, so they follow the variant instead of needing one entry
// per board.
#if defined(ARDUINO_ARCH_ZEPHYR)
#ifndef NUM_DIGITAL_PINS
#define NUM_DIGITAL_PINS NUM_OF_DIGITAL_PINS
#endif
#ifndef NUM_ANALOG_INPUTS
#ifdef CONFIG_ADC
#define NUM_ANALOG_INPUTS DT_PROP_LEN(DT_PATH(zephyr_user), adc_pin_gpios)
#else
#define NUM_ANALOG_INPUTS 0
#endif
#endif
#endif

// Onboard LEDs. No Arduino core defines a capability macro for these — checked
// by grepping all 27 installed cores for anything shaped like ARDUINO_HAS_LED,
// zero hits — so the presence of the pin macro itself is the only signal there
// is. Established the same way as BOARD_HAS_TONE above: a #pragma message probe
// compiled against 42 boards, every one of which built.
//
// Only defined() is safe here. LED_BUILTIN is NOT always a preprocessor
// constant: esp32 variants declare it `static const uint8_t` and then write
// `#define LED_BUILTIN LED_BUILTIN` purely so #ifdef works (it evaluates to 0
// inside #if), and the Zephyr core builds it from the board's devicetree as a
// ~70-term expression. Compare values in C++, never in #if.
//
// Three boards this library has run on define no LED_BUILTIN at all — M5Dial,
// M5StampS3 and the LilyGO T-Display-S3 — which is what BOARD_HAS_LED is for.
// Guessing a pin there is not harmless: GPIO 13, the obvious guess, is SDA on
// the M5Dial and MISO on the T-Display-S3.
#if defined(LED_BUILTIN) && !defined(OSC_NO_LED)
#define BOARD_HAS_LED
#endif

// RGB onboard LEDs, in the four spellings the cores actually use. Ordered by
// how much the core does for you: RGB_BUILTIN is the only one with a driver
// behind it (esp32's rgbLedWrite(), which digitalWrite() also routes to), the
// rest are bare pin numbers wanting Adafruit_NeoPixel or equivalent.
//
// Treat a negative as "no macro said so", not as "no RGB LED". The M5Stack
// NanoC6 has a WS2812 whose pins its own variant names (RGB_LED_DATA_PIN 20,
// RGB_LED_PWR_PIN 19) while defining none of these four. It can also read
// positive on a board without one: m5stack_atoms3 declares RGB_BUILTIN = 48,
// but that variant is shared with the AtomS3 Lite and 48 is the Lite's pixel.
// OSC_NO_RGB forces it off; a sketch that knows its board should just say so.
//
// LED_RED/LED_GREEN/LED_BLUE are deliberately not consulted: on every UNO R4,
// the RGB-less Minima included, they leak in from the Renesas FSP's generated
// bsp_pin_cfg.h in FSP port-pin encoding, which is not an Arduino pin number.
#if !defined(OSC_NO_RGB)
#if defined(RGB_BUILTIN)
#define BOARD_HAS_RGB
#define BOARD_RGB_CORE_DRIVEN
#elif defined(PIN_NEOPIXEL)
#define BOARD_HAS_RGB
#define BOARD_RGB_NEOPIXEL
#elif defined(PIN_DOTSTAR_DATA) && (defined(PIN_DOTSTAR_CLOCK) || defined(PIN_DOTSTAR_CLK))
#define BOARD_HAS_RGB
#define BOARD_RGB_DOTSTAR
#elif defined(LEDR) && defined(LEDG) && defined(LEDB)
#define BOARD_HAS_RGB
#define BOARD_RGB_DISCRETE
#endif
#endif


#ifndef analogInputToDigitalPin
int analogInputToDigitalPin(int i);
#endif

#ifdef BOARD_HAS_DIE_TEMPERATURE_SENSOR
float getTemperature();
#endif

#ifdef BOARD_HAS_DIE_POWER_SUPPLY_MEASUREMENT
float getSupplyVoltage();
#endif

#endif
