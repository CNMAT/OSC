// Example for ETC EOS Conasoles, sending und receiving pings
#include "Ethernet.h"
#include "OSCMessage.h"
#include "SLIPEncodedTCP.h"

uint8_t mac[] = {0x90, 0xA2, 0xDA, 0x10, 0x14, 0x48};
IPAddress localIP(10, 101, 1, 201); // IP of your Arduino
IPAddress dns(10, 101, 1, 201); // IP of your DNS server
IPAddress subnet(255, 255, 0, 0); // your subnet
IPAddress eosIP(10, 101, 1, 100); // IP of your console
uint16_t eosTcpPort = 3037; // use default EOS Slip port

EthernetClient tcp;
SLIPEncodedTCP slip (tcp);

void setup() {
	Serial.begin(9600);
	Ethernet.begin(mac, localIP, dns ,subnet);
	if (!tcp.connected()) {
		tcp.stop();
		tcp.connect(eosIP, eosTcpPort);
	}
}

void loop() {
	OSCMessage msg;
	static unsigned long lastTimeSent;
	static int32_t pingNum;
	unsigned long curTime;

	// look if there is a new meassage, if yes print ping data
	if (slip.available()) {
    while (!slip.endofPacket()) {
      while (slip.available()) {
        msg.fill(slip.read());
			}
		}
		if (msg.fullMatch("/eos/out/ping")) {
			Serial.print("Ping Number ");
			Serial.print(msg.getInt(0));
			Serial.println(" received");
		}
	}

	// send a ping every second
	curTime = millis();
	if (curTime - lastTimeSent > 1000) {
		OSCMessage ping("/eos/ping");
		ping.add(pingNum++);
		if (!tcp.connected()) {
			tcp.stop();
			tcp.connect(eosIP, eosTcpPort);
		}
		slip.beginPacket();
		ping.send(slip);
		slip.endPacket();
		lastTimeSent = curTime;
	}
}
