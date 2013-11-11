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


#include "OSCTiming.h"
#include "OSCBoards.h"

#if defined(__MK20DX128__)
extern volatile uint32_t systick_millis_count;
static uint32_t savedcount, savedcurrent;

static void latchOscTime()
{
    
	uint32_t istatus;
    uint32_t count, current;
    
	__disable_irq();
	current = SYST_CVR;
	count = systick_millis_count;
	istatus = SCB_ICSR;	// bit 26 indicates if systick exception pending
	__enable_irq();
    //systick_current = current;
    //systick_count = count;
    //systick_istatus = istatus & SCB_ICSR_PENDSTSET ? 1 : 0;
	if ((istatus & SCB_ICSR_PENDSTSET) && current > 50) count++;
	current = ((F_CPU / 1000) - 1) - current;
    savedcount=count; savedcurrent=current;
}
static uint64_t computeOscTime()
{ //4,294,967,296
    return  ((( uint64_t)(savedcount/1000))   <<  32 ) +
    
    ( (uint64_t)(4294967295) *  (  (savedcount * 1000 + savedcurrent / (F_CPU / 1000000UL))    % 1000000)   ) /1000000;
}

uint64_t oscTime()
{
    latchOscTime();
    return computeOscTime();
}

#elif defined(CORE_TEENSY) 
extern volatile uint32_t timer0_millis_count;
static uint32_t savedcount, savedmicros;

static void latchOscTime()
{
      cli();
    savedcount = timer0_millis_count;
    savedmicros = micros();
       sei();
}

static uint64_t computeOscTime()
{ //4,294,967,296
    union
    {
        struct { uint32_t lsw, hsw; } split;
        uint64_t t;
    } tt;
    savedmicros %= 1000000;
    tt.split.lsw  = (67108864ULL * savedmicros) / 15625 ; // 2^32/1000000
    tt.split.hsw = savedcount/1000;
    return tt.t;
#ifdef ddfgsdfgsdfgsdfg
    return ((savedcount/1000)<<32) + ( (4294967295ULL) *  (  (savedcount * 1000ULL + savedmicros)    % 1000000ULL)   ) /1000000ULL
    
    ;
#endif

    
}
uint64_t oscTime()
{
    latchOscTime();
    return computeOscTime();

}
#elif defined(AVR)
static uint32_t savedcount, savedmicros;

static void latchOscTime()
{
    cli();
    savedcount = millis();
    savedmicros = micros();
    sei();
}

static uint64_t computeOscTime()
{ //4,294,967,296
    union
    {
        struct { uint32_t lsw, hsw; } split;
        uint64_t t;
    } tt;
    savedmicros %= 1000000;
    tt.split.lsw  = (67108864ULL * savedmicros) / 15625 ; // 2^32/1000000
    tt.split.hsw = savedcount/1000;
    return tt.t;

    
    
}
uint64_t oscTime()
{
    latchOscTime();
    return computeOscTime();
}
#else

static void latchOscTime()
{
}

uint64_t oscTime()
{
    return 1;
}
#endif

int adcRead(int pin, uint64_t *t)
{
    latchOscTime();
    
    int v =analogRead(pin);
    *t = oscTime();
    return v;
}
#ifdef BOARD_HAS_CAPACITANCE_SENSING
int capacitanceRead(int pin, uint64_t *t)
{
    latchOscTime();
    int v =  touchRead(pin);
    
    *t = oscTime();
    return v;
}
#endif
int inputRead(int pin, uint64_t *t)
{
    
    int v =digitalRead(pin);
    *t = oscTime();
    
    return v;
}