/*
 Written by John MacCallum, The Center for New Music and Audio Technologies,
 University of California, Berkeley.  Copyright (c) 2009, The Regents of
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
 */

#ifndef __OSC_MATCH_H__
#define __OSC_MATCH_H__

#ifdef __cplusplus
extern "C" {
#endif
	
	/**
	 * Switch this off to disable matching against a pattern with 2 stars
	 */
    //#define OSC_MATCH_ENABLE_2STARS		1
	/**
	 * Switch this off to disable matching against a pattern with more than 2 stars which will
	 * be done recursively.
	 */
    //#define OSC_MATCH_ENABLE_NSTARS		1
	
	/**
	 * Return code for osc_match() that indicates that the entire address was successfully matched
	 */
#define OSC_MATCH_ADDRESS_COMPLETE	1
	
	/**
	 * Return code for osc_match() that indicates that the entire pattern was successfully matched
	 */
#define OSC_MATCH_PATTERN_COMPLETE	2
	/*
	 typedef struct _osc_callback {
	 const char* address;			// Address
	 struct _osc_callback *child;		// RAM
	 struct _osc_callback *sibling;		// RAM
	 struct _osc_callback *parent;		// RAM
	 int callback;				// ROM
	 } osc_callback;
	 */
	
	/**
	 * Match a pattern against an address.  In the case of a partial match, pattern_offset
	 * and address_offset will contain the number of bytes into their respective strings
	 * where the match failed.
	 *
	 * @param pattern The pattern to match
	 * @param address The address to match
	 * @param pattern_offset The number of bytes into the pattern that were matched successfully
	 * @param address_offset The number of bytes into the address that were matched successfully
	 * @return 0 if the match failed altogether, or an or'd combination of OSC_MATCH_ADDRESS_COMPLETE and
	 * OSC_MATCH_PATTERN_COMPLETE.
	 */
	int osc_match(const char *pattern, const char *address, int *pattern_offset, int *address_offset);
	
#ifdef __cplusplus
}
#endif

#endif // __OSC_MATCH_H__

