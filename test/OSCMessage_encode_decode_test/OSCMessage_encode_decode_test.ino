#include <ArduinoUnit.h>
#include <OSCMessage.h>
#include "TestPrint.h"

#define HAS_DOUBLE sizeof(double) == 8

test(message_encode_int){
  TestPrint printer;
  //this is the desired output
  uint8_t testBuffer[] = {47, 102, 111, 111, 0, 0, 0, 0, 44, 105, 105, 105, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 255, 255, 255, 236};
  OSCMessage msg("/foo");
  msg.add(1);
  msg.add(2);
  msg.add(-20);
  msg.send(printer);
  assertEqual(printer.size(), sizeof(testBuffer));
  for (int i = 0; i < sizeof(testBuffer); i++){
    assertEqual(testBuffer[i], printer.at(i));
  }
}

test(message_encode_string){
  TestPrint printer;
  //this is the desired output
  uint8_t testBuffer[] = {47, 116, 101, 115, 116, 0, 0, 0, 44, 115, 115, 0, 104, 105, 0, 0, 104, 111, 119, 100, 121, 0, 0, 0};
  OSCMessage msg("/test");
  msg.add("hi");
  msg.add("howdy");
  msg.send(printer);
  assertEqual(printer.size(), sizeof(testBuffer));
  for (int i = 0; i < sizeof(testBuffer); i++){
    assertEqual(testBuffer[i], printer.at(i));
  }
}

test(message_encode_float){
  TestPrint printer;
  //this is the desired output
  uint8_t testBuffer[] = {47, 97, 100, 100, 114, 101, 115, 115, 0, 0, 0, 0, 44, 102, 102, 0, 63, 128, 0, 0, 192, 6, 102, 102};
  OSCMessage msg("/address");
  msg.add(1.0f);
  msg.add(-2.1f);
  msg.send(printer);
  assertEqual(printer.size(), sizeof(testBuffer));
  for (int i = 0; i < sizeof(testBuffer); i++){
    assertEqual(testBuffer[i], printer.at(i));
  }
}

test(message_encode_double){
  if (HAS_DOUBLE){
    TestPrint printer;
    //this is the desired output
    uint8_t testBuffer[] = {47, 97, 0, 0, 44, 100, 100, 0, 64, 89, 0, 0, 0, 0, 0, 0, 63, 80, 98, 77, 210, 241, 169, 252};
    OSCMessage msg("/a");
    double d0 = 100;
    double d1 = 0.001;
    msg.add(d0);
    msg.add(d1);
    msg.send(printer);
    assertEqual(printer.size(), sizeof(testBuffer));
    for (int i = 0; i < sizeof(testBuffer); i++){
      assertEqual(testBuffer[i], printer.at(i));
    }
  }
}

test(message_encode_blob){
  TestPrint printer;
  //this is the desired output
  uint8_t testBuffer[] = {47, 98, 0, 0, 44, 98, 0, 0, 0, 0, 0, 5, 1, 2, 3, 4, 5, 0, 0, 0};
  OSCMessage msg("/b");
  uint8_t blob[] = {1, 2, 3, 4, 5};
  msg.add(blob, 5);
  msg.send(printer);
  assertEqual(printer.size(), sizeof(testBuffer));
  for (int i = 0; i < sizeof(testBuffer); i++){
    assertEqual(testBuffer[i], printer.at(i));
  }
}

test(message_encode_mixed){
  TestPrint printer;
  //this is the desired output
  uint8_t testBuffer[] = {47, 109, 105, 120, 101, 100, 0, 0, 44, 115, 105, 102, 0, 0, 0, 0, 111, 110, 101, 0, 0, 0, 0, 1, 63, 128, 0, 0};
  OSCMessage msg("/mixed");
  msg.add("one");
  msg.add(1);
  msg.add(1.0f);
  msg.send(printer);
  assertEqual(printer.size(), sizeof(testBuffer));
  for (int i = 0; i < sizeof(testBuffer); i++){
    assertEqual(testBuffer[i], printer.at(i));
  }
}

test(message_decode_int){
  uint8_t testBuffer[] = {47, 105, 110, 116, 115, 0, 0, 0, 44, 105, 105, 105, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 200, 255, 255, 255, 231};
  OSCMessage msg;
  msg.fill(testBuffer, sizeof(testBuffer));
  assertEqual(msg.size(), 3);
  assertTrue(msg.isInt(0));
  assertTrue(msg.isInt(1));
  assertTrue(msg.isInt(2));
  assertEqual(msg.getInt(0), 1);
  assertEqual(msg.getInt(1), 200);
  assertEqual(msg.getInt(2), -25);
}

test(message_decode_float){
  uint8_t testBuffer[] = {47, 102, 108, 111, 97, 116, 115, 0, 44, 102, 102, 0, 191, 128, 0, 0, 67, 72, 2, 143};
  OSCMessage msg;
  msg.fill(testBuffer, sizeof(testBuffer));
  assertEqual(msg.size(), 2);
  assertFalse(msg.isInt(0));
  assertTrue(msg.isFloat(0));
  assertTrue(msg.isFloat(1));
  assertEqual(msg.getFloat(0), -1.0f);
  assertEqual(msg.getFloat(1), 200.01f);
}

test(message_decode_double){
  if (HAS_DOUBLE){
    uint8_t testBuffer[] = {47, 100, 0, 0, 44, 100, 0, 0, 64, 36, 5, 30, 184, 81, 235, 133};
    OSCMessage msg;
    msg.fill(testBuffer, sizeof(testBuffer));
    double d = 10.01;
    assertEqual(msg.size(), 1);
    assertTrue(msg.isDouble(0));
    assertEqual(msg.getDouble(0), d);
  }
}

test(message_decode_string){
  uint8_t testBuffer[] = {47, 115, 116, 114, 105, 110, 103, 115, 0, 0, 0, 0, 44, 115, 115, 0, 104, 101, 121, 0, 104, 105, 0, 0};
  OSCMessage msg;
  msg.fill(testBuffer, sizeof(testBuffer));
  assertEqual(msg.size(), 2);
  assertTrue(msg.isString(0));
  assertTrue(msg.isString(1));
  char str[4];
  msg.getString(0, str, 5);
  assertEqual(strcmp(str, "hey"), 0);
  msg.getString(1, str, 3);
  assertEqual(strcmp(str, "hi"), 0);
}

test(message_decode_blob){
  uint8_t testBuffer[] = {47, 98, 0, 0, 44, 98, 0, 0, 0, 0, 0, 4, 0, 1, 2, 3};
  OSCMessage msg;
  msg.fill(testBuffer, sizeof(testBuffer));
  assertEqual(msg.size(), 1);
  assertTrue(msg.isBlob(0));
  uint8_t blob[4];
  msg.getBlob(0, blob, 4);
  for (int i = 0; i < 4; i++){
    assertEqual(i, blob[i]);   
  }
}

test(message_decode_mixed){
  uint8_t testBuffer[] = {47, 109, 105, 120, 101, 100, 0, 0, 44, 115, 105, 102, 0, 0, 0, 0, 111, 110, 101, 0, 0, 0, 0, 1, 63, 128, 0, 0};
  OSCMessage msg;
  msg.fill(testBuffer, sizeof(testBuffer));
  assertEqual(msg.size(), 3);
  assertTrue(msg.isString(0));
  assertTrue(msg.isInt(1));
  assertTrue(msg.isFloat(2));
  assertEqual(msg.getInt(1), 1);
  assertEqual(msg.getFloat(2), 1.0f);
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
