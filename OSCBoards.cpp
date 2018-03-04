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
    return -1;
}
#endif
