# OSC for Arduino

This is an Arduino and Teensy library implementation of the [OSC](http://opensoundcontrol.org) (Open Sound Control) encoding.It was developed primarily by Yotam Mann and Adrian Freed at CNMAT where OSC was invented. It benefits from contributions from John MacCallum, Matt Wright, Jeff Lubow and Andy Schmeder and many beta testers.

Features: 

* Supports the four basic OSC data types (32-bit integers, 32-bit floats, strings, and blobs - arbitrary length byte sequences)
* Supports the optional 64-bit timetag data type and Booleans
* Address pattern matching
* Dynamic memory allocation
* Sends and receives OSC packets over transport layers that implements the Arduino Stream Class such as Serial and  Ethernet UDP

# Installation

We recommend Arduino 1.05 or greater and Teensyduino 1.14 or later if you use the Teensy. Download the zip from github. Unzip it and remove text after the "-" in the name (i.e. -master) because the Arduino IDE doesn't allow certain characters in library names. Move the OSC folder into your "libraries" folder in your Arduino Sketch search path. Now you will see OSC examples under the Examples menu of Arduino. 

Additional information about installing libraries on [Arduino's website](https://www.arduino.cc/en/Guide/Libraries).

# Examples

The `Applications` folder contains examples for Max/MSP and PD and Processing that work with the example sketches. This will be expanded to include other applications like TouchOSC and Processing. For the Max/MSP examples you will need to download the CNMAT max externals package that includes the "o." objects available [here](http://cnmat.berkeley.edu/downloads).

# API

OSC for Arduino supports creating, sending and receiving OSCMessages individually and wrapped into OSCBundles. 

The full API is available [here](./API.md).

### Sending Data

Create a new `OSCMessage` with an address in the constructor:

```C++
OSCMessage msg("/address");
```

add some data to it:

```C++
msg.add(1);
msg.add(2.0);
msg.add("three");
```

`add` will infer the type of the data and encode it correctly. The API also supports chaining, so multiple calls to `add` can be strung together: 

```C++
msg.add(1).add(2.0f).add("three");
```

Then send it over any transport layer that extends Arduino's [Print class](http://playground.arduino.cc/Code/Printclass) like the `Serial` out. 

```C++
msg.send(Serial);
```

### Receiving Data

In a typical Serial stream, there is no way to know where one message ends and another begins. That's why we recommend using `SLIPSerial` (which also comes in the OSC for Arduino Package). Read more about the lightweight [SLIP encoding](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol).

To receive an OSCMessage, wait for the end of SLIP Stream, and fill an empty OSCMessage with the available bytes:

```C++
//make an empty message to fill with the incoming data
OSCMessage msg;
//wait for the end of the packet to be received
while(!SLIPSerial.endofPacket()){
	int size = SLIPSerial.available();
	if (size > 0){
		//fill the msg with all of the available bytes
		while(size--){
			msg.fill(SLIPSerial.read());
		}
	}
}
```

Now you can query and use the data you received:

```C++
//returns true if the data in the first position is an integer
if (msg.isInt(0)){
	//get that integer
	int data = msg.getInt(0);
}
```

### Routing / Dispatching

OSCMessages can be routed to a specific function by matching their address exactly or with an OSC pattern.

`dispatch` will do a full match on the OSCMessage's address or patterned address. 

```C++
OSCMessage msg("/a/1");
msg.dispatch("/a/1", dispatchAddress);
```

And the function definition of `dispatchAddress` could be as follows:

```C++
//called whenever an OSCMessage's address matches "/a/1"
void dispatchAddress(OSCMessage &msg){
	//do something with the OSCMessage...
	if (msg.isFloat(0)){
		float val = msg.getFloat(0);
	}
}
```

`route` does the same thing as `dispatch` but allows for partial address matching as long as they are aligned to a `/` character. 

```C++
OSCMessage msg("/b/2");
msg.route("/b", routeAddress);
```

```C++
//called whenever an OSCMessage's address matches "/b"
void routeAddress(OSCMessage &msg, int addressOffset){
	//do something with the OSCMessage...
	if (msg.isBoolean(0)){
		bool val = msg.getBoolean(0);
	}
}
```

### OSCBundles

An OSCBundle is a group of OSCMessage that can be sent and received together. 

```C++
OSCBundle bundle;
//add a new OSCMessage to the bundle with the address "/a"
OSCMessage msgA = bundle.add("/a");
//add some data to that message
msgA.add("some data");
//append another OSCMessage, this time chaining 'add' calls
bundle.add("/b").add("some more data").add("even more data");
```

Now send the OSCBundle over SLIPSerial

```C++
//start a new SLIP Packet
SLIPSerial.beginPacket();
//send the data
bundle.send(SLIPSerial);
//end the packet
SLIPSerial.endPacket();
```

### SLIP Serial

The OSC for Arduino library includes extensions of the USB serial and Hardware serial functions of the Arduino core that sends and receives data using the SLIP encoding. This makes Max/MSP and PD integration very simple using CNMAT's o.io.slipserial. The SLIPSerial library implements the same methods as the Serial object with additional `beginPacket` and `endPacket` methods to mark the boundaries of each packet in a serial stream.

When sending data, begin each packet with `SLIPSerial.beginPacket()`, then write any data to the SLIPSerial and signify the end of the packet using `SLIPSerial.endPacket()`. 

On the receiving side, in addition to the normal `read` and `available` methods of the Serial object, SLIPSerial includes `SLIPSerial.endofPacket()` which returns true when the EOT (End Of Transmission) character is received, marking the end of the data packet. 

# Oscuino

As well as many small examples illustrating the API, there is a larger application called "oscuino" that illustrates how to use OSC to simplify situations Firmata and Maxuino are typically used in.

# Support

### IDEs

Arduino 1.8.5 (tested for Due Serial and Teensy so far)

Best Supported Board:
The Teensy 3.0 and 3.1 and LC have the performance and memory that afford rich OSC implementations.
Our primary test platform for new development is the Teensy 3.2 which currently offers the best performance
of any of the Arduinos and variants. We greatly appreciate Paul Stoffregen's ongoing work
with "best practice" engineering of high performance microcontrollers.

### Unsupported boards

Arduino Yun and related openwrt/arduino hybrids (e.g. Draguino):

Marco Brianza is exploring these interesting approaches to running this OSC library on the Atmel 32u4 in the Yun:
	https://github.com/cylinderlight/udp2serial
	https://github.com/cylinderlight/udp2serialSPI

The Yun still lacks the Linux-side support to reliably move data between the 32u4 and the router's cpu. We recommend that you add a Teensy to the USB port of an OpenWrt router to get good performance and reliability with our library.

# Testing

OSC for Arduino comes with a small suite of tests to validate its functionality and test compatibility when new platforms come out. The tests are broken into a number of individual `.ino` files located in the `test` folder. 

The tests require [ArduinoUnit](https://github.com/mmurdoch/arduinounit) to be installed in the `libraries` folder. The results of the test are printed to the Serial console. 

Tested on:

* Esplora
* Leonardo
* Teensy 3.x
* Mega 2560

# Performance

Currently best performance is achieved with Arduinos with built-in USB Serial, i.e. Teensy 3.0, Teensy 2.0 and 2.0++ and Leanardo variants (12Mbps max). 

This is because the Wiznet 5100 used in the Ethernet Arduino and shields uses really slow SPI (0.3Mbps). This will change as people retool to use the much faster Wiznet 5200 which has been measured with the Due at 6Mbps.

References: 
* http://forum.pjrc.com/threads/17951-WIZ820io-Ethernet-and-2nd-power-supply-with-teensy-3-0
* http://arduino.cc/forum/index.php?topic=139147.0

The serial examples use a 9600 baud rate which is reliable on most of the FTDI based Arduinos. The slow rate is required for Arduino's without clock chips such as the TinyLili. Once you have established that things work at 9600 baud you will find it very beneficial to increase the rate. e.g. `Serial.begin(345600);   // !! 115200, 230400, 345600,   460800 X`

# Roadmap

* support for special OSC types in CNMAT's "o." especially subbundles
* examples for recent OSC support in node.js and Node Red
* nested bundles
* performance tuning
* integrate spark core examples
* Better Time Tags that avoid the overflow limitation of Arduino timer code
* Time Tag synchronization
* Bluetooth LE 
* TCP/IP Examples
* examples for more applications (i.e. TouchOSC, Processing with SLIP)
* deadline scheduling of OSC 64-bit timetags
* Support for Arduino WiFi
* ADK support

We welcome and appreciate your contributions and feedback.

# New in this release
ESPxx support