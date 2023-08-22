/*
 Written by Adrian Freed, The Center for New Music and Audio Technologies,
 University of California, Berkeley.  Copyright (c) 2013, The Regents of
 the University of California (Regents).
 
 Permission to use, copy, modify, distribute, and distribute modified versions
 of this software and its documentation without fee and without a signed
 licensing agreement, is hereby granted, provided that the above copyright
 notice, this paragraph and the following two paragraphs appear in all copies,
 modifications, and distributions.
 
 IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
 SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING
 OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF REGENTS HAS
 BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE. THE SOFTWARE AND ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED
 HEREUNDER IS PROVIDED "AS IS". REGENTS HAS NO OBLIGATION TO PROVIDE
 MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 
 For bug reports and feature requests please email me at yotam@cnmat.berkeley.edu
 */


#include <Arduino.h>
#include "OSCBoards.h"

#ifndef analogInputToDigitalPin
int analogInputToDigitalPin(int i)
{
    switch(i)
    {
#ifdef A0
        case 0: return A0;
#endif
#ifdef A1
        case 1: return A1;
#endif
#ifdef A2
        case 2: return A2;
#endif
#ifdef A3
        case 3: return A3;
#endif
#ifdef A4
        case 4: return A4;
#endif
#ifdef A5
        case 5: return A5;
#endif
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
    return -1;
}
#endif


#ifdef BOARD_HAS_DIE_POWER_SUPPLY_MEASUREMENT
#if defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MKL26Z64__)  || defined(__MK64FX512V__)  || defined(__MK66FX1M0__)
float getSupplyVoltage()
{
    analogReference(DEFAULT);
    analogReadResolution(12);
    analogReadAveraging(32);
    PMC_REGSC |= PMC_REGSC_BGBE; // 39=bandgap ref (PMC_REGSC |= PMC_REGSC_BGBE);
    delay(1);

#if defined(__MKL26Z64__)
    // Teensy 3 LC
    int val = analogRead(39);
    return val>0? (1.0f*4095/val):0.0f;
#elif defined(__MK64FX512V__)  || defined(__MK66FX1M0__)
    int val = analogRead(71);
    return val>0? (1.195f*4095/val):0.0f;
#else
    int val = analogRead(39);
    return val>0? (1.195f*4095/val):0.0f;
#endif
}

#else
// power supply measurement on some Arduinos
float getSupplyVoltage(){
    // powersupply
    int result;
    // Read 1.1V reference against AVcc
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
#elif  defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)    || defined(__AVR_ATmega1280__)
    ADMUX = 0x40| _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1) ;
    ADCSRB =  0;
#else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif
    delayMicroseconds(300); // wait for Vref to settle
    ADCSRA |= _BV(ADSC); // Convert
    while (bit_is_set(ADCSRA,ADSC));
    result = ADCL;
    result |= ADCH<<8;
    
    float supplyvoltage = 1.1264f *1023 / result;
    return supplyvoltage;
}
#endif

#endif

#ifdef BOARD_HAS_DIE_TEMPERATURE_SENSOR

#if   defined(__MK20DX128__) || defined(__MK20DX256__)|| defined(__MKL26Z64__)  || defined(__MK66FX1M0__)  || defined(__MK64FX512V__)
float getTemperature()
{
#if defined(__MK64FX512V__)  || defined(__MK66FX1M0__)
    const int temppin = 70 ;
#else
    const int temppin = 38;
#endif
    // untested on all teensy 3.x
    analogReference(INTERNAL);
    analogReadResolution(12);
    analogReadAveraging(32);
    delay(2);
    
    float val = 25.0 + 0.17083 * (2454.19 - analogRead(temppin));
    
    analogReference(DEFAULT);
    
    return val;
}
#else
// temperature
float getTemperature(){
    int result;
    
#if defined(__AVR_ATmega32U4__)
    ADMUX =  _BV(REFS1) | _BV(REFS0) | _BV(MUX2) | _BV(MUX1) | _BV(MUX0);
    ADCSRB =  _BV(MUX5);
#else
    ADMUX = _BV(REFS1) | _BV(REFS0) | _BV(MUX3);
#endif
    delayMicroseconds(200); // wait for Vref to settle
    ADCSRA |= _BV(ADSC); // Convert
    while (bit_is_set(ADCSRA,ADSC));
    result = ADCL;
    result |= ADCH<<8;
    
    analogReference(DEFAULT);
    
    return  result/1023.0f;
}
#endif  

#endif
