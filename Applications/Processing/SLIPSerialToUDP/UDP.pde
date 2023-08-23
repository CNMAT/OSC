//UDP communication
UDP udp;

int inPort = 9000;
int outPort = 9001;
String ipAddress = "127.0.0.1";

void setupUDP() {
  udp = new UDP( this, inPort );
  //udp.log( true );     // <-- printout the connection activity
  udp.listen( true );
}

void stopUDP() {
  udp.close();
}

void UDPSendBuffer(byte[] data) {
  udp.send( data, ipAddress, outPort );
}

//called when UDP receives some data
void receive( byte[] data) {
  drawIncomingUDP();
  //send it over to serial
  serialSend(data);
}
