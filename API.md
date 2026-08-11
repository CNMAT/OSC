# OSCMessage

An OSCMessage is an address followed by any number of data. Messages can have mixed data types like an integer followed by a string followed by a float, etc. 

## Constructor

OSCMessages can be constructed with or without an address. 

### `OSCMessage(const char *)`

Set the address of the message in the constructor

```C++
OSCMessage msg("/address");
```

### `OSCMessage()`

An OSCMessage constructed without an address is not valid until it is given an address.

## Add/Set Data


### `OSCMessage& add(int i)`

Append an integer to the OSCMessage. 

```C++
msg.add(1);
```


### `OSCMessage& add(float f)`

Append a float to the OSCMessage.



### `OSCMessage& add(bool b)`

Append a boolean to the OSCMessage.



### `OSCMessage& add(const char * str)`

Append a string to the OSCMessage.

```C++
msg.add("hello");
```


### `OSCMessage& add(uint8_t * blob, int length)`

Append a [blob](https://en.wikipedia.org/wiki/Binary_large_object) to the OSCMessage. Pass in the length of the blob as the second argument. 



### `OSCMessage& set(int position, Type data)`

Replace the data at the given position with the data. `Type` can be any of the supported data types. 

```C++
//replace the data at the 0th position with a string
msg.set(0, "string");
```


### `OSCMessage& set(int position, uint8_t * data, int length)`

Set the data at the given position to be a blob of the given length. 


### `OSCMessage& add(double d)`

Append a double precision floating point value to the OSCMessage. NOTE: double is not supported on most Arduino platforms. It will fall back to float, when double is not supported. 

### `OSCMessage& add(int64_t h)`

Append a 64-bit integer, OSC type tag `'h'`.

### `OSCMessage& add(osctime_t time)`

Append an OSC timetag, type tag `'t'`. `immediatetime` is
`0x0000000000000001`, OSC 1.0's "immediately"; `zerotime` is 1900-01-01.

### `OSCMessage& add(oscrgba_t rgba)`

Append an RGBA colour, type tag `'r'`. Sent in the OSC 1.0 order red, green,
blue, alpha. **This changed in 4.0.0** — 3.5.8 and earlier reversed the four
bytes, so a colour sent by an older version arrives inverted and vice versa.

### `OSCMessage& add(oscmidi_t midi)`

Append a MIDI message, type tag `'m'`. Sent as port, status, data1, data2.
**This changed in 4.0.0**: earlier versions reversed the bytes and dropped
`data2`. OSC's `'m'` type has no channel slot of its own, so the struct's
`channel` field is folded into the low nibble of the status byte on encode —
setting the channel directly in `status` and leaving `channel` at 0 gives the
same bytes.

### `OSCMessage& add(oscevent_t event)`

Append a valueless argument: `OSC_IMPULSE` gives type tag `'I'`, anything else
gives `'N'` (Null). Both occupy a type tag and no payload bytes.

## Get Data


### `intOSC_t getInt(int position)`

Returns the integer at the given position. `intOSC_t` is `int32_t`; OSC's `'i'`
type is 32 bits wide on every platform.

```C++
//returns the integer at the third position
msg.getInt(2);
```

### `float getFloat(int position)`

Returns the float at the given position



### `bool getBoolean(int position)`

Returns the boolean at the given position



### `double getDouble(int position)`

Returns the double at the given position. NOTE: double is not supported by most Arduino platforms. This will fail silently if double is not supported.

### `int getString(int position, char * strBuffer)`

Copy the string’s characters into the `strBuffer`, without any safety check. 
Returns the number of copied characters. 

### `int getString(int position, char * strBuffer, int length)`

Copy the string’s characters into the `strBuffer`, after checking that this doesn’t exceed the buffer’s `length`. 
Returns the number of copied characters. NOTE that if the string length is greater than the available buffer length, then NO characters are copied.

### `int getString(int position, char * strBuffer, int length, int offset, int size)`

Copy `size` number of characters from the given `offset` into the `strBuffer`, after checking that this doesn’t exceed the buffer’s `length`. Returns `size`, even if the number of copied characters is lower. 
```C++
char str[8];
//fill str with 8 characters from the 0th datum
msg.getString(0, str, 8, 0, 8);
```

### `int getBlob(int position, uint8_t * blobBuffer)`

Directly copy the blob’s bytes into the `blob` buffer (without safety-check). 
Returns the number of bytes from the blob. 


### `int getBlob(int position, uint8_t * blobBuffer, int length)`

Copy the blob's bytes into the given `blobBuffer`, if the blob's size doesn’t exceed the blobBuffer's `length`. 
Returns the number of bytes copied from the blob. NOTE that if the blob length is greater than the available buffer length, then NO bytes are copied.

### `int getBlob(int position, uint8_t * blobBuffer, int length, int offset, int size)`

Copy `size` bytes from the blob, starting from `offset`,  into the given `blobBuffer`, if the size doesn’t exceed the buffer’s  `length` or the blob’s data length. 
Returns the number of bytes copied from the blob. NOTE that if the requested size is greater than *either* the available buffer length *or* the (partial) blob length, then NO bytes are copied.


### `const uint8_t* getBlob(int position)`

Get a pointer to blob data.


### `uint32_t getBlobLength(int position)`

Returns the length of the blob in bytes. 


### `char getType(int position)`

Returns the type of the data at the given position. 

```C++
OSCMessage msg("/address");
msg.add(1);
msg.getType(0); //-> returns 'i'
```


## Query Data

### `bool isInt(int position)`

Returns `true` when the data at the given position is an integer.

### `bool isFloat(int position)`

Returns `true` when the data at the given position is a float.

### `bool isBoolean(int position)`

Returns `true` when the data at the given position is a boolean.

### `bool isString(int position)`

Returns `true` when the data at the given position is a string.

### `bool isBlob(int position)`

Returns `true` when the data at the given position is a blob.

### `bool isDouble(int position)`

Returns `true` when the data at the given position is a double.

### `int size()`

Returns the number of data the OSCMessage has. 

### `int bytes()`

Returns the size of the OSCMessage in bytes (if everything is 32-bit aligned). 



## Address

### `OSCMessage& setAddress(const char * address)`

Set the address of the OSCMessage. 

### `int getAddress(char * str, int offset=0)`

Copy the address of the OSCMessage into the `str` buffer. Copy after the given address `offset` (defaults to 0). Returns the length of the resulting string. If the offset is past the end of the address an empty string / zero length are returned.

### `int getAddress(char * str, int offset, int len)`

Copy a maximum of len characters of the address of the OSCMessage into the `str` buffer, starting at at the given address `offset`. Returns the length of the resulting string. If the offset is past the end of the address an empty string / zero length are returned.

### `int getAddressLength(int offset=0)`

Returns the length of the OSCMessage's address, starting after the given address `offset` (defaults to 0). If the offset is 
greater than the address length then it returns zero.

### `const char* getAddress()`

Get a pointer to the address as a C string. 

## Send Receive

### `OSCMessage& send(Print &p)`

Output the message to the given transport layer which extends Arduino's [Print class](http://playground.arduino.cc/Code/Printclass) like the `Serial` out. 

```C++
msg.send(SLIPSerial);
```

### `OSCMessage& fill(uint8_t incomingByte)`

Add the incoming byte to the OSCMessage where it will be decoded. 

### `OSCMessage& fill(uint8_t * bytes, int length)`

Add and decode the array of `bytes` as an OSCMessage. 



## Matching / Routing

### `bool fullMatch( const char * pattern, int offset = 0)`

Returns true if the message's address is a full match to the given `pattern` after the `offset`. 

```C++
OSCMessage msg("/a/0");
msg.fullMatch("/0", 2); // ->returns true
```

### `int match( const char * pattern, int offset = 0)`

Returns the number of matched characters of the message's address against the given `pattern` (optionally with an `offset`). Unlike `fullMatch`, `match` allows for partial matches

```C++
OSCMessage msg("/a/0");
msg.match("/a"); // ->returns 2
```

### `bool dispatch(const char * pattern, void (*callback)(OSCMessage &), int offset = 0)`

Invoke the given `callback` if the address is a full match with the `pattern` (after the `offset`). The message is passed into the callback function. Returns true if the pattern was a match and the callback function was invoked. 

### `bool route(const char * pattern, void (*callback)(OSCMessage &, int), int offset = 0)`

Invoke the given `callback` if the address if a match with the `pattern` (after the `offset`). The OSCMessage and the address offset is passed into the callback function. Returns true if the pattern was a match and the callback function was invoked. 

```C++
//define a callback function for matching messages
void routeCallback(OSCMessage & message, int addressOffset){
	//do something with the message...

	//with the message below, the addressOffset will equal 2.
}

OSCMessage msg("/a/0");
msg.route("/a", routeCallback);
```


## Address Patterns

OSCMessages can be constructed with patterns and later routed or dispatched against addresses. 

```C++
OSCMessage msg("/{a,b}/[0-9]");
msg.route("/a/0", a0_callback); //matches the address
msg.route("/b/2", b2_callback); //matches the address
msg.route("/c/11", c11_callback); //not invoked
```

# OSCBundle

A bundle is a group of OSCMessages with a timetag. 


## Constructor

### `OSCBundle()`

Construct an empty OSCBundle. 

### `OSCBundle(osctime_t = immediatetime)`

Construct the bundle with a timetag. Defaults to `immediatetime`
(`0x0000000000000001`), the value OSC 1.0 reserves for "immediately". Pass
`zerotime` explicitly for a timetag of 0, which means 1900-01-01, not
"immediately" -- 3.5.8 and earlier sent 0 by default. 



## Add OSCMessage

### `OSCMessage & add(const char * address)`

Create a new message with the given `address` in the bundle. Returns the newly created OSCMessage. 

```C++
//create a new OSCMessage and add some data to it
bundle.add("/message").add("data");
```


## Get OSCMessage

### `OSCMessage * getOSCMessage(int position)`

Return the OSCMessage in the bundle at the given `position`.

```C++
OSCBundle bundle
bundle.add("/a");
bundle.add("/b");
bundle.getOSCMessage(0);//returns the OSCMessage with the address "/a".
```

### `OSCMessage * getOSCMessage(char * address)`

Return the OSCMessage in the bundle which matches the given address. 

```C++
OSCBundle bundle
bundle.add("/a");
bundle.add("/b");
bundle.getOSCMessage("/b");//returns the second OSCMessage in the bundle
```


## Routing

### `bool dispatch(const char * pattern, void (*callback)(OSCMessage&), int offset = 0)`

Invoke the `callback` function with all messages in the bundle which match the given pattern after the offset. 

```C++
bundle.add("/a/0");
bundle.add("/b/0");
bundle.dispatch("/0", dispatchZero, 2);
```

### `bool route(const char * pattern, void (*callback)(OSCMessage &, int), int offset = 0)`

Invoke the `callback` with all the OSCMessages in the bundle which match the given `pattern`. `route` allows for partial matches. 



## Send/Receive

### `OSCBundle& send(Print &p)`

Output the bundle to the given transport layer which extends Arduino's [Print class](http://playground.arduino.cc/Code/Printclass) (such as `SLIPSerial` out). 

```C++
bundle.send(SLIPSerial);
```

### `OSCBundle& fill(uint8_t incomingByte)`

Add the incoming byte to the OSCBundle where it will be decoded. 

### `OSCBundle& fill(const uint8_t * bytes, int length)`

Add and decode the array of bytes as an OSCBundle. 



# OSCBufferedPrint

`send()` writes a packet in small pieces: the address, then each pad byte on its
own, the comma, each type character, more padding, then the data. That is free
on a buffered transport and expensive on one where a write costs a round trip.
On the UNO R4 WiFi and its clones every `WiFiUDP::write()` is an `AT+UDPWRITE`
command to the ESP32-S3 radio over a 115200 baud UART, and it blocks until the
answer comes back, so `/analog/0` with one integer costs eight round trips to
move twenty bytes. Wiznet Ethernet shields pay the same tax in SPI transactions.

`OSCBufferedPrint` is a `Print` that collects the packet in a buffer you supply
and passes it on in a single write. It is in `OSCBufferedPrint.h`, which nothing
else in the library includes.

### `OSCBufferedPrint(Print &sink, uint8_t *buffer, size_t capacity)`

Wraps `sink`. Nothing is allocated: the buffer is yours, and its size is a
performance knob rather than a limit — a packet larger than the buffer is not
truncated, the buffer is passed on as soon as it fills and filling carries on.

```C++
uint8_t packetbuf[128];

Udp.beginPacket(outIp, outPort);
  OSCBufferedPrint out(Udp, packetbuf, sizeof(packetbuf));
  msg.send(out);
  out.flush();
Udp.endPacket();
```

### `void flush()`

Hand what has accumulated to the sink. Not optional: bytes still in the buffer
when the packet is closed never reach the transport. A short write from the sink
leaves the remainder in the buffer for the next `flush()` rather than dropping
it out of the middle of the packet.

### `size_t pending()`

Bytes waiting for a `flush()`.



# Chaining

Many methods return `this` which enables you to string together multiple commands. 

This technique allows multiple lines to be condensed into one:

```C++
OSCMessage msg("/address");
msg.add("data").add(0).send(SLIPSerial).empty();
```

Take care when chaining off `OSCBundle::add()`: it returns a reference to the
newly created `OSCMessage`, not to the bundle. So in

```C++
bundle.add("/address").add("data").add(0).send(SLIPSerial).empty();
```

every call after `add("/address")` acts on that one message — it sends the bare
message rather than the bundle, and empties the message rather than the bundle.
To send a bundle, build it first and then send it:

```C++
bundle.add("/address").add("data").add(0);
bundle.send(SLIPSerial);
bundle.empty();
```

# SLIPEncodedSerial

A serial stream has no packet boundaries, so OSC over serial is framed with
[SLIP](https://en.wikipedia.org/wiki/Serial_Line_Internet_Protocol). Every
example that says `SLIPSerial` declares one of these; the library never
declares it for you.

`_SLIPSerial<T>` wraps any Arduino `Stream` and is aliased for the common ones:

| alias | wraps | use |
|---|---|---|
| `SLIPEncodedSerial` | `HardwareSerial` | UART, and any board whose `Serial` is USB CDC deriving from `HardwareSerial` |
| `SLIPEncodedUSBSerial` | the board's native USB CDC | only where `BOARD_HAS_USB_SERIAL` is defined |
| `SLIPEncodedTCP` | `Client` | SLIP over TCP; since 4.0.0 this is `_SLIPSerial<Client>` rather than a separate class |
| `SLIPEncodedBluetoothSerial` | `BluetoothSerial` | ESP32 classic Bluetooth only |

The portable declaration, which every example uses:

```C++
#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif
```

### `void begin(unsigned long baudrate)`

Calls `begin()` on the underlying stream. Ignored by native USB CDC ports,
which have no line rate; a bridged UART such as the UNO R4 WiFi's needs the
host to match it.

### `void beginPacket()` / `void endPacket()`

Mark the start and end of one OSC packet. **Both are required.** Since 4.0.0
outgoing bytes are collected and handed to the port in blocks, and `endPacket()`
is what flushes them — `send()` on its own may leave the packet sitting in the
buffer. See `OSC_SLIP_TX_BUFFER` to change the block size.

### `bool endofPacket()`

True once a complete frame has been received. The receive loop waits on this,
but must not wait on it forever: a stream that stops mid-packet leaves it false
and `available()` at 0 indefinitely.

### `int read()`

Returns the next decoded byte, or **-1 for "no byte"** — at end of packet, on a
malformed escape pair, and mid-packet whenever `available()` has overcounted,
which it does because a SLIP escape pair is two raw bytes for one decoded byte.
`fill()` takes `uint8_t`, so `fill(read())` narrows that -1 into an ordinary
`0xFF` byte of message content. Always check before filling:

```C++
int c = SLIPSerial.read();
if (c >= 0) msg.fill((uint8_t)c);
```

### `int available()` / `int peek()` / `void flush()`

As `Stream`. `flush()` drains the transmit buffer as well as the port's.

# Compile-time options

Define before including the library, or with a `-D` build flag.

| macro | default | effect |
|---|---|---|
| `OSC_MAX_INCOMING` | 512 on AVR, 4096 elsewhere | Cap on a single decoded bundle element or message. A peer claiming a larger size raises `BUFFER_FULL` instead of growing the buffer until `malloc` fails. Note this bounds one *element*, not a whole datagram. |
| `OSC_SLIP_TX_BUFFER` | 64 | Bytes of outgoing SLIP collected before handing them to the port. Larger means fewer writes, which matters where a write is expensive; costs that many bytes of RAM per `SLIPSerial` instance. |
| `OSC_SLIP_RX_BUFFER` | 4096 | ESP32 cores only. `begin()` passes this to the core's `setRxBufferSize()` before opening the port. The USB-Serial-JTAG parts (S3/C3/C6) drop received bytes silently once their default 256-byte ring fills — even against a sketch draining flat out — and the enlargement only works before `begin()`, so the library makes the call where the ordering cannot go wrong. Set to 0 to leave the core default alone. Bursts larger than any buffer still need host-side pacing. Measured: test/hardware/README.md. |
| `OSC_NO_TONE` | undefined | Suppresses `BOARD_HAS_TONE`, for boards whose core has no `tone()`. |
