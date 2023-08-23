
ControlP5 cp5;

void setupGUI() {
  //the ControlP5 object
  cp5 = new ControlP5(this);

  //start button
  cp5.addButton("START")
    .setPosition(width/2 - 100, height-30)
    .setSize(200, 20);
  
  //stop button
  cp5.addButton("STOP")
    .setPosition(width/2 - 100, height-30)
    .setSize(200, 20)
    .hide();

  //setup the baud list
  DropdownList baudRate = cp5.addDropdownList("BaudRate")
    .setPosition(50, 130)
    .setSize(200, 90)
    .setItemHeight(20)
    .setBarHeight(20)
    .setId(4).close();
  ControllerStyle baudRateStyle = baudRate.getCaptionLabel().getStyle();
  baudRate.getCaptionLabel().set("SELECT THE BAUD RATE");
  baudRateStyle.marginTop = baudRateStyle.marginLeft = baudRateStyle.marginTop = 3;
  //the baud options
  for (int i=0; i<serialRateStrings.length; i++) {
    String baudString = serialRateStrings[i];
    baudRate.addItem(baudString, i);
  }

  //Serial Port selector
  DropdownList serialddl = cp5.addDropdownList("SerialPort")
    .setPosition(50, 30)
    .setSize(200, 90)
    .setItemHeight(20)
    .setBarHeight(20)
    .setId(0).close();
  ControllerStyle serialStyle = serialddl.getCaptionLabel().getStyle();
  serialddl.getCaptionLabel().set("SELECT ARDUINO SERIAL PORT");
  serialStyle.marginTop = serialStyle.marginLeft = serialStyle.marginTop = 3;
  //set the serial options
  String SerialList[] = Serial.list(); 
  for (int i=0; i<SerialList.length; i++) {
    serialddl.addItem(SerialList[i], i);
  }
  
  //udp IP/port
  cp5.addTextfield("IP address")
    .setPosition(300, 30)
    .setAutoClear(false)
    .setText(ipAddress);

  cp5.addTextfield("Incoming Port Number")
    .setPosition(300, 80)
    .setAutoClear(false)
    .setText(str(inPort));

  cp5.addTextfield("Outgoing Port Number")
    .setPosition(300, 130)
    .setAutoClear(false)
    .setText(str(outPort));

  //text labels
  cp5.addTextlabel("arduinoLabel")
    .setText("Serial")
    .setPosition(50, 10)
    .setColorValue(0xffffff00)
    .setFont(createFont("SansSerif", 11));

  cp5.addTextlabel("UDPLabel")
    .setText("UDP")
    .setPosition(300, 10)
    .setColorValue(0xffffff00)
    .setFont(createFont("SansSerif", 11));

  cp5.addTextlabel("incomingPacketLabel")
    .setText("Incoming Packet")
    .setPosition(210, 100)
    .setColorValue(0xffffff00)
    .setFont(createFont("SansSerif", 10))
    .hide();
}

void controlEvent(ControlEvent theEvent) {
  String eventName = theEvent.getName();
  if (eventName == "SerialPort") {
    //set the serial port 
    serialListNumber = int(theEvent.getValue());
  } else if (eventName == "BaudRate") {
    int index = int(theEvent.getValue());
    baud = Integer.parseInt(serialRateStrings[index]);
  } else if (eventName == "IP address") {
    cp5.get(Textfield.class, eventName).setFocus(false);
    ipAddress = theEvent.getStringValue();
  } else if (eventName == "Incoming Port Number") {
    cp5.get(Textfield.class, eventName).setFocus(false);
    inPort = Integer.parseInt(theEvent.getStringValue());
  } else if (eventName == "Outgoing Port Number") {
    cp5.get(Textfield.class, eventName).setFocus(false);
    outPort = Integer.parseInt(theEvent.getStringValue());
  }
}

//hide all the controls and show the stop button
void hideControls() {
  //HIDE
  cp5.get(Button.class, "START").hide();
  cp5.get(Textfield.class, "IP address").hide();
  cp5.get(DropdownList.class, "BaudRate").hide();
  cp5.get(DropdownList.class, "SerialPort").hide();
  cp5.get(Textfield.class, "Outgoing Port Number").hide();
  cp5.get(Textfield.class, "Incoming Port Number").hide();
  //SHOW
  cp5.get(Button.class, "STOP").show();
  cp5.get(Textlabel.class, "incomingPacketLabel").show();
}

void showControls() {
  //SHOW
  cp5.get(Button.class, "START").show();
  cp5.get(Textfield.class, "IP address").show();
  cp5.get(DropdownList.class, "BaudRate").show();
  cp5.get(DropdownList.class, "SerialPort").show();
  cp5.get(Textfield.class, "Outgoing Port Number").show();
  cp5.get(Textfield.class, "Incoming Port Number").show();
  //HIDE
  cp5.get(Button.class, "STOP").hide();
  cp5.get(Textlabel.class, "incomingPacketLabel").hide();
}

//start everything
public void START(int theValue) {
  setupUDP();
  setupSerial();
  hideControls();
  applicationRunning = true;
}

public void STOP() {
  stopSerial();
  stopUDP();
  showControls();
  applicationRunning = false;
}
