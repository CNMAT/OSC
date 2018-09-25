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

## Get Data


### `int getInt(int position)`

Returns the integer at the given position

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

Copy the string’s characters into the `strBuffer’, without any safetcheck’. 
Returns the number of copied characters. 

### `int getString(int position, char * strBuffer, int length)`

Copy the string’s characters into the `strBuffer’, after checking that this doesn’t exceed the buffer’s ‘length’. 
Returns the number of copied characters. 

### `int getString(int position, char * strBuffer, int length, int offset, int size)`

Copy `size` number of characters from the given ‘offset’ into the `strBuffer’, after checking that this doesn’t exceed the buffer’s ‘length’. Returns the number of copied characters. 

```C++
char str[8];
//fill str with 8 characters from the 0th datum
msg.getString(str, 8, 0, 8);
```

### `int getBlob(int position, uint8_t * blobBuffer)`

Directly copy the blob’s bytes into the `blob` buffer (without safety-check). 
Returns the number of bytes from the blob. 


### `int getBlob(int position, uint8_t * blobBuffer, int length, int offset, int size)`

Copy ‘size’ bytes from the blob, starting from ‘offset’,  into the given `blobBuffer’, if the size doesn’t exceed the buffer’s (or the blob’s) ‘length’. 
Returns the number of bytes copied from the blob. 


### `int getBlobLength(int position)`

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

### `OSCMessage& getAddress(char * str, int offset=0)`

Copy the address of the OSCMessage into the `str` buffer. Copy after the given address offset (defaults to 0).


## Send Receive

### `OSCMessage& send(Print &p)`

Output the message to the given transport layer which extends Arduino's [Print class](http://playground.arduino.cc/Code/Printclass) like the `Serial` out. 

```C++
msg.send(SLIPSerial);
```

### `OSCMessage& fill(uint8_t incomingByte)`

Add the incoming byte to the OSCMessage where it will be decoded. 

### `OSCMessage& fill(uint8_t * bytes, int length)`

Add and decode the array of bytes as an OSCMessage. 



## Matching / Routing

### `bool fullMatch( const char * pattern, int offset = 0)`

Returns true if the message's address is a full match to the given pattern after the offset. 

```C++
OSCMessage msg("/a/0");
msg.fullMatch("/0", 2); // ->returns true
```

### `int match( const char * pattern, int offset = 0)`

Returns the number of matched characters of the message's address against the given pattern (optionally with an offset). Unlike `fullMatch`, `match` allows for partial matches

```C++
OSCMessage msg("/a/0");
msg.match("/a"); // ->returns 2
```

### `bool dispatch(const char * pattern, void (*callback)(OSCMessage &), int offset = 0)`

Invoke the given callback if the address if a full match with the pattern (after the offset). The message is passed into the callback function. Returns true if the pattern was a match and the callback function was invoked. 

### `bool route(const char * pattern, void (*callback)(OSCMessage &, int), int offset = 0)`

Invoke the given callback if the address if a match with the pattern (after the offset). The OSCMessage and the address offset is passed into the callback function. Returns true if the pattern was a match and the callback function was invoked. 

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

### `OSCBundle(osctime_t = zerotime)`

Construct the bundle with a timetag. timetag defaults to 0 (immediate). 



## Add OSCMessage

### `OSCMessage & add(char * address)`

Create a new message with the given address in the bundle. Returns the newly created OSCMessage. 

```C++
//create a new OSCMessage and add some data to it
bundle.add("/message").add("data");
```


## Get OSCMessage

### `OSCMessage * getOSCMessage(int position)`

Return the OSCMessage in the bundle at the given position.

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

Invoke the callback function with all messages in the bundle which match the given pattern after the offset. 

```C++
bundle.add("/a/0");
bundle.add("/b/0");
bundle.dispatch("/0", dispatchZero, 2);
```

### `bool route(const char * pattern, void (*callback)(OSCMessage &, int), int offset = 0)`

Invoke the callback with all the OSCMessages in the bundle which match the given pattern. `route` allows for partial matches. 



## Send/Receive

### `OSCBundle& send(Print &p)`

Output the bundle to the given transport layer which extends Arduino's [Print class](http://playground.arduino.cc/Code/Printclass) (such as `SLIPSerial` out). 

```C++
bundle.send(SLIPSerial);
```

### `OSCBundle& fill(uint8_t incomingByte)`

Add the incoming byte to the OSCBundle where it will be decoded. 

### `OSCBundle& fill(uint8_t * bytes, int length)`

Add and decode the array of bytes as an OSCBundle. 



# Chaining

Many methods return `this` which enables you to string together multiple commands. 

This technique allows multiple lines to be condensed into one:

```C++
bundle.add("/address").add("data").add(0).send(SLIPSerial).empty();
```
