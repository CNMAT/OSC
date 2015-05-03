#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
const char* ssid = "ESP";
const char* password = "itsasecret";

WiFiUDP Udp;

unsigned int listenPort = 9000;
unsigned int sendPort = 9001;
IPAddress sendIP1(192, 168, 4 , 2);
IPAddress sendIP2(192, 168, 4 , 3);
IPAddress sendIP3(192, 168, 4 , 4);

float inc = 0;

void setup() {
  Serial.begin(115200);
  delay(10);
  
  while(!Serial) {
    
  }
  
  pinMode(2, OUTPUT);
  digitalWrite(2, 0);
  
  // flush out the gibberish?
  Serial.println();
  Serial.println();
  Serial.println();
  
  WiFi.mode(WIFI_AP);
  WiFi.disconnect();
  delay(200);
  WiFi.softAP(ssid, password);
  delay(200);
  
  Serial.println();
  Serial.println();
  Serial.print("AP IP interface: ");
  Serial.println(WiFi.softAPIP());
  
  Serial.println();
  Serial.println();
  Serial.println("connecting to OSC server");
  while(!Udp.begin(listenPort)) {
    Serial.print("-");
  }
  Serial.println("success!");
  
}

void loop() {
  OSCMessage msg("/1/fader1");
  msg.add(inc);
  Udp.beginPacket(sendIP1, sendPort);
  msg.send(Udp);
  Udp.endPacket();
  
  Udp.beginPacket(sendIP2, sendPort);
  msg.send(Udp);
  Udp.endPacket();
  
  Udp.beginPacket(sendIP3, sendPort);
  msg.send(Udp);
  Udp.endPacket();
  msg.empty();
  delay(200);
  
  //inc = random(0,1);
  inc = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
  Serial.println(inc);

}
