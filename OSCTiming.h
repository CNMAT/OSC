//
//  OSCTiming.h
//  
//
//  Created by AdrianFreed on 11/10/13.
//
//

#ifndef ____OSCTiming__
#define ____OSCTiming__
#if ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

uint64_t oscTime();
int adcRead(int pin, uint64_t *t);
int capacitanceRead(int pin, uint64_t *t);

int inputRead(int pin, uint64_t *t);


#endif /* defined(____OSCTiming__) */
