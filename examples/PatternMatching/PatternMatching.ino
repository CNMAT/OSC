/*
 The library has four methods for doing pattern matching on address. 
 'match' and 'fullMatch' are specific to OSCMessages while route and dispatch work on both messages and bundles. 
 
 OSCMessage:
 match - matches the message's address pattern against an address. returns the number of characters matched from the address passed in.
 fullMatch - returns true if the pattern was a complete match against the address
 
 OSCMessage && OSCBundle:
 route - calls a function with the matched OSCMessage(s) and the number of matched characters in the address as the parameters
 dispatch - calls a function with each OSCMessage which was fully matched by the pattern

///////////////////////////////////////////////////////////////////////////////////////////////////
OSC Regular expression pattern matching rules from http://opensoundcontrol.org/spec-1_0
 
 1. '?' in the OSC Address Pattern matches any single character
 2. '*' in the OSC Address Pattern matches any sequence of zero or more characters
 3. A string of characters in square brackets (e.g., "[string]") in the OSC Address Pattern matches any character in the string. 
 Inside square brackets, the minus sign (-) and exclamation point (!) have special meanings:
 two characters separated by a minus sign indicates the range of characters between the given two in ASCII collating sequence.
 An exclamation point at the beginning of a bracketed string negates the sense of the list, meaning that the list matches any character not in the list.
 4. A comma-separated list of strings enclosed in curly braces (e.g., "{foo,bar}") in the OSC Address Pattern matches any of the strings in the list.
 5. Any other character in an OSC Address Pattern can match only the same character.
 ///////////////////////////////////////////////////////////////////////////////////////////////////
 */

#include <OSCBundle.h>

void setup() {
  Serial.begin(38400);
}

void loop(){
  //a heavily patterned message address: an analog or digital pin address from
  //ADDRESSES.md, any pin number, then a one-character sub-address that is not
  //a or b (the contract's /u pull-up read is one)
  OSCMessage msg0("/{a,d}/[0-9]*/[!ab]");
  //match will traverse as far as it can in the pattern
  //it returns the number of characters matched from the pattern
  int patternOffset = msg0.match("/d/13");
  if (patternOffset>0){
    //string multiple 'match' methods together using the pattern offset parameter to continue matching where it left off
    //use 'fullMatch' to test if the entire pattern was matched. 
    if(msg0.fullMatch("/u", patternOffset)){
      Serial.println("Match: '/d/13/u' against the pattern '/{a,d}/[0-9]*/[!ab]'");
    }
  }
  //write over the other message address
  OSCMessage msg1("/display");
  //match will return 0 if it did not reach the end or a '/'
  if(!msg1.match("/d")){
    Serial.println("No Match: '/d' against the pattern '/display'");
  }
  OSCMessage msg2("/d/[0-2]");
  //'route' is uses 'match' to allow for partial matches
  //it invokes the callback with the matched message and the pattern offset as parameters to the callback
  msg2.route("/d", routeDigital);
  //'dispatch' uses 'fullMatch' so it does not allow for partial matches
  //invokes the callback with only one argument which is the matched message
  msg2.dispatch("/d/1", routeDigitalOne);
  delay(1000);
}

//called after matching '/d' 
//the matched message and the number of matched characters as the parameters
void routeDigital(OSCMessage &msg, int patternOffset){
  Serial.println("Match: '/d'");
  //string multiple 'route' methods together using the pattern offset parameter. 
  msg.route("/0", routeZero, patternOffset);
}

//called after matching '/0'
void routeZero(OSCMessage &msg, int addressOffset){
  Serial.println("Match: '/d/0'");
}

//called after matching '/d/1' 
void routeDigitalOne(OSCMessage &msg){
  Serial.println("Match: '/d/1'");
}


//
// TROUBLESHOOTING:
// Because of a bug in the Arduino IDE, it sometimes thinks that the '*' in combination with '/' is the opening or closing of a multiline comment
// This can be fixed by escaping the '/' with '\' or using the octal value of '*' which is '\052'
// for example:
// "/*" == "/\052" ==  "\/*"
//




