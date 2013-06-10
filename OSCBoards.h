//
//  OSCBoards.h
//  
//
//  Created by AdrianFreed on 5/26/13.
//
//

#ifndef _OSCBoards_h
#define _OSCBoards_h





#if defined(__SAM3X8E__)  
// Arduino Due 1.5.2 betas have no Tone yet
// so this was pulled from here: http://forum.arduino.cc/index.php?PHPSESSID=q1137107qo27jv0kenicha8g17&topic=136500.msg1029238#msg1029238
#include <Tone.h>
/*
 Tone generator
 v1  use timer, and toggle any digital pin in ISR
 funky duration from arduino version
 TODO use FindMckDivisor?
 timer selected will preclude using associated pins for PWM etc.
 could also do timer/pwm hardware toggle where caller controls duration
 */


// timers TC0 TC1 TC2   channels 0-2 ids 0-2  3-5  6-8     AB 0 1
// use TC1 channel 0
#define TONE_TIMER TC1
#define TONE_CHNL 0
#define TONE_IRQ TC3_IRQn

// TIMER_CLOCK4   84MHz/128 with 16 bit counter give 10 Hz to 656KHz
//  piano 27Hz to 4KHz

static uint8_t pinEnabled[PINS_COUNT];
static uint8_t TCChanEnabled = 0;
static boolean pin_state = false ;
static Tc *chTC = TONE_TIMER;
static uint32_t chNo = TONE_CHNL;

volatile static int32_t toggle_count;
static uint32_t tone_pin;


// frequency (in hertz) and duration (in milliseconds).

void tone(uint32_t ulPin, uint32_t frequency, int32_t duration)
{
    const uint32_t rc = VARIANT_MCK / 256 / frequency;
    tone_pin = ulPin;
    toggle_count = 0;  // strange  wipe out previous duration
    if (duration > 0 ) toggle_count = 2 * frequency * duration / 1000;
    else toggle_count = -1;
    
    if (!TCChanEnabled) {
        pmc_set_writeprotect(false);
        pmc_enable_periph_clk((uint32_t)TONE_IRQ);
        TC_Configure(chTC, chNo,
                     TC_CMR_TCCLKS_TIMER_CLOCK4 |
                     TC_CMR_WAVE |         // Waveform mode
                     TC_CMR_WAVSEL_UP_RC ); // Counter running up and reset when equals to RC
        
        chTC->TC_CHANNEL[chNo].TC_IER=TC_IER_CPCS;  // RC compare interrupt
        chTC->TC_CHANNEL[chNo].TC_IDR=~TC_IER_CPCS;
        NVIC_EnableIRQ(TONE_IRQ);
        TCChanEnabled = 1;
    }
    if (!pinEnabled[ulPin]) {
        pinMode(ulPin, OUTPUT);
        pinEnabled[ulPin] = 1;
    }
    TC_Stop(chTC, chNo);
    TC_SetRC(chTC, chNo, rc);    // set frequency
    TC_Start(chTC, chNo);
}
void tone(uint32_t ulPin, uint32_t frequency)
{
    tone(ulPin, frequency, 0);
    
}
void noTone(uint32_t ulPin)
{
	TC_Stop(chTC, chNo);  // stop timer
	digitalWrite(ulPin,LOW);  // no signal on pin
}

// timer ISR  TC1 ch 0
void TC3_Handler ( void ) {
	TC_GetStatus(TC1, 0);
	if (toggle_count != 0){
		// toggle pin  TODO  better
		digitalWrite(tone_pin,pin_state= !pin_state);
		if (toggle_count > 0) toggle_count--;
	} else {
		noTone(tone_pin);
	}
}
#define BOARD_HAS_TONE
#endif

#if defined (__MK20DX128__)    // Teensy 3.0  (PIC32's might have some day)
#define BOARD_HAS_CAPACITANCE_SENSING
#endif

#if defined(__AVR_ATmega32U4__) ||  defined(__MK20DX128__) || defined(__AVR_ATmega328_) ||   defined(__AVR_ATmega128__)

#define BOARD_HAS_DIE_TEMPERATURE_SENSOR
#endif

#if defined(__AVR_ATmega32U4__) ||  defined(__MK20DX128__) || defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__) || defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__) || defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)    || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega328_) ||   defined(__AVR_ATmega128__)


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
#ifndef LED_BUILTIN
#define LED_BUILTIN  PIN_LED
#endif
#ifndef analogInputToDigitalPin
#define analogInputToDigitalPin(p)  ((p < 12) ? (p) + 54 : -1)
#endif
#ifndef NUM_DIGITAL_PINS
#define NUM_DIGITAL_PINS 54
#endif
#ifndef NUM_ANALOG_INPUTS
#define NUM_ANALOG_INPUTS 12
#endif
#endif // defined(__SAM3X8E__)

// missing specs for Leonardo derived devices
#if defined(__AVR_ATmega32U4__) && (!defined(LED_BUILTIN) || !defined(analogInputToDigitalPin))

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

#ifndef analogInputToDigitalPin
int analogInputToDigitalPin(int i)
{
    switch(i)
    {
        case  0: return A0;
        case 1: return A1;
        case 2: return A2;
        case 3: return A3;
        case 4: return A4;
        case 5: return A5;
#ifdef A6
        case 6: return A6;
#endif
#ifdef A7
        case 7: return A7;
#endif
#ifdef A8
        case 8: return A8;
#endif
#ifdef A9
        case 9: return A9;
#endif
#ifdef A10
        case 10: return A10;
#endif
#ifdef A11
        case 11: return A11;
#endif
#ifdef A12
        case 12: return A12;
#endif
#ifdef A13
        case 13: return A13;
#endif
#ifdef A14
        case 14: return A14;
#endif
#ifdef A15
        case 15: return A15;
#endif
#ifdef A16    
        case 16: return A16;
#endif
    }
}
#endif

