#include <HardwareSerial.h>

#define BUTTON_PIN 4
#define LED_PIN 2
#define MY_ADDRESS 1301
// #define DESTINATION_ADDRESS 1302 
#define DESTINATION_ADDRESS 1400 // test receiver
#define BROADCAST_ADDRESS 0

/********** AT COMMAND SET ************
AT+SEND=<destination_address>,<length_of_data>,<data>

AT: This prefix is used for "Attention" and is common to all AT commands, indicating the start of a command line.
+SEND: This command is used to send data through the LoRa module.
0: This is the first parameter of the +SEND command, which typically specifies the destination address where the data should be sent. In the case of 0, it often denotes a broadcast to all devices on the network or a default address if addressing has not been specifically set up.
1: The second parameter represents the number of bytes of data that will be sent in the message. In this case, 1 indicates that a single byte of data will be transmitted.
1: The third parameter is the actual data to be sent. Here, 1 is the data byte being transmitted. This could represent a command or a value, such as turning a device on or off, depending on the context of your application.
*/

bool ledState = false;
String msg_sent;
String msg_received;

unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers


void setup() {
  // put your setup code here, to run once:
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, ledState);

  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1);

  Serial.println("Waiting for serial");
  while (!Serial || !Serial1){
    delay(10);
  }

  Serial.println("Setting local address to " + String(MY_ADDRESS));
  Serial1.println("AT+ADDRESS=" + String(MY_ADDRESS));
  delay(1000);
  Serial1.println("AT+ADDRESS?"); 
  
  msg_received = Serial1.readStringUntil('\n');
  if (msg_received && msg_received.startsWith("+OK")) {
    msg_received = Serial1.readStringUntil('\n');
    Serial.print("Success: ");
    Serial.println(msg_received);
  } else {
    Serial.println("[ERROR] Failed to set transceiver address");
  }  
}

void loop() {
  int buttonState = digitalRead(BUTTON_PIN);

  // check if the button is pressed (button pin goes LOW) and the last debounce time has elapsed
  if (buttonState == LOW && (millis() - lastDebounceTime) > debounceDelay) {
    lastDebounceTime = millis();  // reset the debouncing timer

    // print and toggle LED
    Serial.println("Button pressed");
    ledState = !ledState;      
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);

    msg_sent = "AT+SEND=" + String(DESTINATION_ADDRESS) + ",1,1";

    // send message
    Serial1.println(msg_sent); 
    Serial.println("Message transmitted: " + msg_sent);

    // wait for button release to reset the debounce timer
    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(10);  // small delay to help with debouncing
    }
  }

  // TODO set a timer to go into sleep mode. The timer should last as long as the gate takes to open/close and 
  // send a response code. Based on that code, blink a light.
  // perhaps also have the receiver send a code as soon as it enters a state. Use that to turn on the light (blink when complete)

  // TODO create a server/monitor sketch that can do everything the transmitter can do AND records information; use this for the api

}
