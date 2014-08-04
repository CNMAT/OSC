//
//  OSCTiming.h
//  
//
//  Created by AdrianFreed on 11/10/13.
//
//

#ifndef ____OSCTiming__
#define ____OSCTiming__


#include "Arduino.h"
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
typedef struct
{
    uint32_t seconds;
    uint32_t fractionofseconds;
} osctime_t;

osctime_t oscTime();
int adcRead(int pin, osctime_t *t);
int capacitanceRead(int pin, osctime_t *t);

int inputRead(int pin, uint64_t *t);


#endif /* defined(____OSCTiming__) */
