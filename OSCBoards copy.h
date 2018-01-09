//
//  OSCBoards.h
//  
//
//  Created by AdrianFreed on 5/26/13.
//
//

#ifndef _OSCBoards_h
#define _OSCBoards_h



#if defined(__MK20DX128__) ||  defined(__MK20DX256__)  || defined(__MKL26Z64__) // Teensy 3.0  3.1 (PIC32's might have some day) 3.1LC
#define BOARD_HAS_CAPACITANCE_SENSING
#endif

#if defined(__AVR_ATmega32U4__) || defined(__MKL26Z64__) || defined(__MK20DX128__)||  defined(__MK20DX256__) || defined(__AVR_ATmega328_) ||   defined(__AVR_ATmega128__)

#define BOARD_HAS_DIE_TEMPERATURE_SENSOR
#endif

#if defined(__AVR_ATmega32U4__) ||  defined(__MK20DX128__) ||  defined(__MK20DX256__) || defined(__MKL26Z64__) || defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__) || defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__) || defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)    || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega328_) ||   defined(__AVR_ATmega128__)


#define BOARD_HAS_DIE_POWER_SUPPLY_MEASUREMENT
#define BOARD_HAS_ANALOG_PULLUP
#endif

// missing specs for PIC32

#if defined(__PIC32MX__)
#define NUM_ANALOG_INPUTS NUM_ANALOG_PINS
#define NUM_DIGITAL_INPUTS NUM_DIGITAL_PINS
#define LED_BUILTIN PIN_LED1
#endif

// missing specs for Arduino Due
#if defined(__SAM3X8E__)

#ifndef analogInputToDigitalPin
#define analogInputToDigitalPin(p)  ((p < 12) ? (p) + 54 : -1)
#endif


#endif // defined(__SAM3X8E__)

// missing specs for Leonardo derived devices
#if ! defined(CORE_TEENSY) && (defined(__AVR_ATmega32U4__) && (!defined(LED_BUILTIN) || !defined(analogInputToDigitalPin)) )

#if F_CPU==16000000
// Leonardo and Esplora
#ifndef LED_BUILTIN
#define LED_BUILTIN  13
#endif
#ifndef analogInputToDigitalPin
#define analogInputToDigitalPin(p)  ((p < 12) ? (p) + 18 : -1)
#endif
#else // F_CPU
//Lilypad USB and Flora (8Mhz)
#ifndef LED_BUILTIN
#define LED_BUILTIN  13  // its 7 on Flora but how do we detect this?
#endif
#ifndef analogInputToDigitalPin
#define analogInputToDigitalPin(p)  ((p < 12) ? (p) + 18 : -1)
#endif
#endif //F_CPU
#endif

#endif

