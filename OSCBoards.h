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
