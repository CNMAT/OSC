#include <ArduinoUnit.h>
#include <OSCData.h>

#define HAS_DOUBLE sizeof(double) == 8

test(data_int){
  int i = 1;
  OSCData datum(i);
  assertEqual(datum.getInt(), i);
  assertEqual(datum.type, 'i');
}

test(data_float){
  float f = 1.1;
  OSCData datum(f);
  assertEqual(datum.getFloat(), f);
  assertEqual(datum.type, 'f');
}

test(data_string){
  char * testStr = "data";
  int testStrLen = strlen(testStr) + 1;
  OSCData datum(testStr);
  char str[testStrLen];
  datum.getString(str, testStrLen);
  assertEqual(strcmp(str, testStr), 0);
  assertEqual(datum.type, 's');
}

test(data_string_partial_copy){
  char * testStr = "data";
  int testStrLen = strlen(testStr) + 1;
  OSCData datum(testStr);
  char str[testStrLen];
  assertEqual(datum.getString(str, 2), 2);
  assertEqual(strncmp(str, testStr, 2), 0);
}

test(data_bool){
  bool f = false;
  OSCData datum(f);
  assertFalse(datum.getBoolean());
}

test(has_double){ 
  assertEqual(sizeof(double), 8);
}

test(data_double){ 
  if (HAS_DOUBLE){
    double d = 1.1;
    OSCData datum = OSCData(d);
    assertEqual(datum.getDouble(), d);
    assertEqual(datum.type, 'd');
  }
}

test(data_blob){ 
  uint8_t b[] = {0, 1, 2, 3};
  OSCData datum(b, 4);
  uint8_t blob[4];
  datum.getBlob(blob, 4);
  for (int i = 0; i < 4; i++){
    assertEqual(blob[i], b[i]);
  }
  assertEqual(datum.type, 'b');
}

test(data_blob_partial_copy){ 
  uint8_t b[] = {0, 1, 2, 3};
  OSCData datum(b, 4);
  uint8_t blob[4];
  assertEqual(datum.getBlob(blob, 2), 2);
  for (int i = 0; i < 2; i++){
    assertEqual(blob[i], b[i]);
  }
}

test(data_copy){ 
  OSCData datum = OSCData(1);
  OSCData cpy(datum);
  assertEqual(cpy.getInt(), 1);
  assertEqual(cpy.type, 'i');
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
