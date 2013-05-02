/* OSCBoards.h - Hardware Abstraction Layer for OSCuino library */

#ifndef Oscuino_Boards_h
#define Oscuino_Boards_h

#include <inttypes.h>

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"	// for digitalRead, digitalWrite, etc
#else
#include "WProgram.h"
#endif


/*
    OSCuino Hardware Abstraction Layer
	
	Derived with appreciation from the Firmata Boards.h
	 by Adrian Freed 2013
	Why so many Boards.h files?
		Arduino.h  can't keep up with new non-arduino boards.
		Firmata inherited many of the problems of MIDI
			that OSC carefully avoids,
			 e.g. limits on the number of pins
                Needed to add capabilities:
                (temperature and power supply, board name)
                USB ports
                Mac Address
                Max Baud Rate
	
	

Oscuino is built on top of the hardware abstraction functions of Arduino
and Teensy, specifically digitalWrite, digitalRead, analogWrite, analogRead, and 
pinMode.  Edit this file to add new board support for OSCuino.

General Constants:

These constants provide basic information Firmata requires.

TOTAL_PINS: The total number of pins.
    Usually this will match the number of pins the Arduino functions
    implement, including any pins pins capable of analog or digital.
    For example onn Arduino Mini with 8 analog inputs, 6 of these may be used
    for digital functions, and 2 are analog only.  
	
TOTAL_ANALOG_PINS: The total number of analog input pins implemented.




Pin Mapping Macros:

These macros provide the mapping between pins as implemented by
Oscuino protocol and the actual pin numbers used by the Arduino
functions.  


IS_PIN_XXXX(pin): The IS_PIN macros resolve to true or non-zero
    if a pin as implemented by OSCuino corresponds to a pin
    that actually implements the named feature.

PIN_TO_XXXX(pin): The PIN_TO macros translate pin numbers as
    implemented by OSCuino to the pin numbers needed as inputs
    to the Arduino functions.  The corresponding IS_PIN macro
    should always be tested before using a PIN_TO macro, so
    these macros only need to handle valid Firmata pin
    numbers for the named feature.


Port Access Inline Funtions:

For efficiency (on ATMEL) , OSCuino  provides access to digital
input and output pins grouped by 8 bit ports.  When these
groups of 8 correspond to actual 8 bit ports as implemented
by the hardware, these inline functions can provide high
speed direct port access.  Otherwise, a default implementation
using 8 calls to digitalWrite or digitalRead is used.

When porting OSCuino to a new board, it is recommended to
use the default functions first and focus only on the constants
and macros above.  When those are working, if optimized port
access is desired, these inline functions may be extended.
The recommended approach defines a symbol indicating which
optimization to use, and then conditional complication is
used within these functions.

readPort(port, bitmask):  Read an 8 bit port, returning the value.
   port:    The port number, Firmata pins port*8 to port*8+7
   bitmask: The actual pins to read, indicated by 1 bits.

writePort(port, value, bitmask):  Write an 8 bit port.
   port:    The port number, Firmata pins port*8 to port*8+7
   value:   The 8 bit value to write
   bitmask: The actual pins to write, indicated by 1 bits.
*/

/*==============================================================================
 * Board Specific Configuration
 *============================================================================*/

#ifndef digitalPinHasPWM
#define digitalPinHasPWM(p)     OSC_IS_PIN_DIGITAL(p)
#endif

// Arduino Duemilanove, Diecimila, and NG
#if defined(__AVR_ATmega168__) || defined(__AVR_ATmega328P__)
#if defined(NUM_ANALOG_INPUTS) && NUM_ANALOG_INPUTS == 6
#define OSC_TOTAL_ANALOG_PINS       6
#define OSC_TOTAL_PINS              20 // 14 digital + 6 analog
#else
#define OSC_TOTAL_ANALOG_PINS       8
#define OSC_TOTAL_PINS              22 // 14 digital + 8 analog
#endif
#define OSC_LED_PIN       13
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 2 && (p) <= 19)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 14 && (p) < 14 + OSC_TOTAL_ANALOG_PINS)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         (OSC_IS_PIN_DIGITAL(p) && (p) - 2 < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 18 || (p) == 19)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - 14)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         ((p) - 2)
#define OSC_ARDUINO_PINOUT_OPTIMIZE 1


// Wiring (and board)
#elif defined(WIRING)
#define OSC_LED_PIN       WLED
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 0 && (p) < TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= FIRST_ANALOG_PIN && (p) < (FIRST_ANALOG_PIN+OSC_TOTAL_ANALOG_PINS))
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == SDA || (p) == SCL)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - FIRST_ANALOG_PIN)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         (p) 


// old Arduinos
#elif defined(__AVR_ATmega8__)
#define OSC_TOTAL_ANALOG_PINS       6
#define OSC_TOTAL_PINS              20 // 14 digital + 6 analog
#define OSC_LED_PIN       13
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 2 && (p) <= 19)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 14 && (p) <= 19)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         (OSC_IS_PIN_DIGITAL(p) && (p) - 2 < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 18 || (p) == 19)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - 14)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         ((p) - 2)
#define OSC_ARDUINO_PINOUT_OPTIMIZE 1


// Arduino Mega
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
#define OSC_TOTAL_ANALOG_PINS       16
#define OSC_TOTAL_PINS              70 // 54 digital + 16 analog
#define OSC_LED_PIN       13
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 2 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 54 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 2 && (p) - 2 < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 20 || (p) == 21)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - 54)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         ((p) - 2)


// Teensy 1.0
#elif defined(__AVR_AT90USB162__)
#define OSC_TOTAL_ANALOG_PINS       0
#define OSC_TOTAL_PINS              21 // 21 digital + no analog
#define OSC_LED_PIN       6
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 0 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        (0)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           (0)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        (0)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         (p)


// Teensy 2.0
#elif defined(__AVR_ATmega32U4__)
#define OSC_TOTAL_ANALOG_PINS       12
#define OSC_TOTAL_PINS              25 // 11 digital + 12 analog
#define OSC_LED_PIN       11
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 0 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 11 && (p) <= 22)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 5 || (p) == 6)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        (((p)<22)?21-(p):11)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         (p)


// Teensy++ 1.0 and 2.0
#elif defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)
#define OSC_TOTAL_ANALOG_PINS       8
#define OSC_TOTAL_PINS              46 // 38 digital + 8 analog
#define OSC_LED_PIN       6
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 0 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 38 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 0 || (p) == 1)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - 38)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         (p)

// Teensy 3.0
#elif defined(__MK20DX128__)
#define OSC_TOTAL_ANALOG_PINS       14
#define OSC_TOTAL_PINS              38 // 24 digital + 10 analog-digital + 4 analog
#define OSC_LED_PIN       13
#define OSC_IS_PIN_TOUCH(p)        ((p)==33 ||(p)==32 ||(p)==25 ||(p)==1 ||(p)==0 ||(p)==22 ||(p)==23 || ((p)<=19 && (p)>15))
#define OSC_PIN_TO_TOUCH(p)        (p)
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 0 && (p) <= 34)
#define OSC_IS_PIN_ANALOG(p)        (((p) >= 14 && (p) <= 23) || ((p) >= 34 && (p) <= 38))
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 18 || (p) == 19)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        (((p)<=23)?(p)-14:(p)-24)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         (p)
// Leonardo
#elif defined(__AVR_ATmega32U4__)
#define OSC_TOTAL_ANALOG_PINS 12
#define OSC_TOTAL_PINS 30 // 14 digital + 12 analog + 4 SPI (D14-D17 on ISP header)
#define OSC_LED_PIN 13
#define OSC_IS_PIN_DIGITAL(p) ((p) >= 0 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p) ((p) >= 18 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_PWM(p) ((p) == 3 || (p) == 5 || (p) == 6 || (p) == 9 || (p) == 10 || (p) == 11 || (p) == 13)
#define OSC_IS_PIN_SERVO(p) ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p) ((p) == 2 || (p) == 3)
#define OSC_PIN_TO_DIGITAL(p) (p)
#define OSC_PIN_TO_ANALOG(p) (p) - 18
#define OSC_PIN_TO_PWM(p) OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p) (p) 

// Sanguino
#elif defined(__AVR_ATmega644P__) || defined(__AVR_ATmega644__)
#define OSC_TOTAL_ANALOG_PINS       8
#define OSC_TOTAL_PINS              32 // 24 digital + 8 analog
#define OSC_LED_PIN       0
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 2 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 24 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 16 || (p) == 17)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - 24)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         ((p) - 2)


// Illuminato
#elif defined(__AVR_ATmega645__)
#define OSC_TOTAL_ANALOG_PINS       6
#define OSC_TOTAL_PINS              42 // 36 digital + 6 analog
#define OSC_LED_PIN       13
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 2 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 36 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 0 && (p) < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 4 || (p) == 5)
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - 36)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         ((p) - 2)

// Arduino DUE
#elif defined(__SAM3X8E__)
#define OSC_TOTAL_ANALOG_PINS       12
#define OSC_TOTAL_PINS              66 // 54 digital + 12 analog
#define OSC_LED_PIN       13
#define OSC_IS_PIN_DIGITAL(p)       ((p) >= 2 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_ANALOG(p)        ((p) >= 54 && (p) < OSC_TOTAL_PINS)
#define OSC_IS_PIN_PWM(p)           digitalPinHasPWM(p)
#define OSC_IS_PIN_SERVO(p)         ((p) >= 2 && (p) - 2 < OSC_MAX_SERVOS)
#define OSC_IS_PIN_I2C(p)           ((p) == 20 || (p) == 21) // 70 71
#define OSC_PIN_TO_DIGITAL(p)       (p)
#define OSC_PIN_TO_ANALOG(p)        ((p) - 54)
#define OSC_PIN_TO_PWM(p)           OSC_PIN_TO_DIGITAL(p)
#define OSC_PIN_TO_SERVO(p)         ((p) - 2)
// anything else
#else
#error "Please edit OSCBoards.h with a hardware abstraction for this board"
#endif


/*==============================================================================
 * readPort() - Read an 8 bit port
 *============================================================================*/

static inline unsigned char readPort(byte, byte) __attribute__((always_inline, unused));
static inline unsigned char readPort(byte port, byte bitmask)
{
#if defined(OSC_ARDUINO_PINOUT_OPTIMIZE)
	if (port == 0) return (PIND & 0xFC) & bitmask; // ignore Rx/Tx 0/1
	if (port == 1) return ((PINB & 0x3F) | ((PINC & 0x03) << 6)) & bitmask;
	if (port == 2) return ((PINC & 0x3C) >> 2) & bitmask;
	return 0;
#else
	unsigned char out=0, pin=port*8;
	if (OSC_IS_PIN_DIGITAL(pin+0) && (bitmask & 0x01) && digitalRead(OSC_PIN_TO_DIGITAL(pin+0))) out |= 0x01;
	if (OSC_IS_PIN_DIGITAL(pin+1) && (bitmask & 0x02) && digitalRead(OSC_PIN_TO_DIGITAL(pin+1))) out |= 0x02;
	if (OSC_IS_PIN_DIGITAL(pin+2) && (bitmask & 0x04) && digitalRead(OSC_PIN_TO_DIGITAL(pin+2))) out |= 0x04;
	if (OSC_IS_PIN_DIGITAL(pin+3) && (bitmask & 0x08) && digitalRead(OSC_PIN_TO_DIGITAL(pin+3))) out |= 0x08;
	if (OSC_IS_PIN_DIGITAL(pin+4) && (bitmask & 0x10) && digitalRead(OSC_PIN_TO_DIGITAL(pin+4))) out |= 0x10;
	if (OSC_IS_PIN_DIGITAL(pin+5) && (bitmask & 0x20) && digitalRead(OSC_PIN_TO_DIGITAL(pin+5))) out |= 0x20;
	if (OSC_IS_PIN_DIGITAL(pin+6) && (bitmask & 0x40) && digitalRead(OSC_PIN_TO_DIGITAL(pin+6))) out |= 0x40;
	if (OSC_IS_PIN_DIGITAL(pin+7) && (bitmask & 0x80) && digitalRead(OSC_PIN_TO_DIGITAL(pin+7))) out |= 0x80;
	return out;
#endif
}

/*==============================================================================
 * writePort() - Write an 8 bit port, only touch pins specified by a bitmask
 *============================================================================*/

static inline unsigned char writePort(byte, byte, byte) __attribute__((always_inline, unused));
static inline unsigned char writePort(byte port, byte value, byte bitmask)
{
#if defined(OSC_ARDUINO_PINOUT_OPTIMIZE)
	if (port == 0) {
		bitmask = bitmask & 0xFC;  // do not touch Tx & Rx pins
		byte valD = value & bitmask;
		byte maskD = ~bitmask;
		cli();
		PORTD = (PORTD & maskD) | valD;
		sei();
	} else if (port == 1) {
		byte valB = (value & bitmask) & 0x3F;
		byte valC = (value & bitmask) >> 6;
		byte maskB = ~(bitmask & 0x3F);
		byte maskC = ~((bitmask & 0xC0) >> 6);
		cli();
		PORTB = (PORTB & maskB) | valB;
		PORTC = (PORTC & maskC) | valC;
		sei();
	} else if (port == 2) {
		bitmask = bitmask & 0x0F;
		byte valC = (value & bitmask) << 2;
		byte maskC = ~(bitmask << 2);
		cli();
		PORTC = (PORTC & maskC) | valC;
		sei();
	}
#else
	byte pin=port*8;
	if ((bitmask & 0x01)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+0), (value & 0x01));
	if ((bitmask & 0x02)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+1), (value & 0x02));
	if ((bitmask & 0x04)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+2), (value & 0x04));
	if ((bitmask & 0x08)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+3), (value & 0x08));
	if ((bitmask & 0x10)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+4), (value & 0x10));
	if ((bitmask & 0x20)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+5), (value & 0x20));
	if ((bitmask & 0x40)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+6), (value & 0x40));
	if ((bitmask & 0x80)) digitalWrite(OSC_PIN_TO_DIGITAL(pin+7), (value & 0x80));
#endif
}




#ifndef OSC_TOTAL_PORTS
#define OSC_TOTAL_PORTS             ((OSC_TOTAL_PINS + 7) / 8)
#endif


#endif /* OSC_Boards_h */

