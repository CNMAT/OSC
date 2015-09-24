#include <ArduinoUnit.h>
#include <OSCMessage.h>


test(message_address_match){
  OSCMessage msg("/a/0");
  assertEqual(msg.match("/a"), 2);
  assertEqual(msg.match("/a/0"), 4);
}

test(message_address_match_offset){
  OSCMessage msg("/a/0");
  assertEqual(msg.match("/0", 2), 2);
  assertEqual(msg.match("/1", 2), 0);
}

test(message_address_match_range){
  OSCMessage msg("/[a-z]/0");
  assertEqual(msg.match("/a"), 6);
  assertEqual(msg.match("/b/0"), 8);
}

test(message_address_match_or){
  OSCMessage msg("/{a,b}/0");
  assertEqual(msg.match("/a/0"), 8);
  assertEqual(msg.match("/b/0"), 8);
  assertEqual(msg.match("/c/0"), 0);
}

test(message_address_match_char){
  OSCMessage msg("/a/?");
  assertEqual(msg.match("/a/0"), 4);
  assertEqual(msg.match("/a/1"), 4);
  assertEqual(msg.match("/a/10"), 0);
}

test(message_address_match_star){
  OSCMessage msg("/a/*");
  assertEqual(msg.match("/a/0"), 4);
  assertEqual(msg.match("/a/10"), 4);
  assertEqual(msg.match("/a/100"), 4);
}

test(message_address_fullMatch){
  OSCMessage msg("/a/0");
  assertTrue(msg.fullMatch("/a/0"));
  assertFalse(msg.fullMatch("/a/1"));
  assertTrue(msg.fullMatch("/0", 2));
}

void dispatchMsg(OSCMessage &m){
  assertTrue(m.isInt(0));
  assertEqual(m.getInt(0), 1);
}

test(message_address_dispatch){
  OSCMessage msg("/a/[0-9]");
  msg.add(1);
  assertTrue(msg.dispatch("/a/0", dispatchMsg));
  assertTrue(msg.dispatch("/1", dispatchMsg, 2));
}


void routeMsg(OSCMessage &m, int offset){
  assertTrue(m.isInt(0));
  assertEqual(m.getInt(0), 2);
  assertEqual(offset, 2);
}

test(message_address_route){
  OSCMessage msg("/?/[0-9]");
  msg.add(2);
  assertTrue(msg.route("/a", routeMsg));
  assertTrue(msg.route("/b", routeMsg));
}

void setup()
{
  Serial.begin(9600);
  while(!Serial); // for the Arduino Leonardo/Micro only
}

void loop()
{
  Test::run();
}
