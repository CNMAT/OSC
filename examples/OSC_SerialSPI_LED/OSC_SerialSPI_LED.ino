
// Marco Brianza
// May 15 2014

#include <OSCMessage.h>
#include <SLIPEncodedSPISerial.h>
#include <StreamSPI.h>

#define OUT_PIN 13

SLIPEncodedSPISerial SLIPSerial(StreamSPI0); //connection with Yun

// ---------------------------------------

void setup(){   
 //Serial.begin(115200); //debug serial on usb
 SLIPSerial.begin();   //speed of linino serial
}
  
// - ----------------------------------------  


  
void loop(){
OSCMessage msg;  
int s;

if (SLIPSerial.available()){
while(!SLIPSerial.endofPacket())
//Serial.println("new");
      if( (s =SLIPSerial.available()) > 0)
      
      {
       while(s--)
       msg.fill(SLIPSerial.read());
      }
        if(!msg.hasError())
        {
          msg.dispatch("/All/R",All_R); 
          
          }
     }           
} // end main---------------------



 
 //------------------------
 
 void All_R(OSCMessage &msg)
 {
     float r=msg.getFloat(0); 
     int R=int(r*2.55);
     if (R>255) R=255;
     if (R<0) R=0;
     
//     Serial.print("/All/R");
//     Serial.println(R);
   analogWrite(OUT_PIN,R);
     
   msgReply(R);  
 }
 

void msgReply(float ledI){
 OSCMessage msg("/reply");   
  msg.add(ledI);
  SLIPSerial.beginPacket(); // mark the beginning of the OSC Packet       
  msg.send(SLIPSerial);
  SLIPSerial.endPacket(); 
  msg.empty();
}


  
 
 



