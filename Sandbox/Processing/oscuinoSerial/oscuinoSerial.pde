import com.illposed.osc.utility.*;
import com.illposed.osc.*;
import processing.serial.*;

// The serial port:
Serial arduino;      
//the slip decoder/encoder
SLIPEncodedSerial SLIPSerial;

void setup() {
  frameRate(200);
  //  I use the first port which is usually the Arduino
  arduino = new Serial(this, Serial.list()[0], 115200);
  SLIPSerial= new SLIPEncodedSerial(arduino);
  //setup the drawing output
  size(320, 100);
  background(0);
}


void draw() {
  OSCByteArrayToJavaConverter converter = new OSCByteArrayToJavaConverter(); 
  byte[] buffer = new byte[512];
  int len = 0;
  int avail  = SLIPSerial.available ();
  while(avail>0){
    buffer[len++] = SLIPSerial.read();
    avail--; 
  }
  //while (SLIPSerial.available () > 0) {
    
    //;
//    /len++;
  //}
  //turn the buffer into OSC
  OSCPacket pkt = converter.convert(buffer, len);
    // = new OSCBundle();
  //pkt.getAddress();
  //Object[] data = msg.getArguments();
  //println(data.length);
  //OSCBundle bndl = OSCBundle();
  //bndl.addPacket(pkt);
}

//SLIP ENCODED SERIAL 
//encodes/decodes SLIP coming from the arduino's serial port
class SLIPEncodedSerial {
  private int bytesAvailable;
  private int bytesUnavailable;

  //for the ring buffer
  private int head;
  private int tail;

  //store the bytes in a buffer
  private int SLIP_SERIAL_BUFFER_SIZE = 512;
  private byte[] buffer; 

  //the serial object
  private Serial serial;

  //some slip constants
  private byte eot = (byte) 0300;
  private byte slipesc = (byte) 0333;
  private byte slipescend = (byte) 0334;
  private byte slipescesc = (byte) 0335;

  public SLIPEncodedSerial (Serial ser) {
    buffer = new byte[SLIP_SERIAL_BUFFER_SIZE];
    serial = ser;
    bytesAvailable = 0;
    bytesUnavailable = 0;
    head = 0;
    tail = 0;
  }

  //adds a byte to the buffer
  private void addToBuffer(byte toAdd) {
    //increment the head
    int i = (head + 1) % SLIP_SERIAL_BUFFER_SIZE;
    //check that there is still space in the buffer
    if (i != tail) {
      //put the char in the buffer
      buffer[head] = toAdd;
      head = i;
      //increment the counter
      bytesUnavailable++;
    }
  }

  //pulls a byte off the buffer
  private byte getFromBuffer() {
    if ((bytesAvailable < 1) || (tail == head)) {
      return -1;
    } 
    else {		
      bytesAvailable--;
      byte c = buffer[tail];
      //increment the tail
      tail = (int)(tail + 1) % SLIP_SERIAL_BUFFER_SIZE;
      return c;
    }
  }

  //does the slip decoding and adds it to the buffer
  private void decodeSLIP() {
    byte inByte = (byte) serial.readChar();
    if (inByte==slipesc) {
      //then read the next one
      byte next = (byte) serial.readChar();
      if (next==slipescend) { 
        addToBuffer(eot);
      } 
      else if (next==slipescesc) {
        addToBuffer(slipesc);
      }
    } 
    //if its the end of transmission
    else if (inByte==eot) {
      //add the unavailable chars to the list of available
      bytesAvailable += bytesUnavailable;
      bytesUnavailable = 0;
    } 
    else {
      addToBuffer(inByte);
    }
  }

  //returns the number of bytes in the buffer
  public int available() {
    while (serial.available () > 0) {
      decodeSLIP();
    }
    return bytesAvailable;
  }

  //returns the number of bytes in the buffer
  public byte read() {
    return getFromBuffer();
  }
}

