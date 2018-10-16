#include <ArduinoUnit.h>
#include <OSCMessage.h>

#define HAS_DOUBLE sizeof(double) == 8


test(message_address){
  OSCMessage msg("/hihi");
  char addr[6];
  msg.getAddress(addr);
  assertEqual(strcmp(addr, "/hihi"), 0);
}

test(message_address_offset){
  OSCMessage msg("/foo/bar");
  char addr[5];
  msg.getAddress(addr, 4);
  assertEqual(strcmp(addr, "/bar"), 0);
}

test(message_copy){
  OSCMessage msg("/hihi");
  msg.add(1);
  OSCMessage cpy(&msg);
  assertEqual(cpy.size(), 1);
  assertTrue(cpy.isInt(0));
  assertEqual(cpy.getInt(0), 1);
}

test(message_int){
  OSCMessage msg("/foo");
  msg.add(1);
  assertTrue(msg.isInt(0));
  assertEqual(msg.getInt(0), 1);
  assertEqual(msg.getDataLength(0), 4);
}

test(message_float){
  OSCMessage msg("/foo");
  msg.add(1.0f);
  assertTrue(msg.isFloat(0));
  assertEqual(msg.getFloat(0), 1.0f);
  assertEqual(msg.getDataLength(0), 4);
}

test(message_string){
  OSCMessage msg("/foo");
  msg.add("oh hi");
  assertTrue(msg.isString(0));
  char str[6];
  msg.getString(0, str, 6);
  assertEqual(strcmp(str, "oh hi"), 0);
  assertEqual(msg.getDataLength(0), 6);
}

test(message_blob){
  OSCMessage msg("/foo");
  uint8_t b[] = {0, 1, 2, 3, 4};
  msg.add(b, 5);
  assertTrue(msg.isBlob(0));
  uint8_t blob[5];
  msg.getBlob(0, blob, 5);
  for (int i = 0; i < 5; i++){
    assertEqual(blob[i], b[i]); 
  }
  //9 because it includes the 4 byte length
  assertEqual(msg.getDataLength(0), 9);
}

test(message_boolean){
  OSCMessage msg("/foo");
  msg.add(true);
  assertTrue(msg.isBoolean(0));
  assertEqual(msg.getBoolean(0), true);
  assertEqual(msg.getDataLength(0), 0);
}

test(mixed_message_type){
  OSCMessage msg("/foo");
  msg.add(true);
  msg.add(1.0f);
  msg.add(2);
  msg.add("test");
  assertEqual(msg.size(), 4);
  assertEqual(msg.getType(0), 'T');
  assertTrue(msg.isBoolean(0));
  assertFalse(msg.isBoolean(1));
  assertTrue(msg.isFloat(1));
  assertTrue(msg.isInt(2));
  assertTrue(msg.isString(3));
  assertEqual(msg.getInt(2), 2);
  assertEqual(msg.getInt(3), NULL);
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
