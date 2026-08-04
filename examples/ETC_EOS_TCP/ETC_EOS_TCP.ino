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
		// Bounded. A TCP segment can end part way through a packet, and an
		// unbounded wait here never returns: available() stays 0, endofPacket()
		// stays false, and the ping below stops being sent for good, which on a
		// console that expects a keepalive drops the connection.
		unsigned long lastByte = millis();
		while (!slip.endofPacket()) {
			if (slip.available()) {
				//read() returns int, -1 when the stream dries up mid-packet; fed
				//straight into fill() that narrows to an 0xFF data byte
				int c = slip.read();
				if (c >= 0) msg.fill((uint8_t)c);
				lastByte = millis();
			} else if (millis() - lastByte > 200) {
				break;              // stalled mid-packet; drop it and keep pinging
			}
		}
		// isInt() as well as hasError(): an argument-less /eos/out/ping frame
		// leaves hasError() false and getInt(0) returning a fake 0.
		if (!msg.hasError() && msg.fullMatch("/eos/out/ping") && msg.isInt(0)) {
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
