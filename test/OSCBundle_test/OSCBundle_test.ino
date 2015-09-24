#include <ArduinoUnit.h>
#include <OSCBundle.h>
#include "TestPrint.h"

#define HAS_DOUBLE sizeof(double) == 8

test(bundle_address){
  OSCBundle bundle;
  bundle.add("/address").add(1);
  assertEqual(bundle.size(), 1);
  OSCMessage msg = bundle.getOSCMessage("/address");
  assertTrue(msg.isInt(0));
  assertEqual(msg.getInt(0), 1);
}

test(bundle_message_position){
  OSCBundle bundle;
  bundle.add("/one").add(1);
  bundle.add("/two").add(2);
  assertEqual(bundle.size(), 2);
  OSCMessage msg0 = bundle.getOSCMessage(0);
  assertTrue(msg0.isInt(0));
  assertEqual(msg0.getInt(0), 1);
  OSCMessage msg1 = bundle.getOSCMessage(1);
  assertTrue(msg1.isInt(0));
  assertEqual(msg1.getInt(0), 2);
}

test(bundle_add_message){
  OSCMessage msg("/msg");
  OSCBundle bundle;
  bundle.add(msg).add(1);
  assertEqual(bundle.size(), 1);
  OSCMessage test_msg = bundle.getOSCMessage(0);
  assertTrue(test_msg.isInt(0));
  assertEqual(test_msg.getInt(0), 1);
}

void routeA(OSCMessage & msg, int offset){
  assertTrue(msg.isInt(0));
  assertEqual(msg.getInt(0), 1); 
}

void routeB(OSCMessage & msg, int offset){
  assertTrue(msg.isInt(0));
  assertEqual(msg.getInt(0), 2); 
}

test(bundle_route){
  OSCMessage msg("/msg");
  OSCBundle bundle;
  bundle.add("/a").add(1);
  bundle.add("/b/1").add(2);
  assertEqual(bundle.size(), 2);
  assertTrue(bundle.route("/a", routeA));
  assertTrue(bundle.route("/b", routeB));
}

void dispatchA(OSCMessage & msg){
  assertTrue(msg.isInt(0));
  assertEqual(msg.getInt(0), 1); 
}

void dispatchB(OSCMessage & msg){
  assertTrue(msg.isInt(0));
  assertEqual(msg.getInt(0), 2); 
}

test(bundle_dispatch){
  OSCMessage msg("/msg");
  OSCBundle bundle;
  bundle.add("/a").add(1);
  bundle.add("/b").add(2);
  assertEqual(bundle.size(), 2);
  assertTrue(bundle.route("/a", routeA));
  assertTrue(bundle.route("/b", routeB));
}

test(bundle_encode){
   TestPrint printer;
  //this is the desired output
  uint8_t testBuffer[] = {35, 98, 117, 110, 100, 108, 101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 47, 97, 0, 0, 44, 105, 0, 0, 0, 0, 0, 1, 0, 0, 0, 12, 47, 98, 0, 0, 44, 105, 0, 0, 0, 0, 0, 2};
  OSCBundle bundle;
  bundle.add("/a").add(1);
  bundle.add("/b").add(2);
  bundle.send(printer);
  assertEqual(printer.size(), sizeof(testBuffer));
  for (int i = 0; i < sizeof(testBuffer); i++){
    assertEqual(testBuffer[i], printer.at(i));
  }
}

test(bundle_decode){
  uint8_t testBuffer[] = {35, 98, 117, 110, 100, 108, 101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 47, 97, 0, 0, 44, 105, 0, 0, 0, 0, 0, 1, 0, 0, 0, 12, 47, 98, 0, 0, 44, 105, 0, 0, 0, 0, 0, 2};
  OSCBundle bundle;
  bundle.fill(testBuffer, sizeof(testBuffer));
  assertEqual(bundle.size(), 2);
  OSCMessage msgA = bundle.getOSCMessage("/a");
  assertTrue(msgA.isInt(0));
  assertEqual(msgA.getInt(0), 1);
  OSCMessage msgB = bundle.getOSCMessage("/b");
  assertTrue(msgB.isInt(0));
  assertEqual(msgB.getInt(0), 2);
}

test(bundle_decode_invalid){
  uint8_t testBuffer[] = {35, 98, 117, 110, 100, 108, 101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 47, 97, 0, 0, 44, 105, 0, 0, 0, 0, 0, 1, 0, 0, 0, 12, 47, 98, 0, 0, 44, 105, 0, 0, 0, 0, 2};
  OSCBundle bundle;
  bundle.fill(testBuffer, sizeof(testBuffer));
  assertTrue(bundle.hasError());
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
