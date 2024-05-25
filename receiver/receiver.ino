#include <HardwareSerial.h>

// LoRa
#define MY_ADDRESS 1302
#define SOURCE_ADDRESS 1301
#define BROADCAST_ADDRESS 0
#define SERVER_ADDRESS 1300

// pins
#define GATE_FORWARD_CURRENT_PIN 0
#define GATE_FORWARD_ENABLE_PIN 1
#define GATE_FORWARD_PWM_PIN 2
#define GATE_REVERSE_CURRENT_PIN 4
#define GATE_REVERSE_ENABLE_PIN 6
#define GATE_REVERSE_PWM_PIN 7

#define GATE_LIMIT_PIN 9
#define MOTOR_LIMIT_PIN 8
#define LOCK_PIN 10
#define LED_PIN 8

// PWM settings
//#define PWM_FREQ 5000
//#define GATE_FORWARD_PWM_CHANNEL 0
//#define GATE_REVERSE_PWM_CHANNEL 1
//#define PWM_RESOLUTION 8

#define GATE_SPEED_PERCENT 100
#define UNLOCK_TIME 3 // time in seconds that the lock stays open
#define STAY_OPEN_TIME 4 // time the gate stays open before closing again

#define GATE_CURRENT_THRESHOLD 819 // 4 amp threshold for overcurrent condition (for 10-bit ADC: 0-1023)

#define DEBOUNCE_DELAY 50

/********** AT COMMAND SET ************
AT+SEND=<destination_address>,<length_of_data>,<data>

AT: This prefix is used for "Attention" and is common to all AT commands, indicating the start of a command line.
+SEND: This command is used to send data through the LoRa module.
0: This is the first parameter of the +SEND command, which typically specifies the destination address where the data should be sent. In the case of 0, it often denotes a broadcast to all devices on the network or a default address if addressing has not been specifically set up.
1: The second parameter represents the number of bytes of data that will be sent in the message. In this case, 1 indicates that a single byte of data will be transmitted.
1: The third parameter is the actual data to be sent. Here, 1 is the data byte being transmitted. This could represent a command or a value, such as turning a device on or off, depending on the context of your application.
*/

enum GateState {
  STOPPED,
  CLOSING,
  OPENING,
  CLOSED,
  OPEN,
};

bool ledState = false;
String msg_sent;
String msg_received;
GateState lastDirection = CLOSING;
GateState currentState = CLOSED; 
bool isLocked = true;
unsigned long lastDebounceTime = 0;
unsigned long unlockTimestamp = 0;
unsigned long openTimestamp = 0; // time at which the gate reaches open position
unsigned short gateSpeedPercent = GATE_SPEED_PERCENT;
unsigned short unlockTime = UNLOCK_TIME;
unsigned short stayOpenTime = STAY_OPEN_TIME;
unsigned short currentThreshold = GATE_CURRENT_THRESHOLD;

void setup() {
  // motor driver pin modes
  pinMode(GATE_FORWARD_CURRENT_PIN, INPUT);
  pinMode(GATE_FORWARD_ENABLE_PIN, OUTPUT);
  pinMode(GATE_FORWARD_PWM_PIN, OUTPUT);
  pinMode(GATE_REVERSE_CURRENT_PIN, INPUT);  
  pinMode(GATE_REVERSE_ENABLE_PIN, OUTPUT);
  pinMode(GATE_REVERSE_PWM_PIN, OUTPUT);

  // other pin modes
  pinMode(GATE_LIMIT_PIN, INPUT_PULLUP);
  pinMode(MOTOR_LIMIT_PIN, INPUT_PULLUP);
  pinMode(LOCK_PIN, OUTPUT);

  pinMode(LED_PIN, OUTPUT);

  // initialize pin states
  digitalWrite(GATE_FORWARD_ENABLE_PIN, LOW);
  digitalWrite(GATE_REVERSE_ENABLE_PIN, LOW);
  digitalWrite(GATE_FORWARD_PWM_PIN, LOW);
  digitalWrite(GATE_REVERSE_PWM_PIN, LOW);
  digitalWrite(LOCK_PIN, LOW);

  digitalWrite(LED_PIN, ledState);

  Serial.begin(115200); // USB
  Serial1.begin(115200, SERIAL_8N1); // UART TX/RX pins

  Serial.println("Waiting for serial");
  while (!Serial || !Serial1){
    delay(10);
  }

  Serial.println("Setting local address to " + String(MY_ADDRESS));
  Serial1.println("AT+ADDRESS=" + String(MY_ADDRESS));
  delay(1000);
  Serial1.println("AT+ADDRESS?"); 
  
  msg_received = Serial1.readStringUntil('\n');
  if (msg_received && msg_received.indexOf("+OK") >= 0) {
    msg_received = Serial1.readStringUntil('\n');
    Serial.print("Success: ");
    Serial.println(msg_received);
  } else {
    Serial.println("[ERROR] Failed to set transceiver address");
  } 
}

void loop() {

  // check for remote trigger
  if (Serial1.available()) {
    String incomingData = Serial1.readStringUntil('\n');
    Serial.println("Message received: " + incomingData);

    if (incomingData.startsWith("+RCV=" + String(SOURCE_ADDRESS) + ",1,1")) {
      Serial.println("Toggling LED");
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      handleButtonPress();
    }
  }

  // lock gate after unlockTime seconds from being unlocked
  if (currentState == OPENING && !isLocked && ((millis() - unlockTimestamp) >= (unlockTime * 1000))){
    lock();
  }

  if (currentState == OPEN && ((millis() - openTimestamp) >= stayOpenTime * 1000)){
    closeGate();
    debugPrint();
  }

  if (digitalRead(MOTOR_LIMIT_PIN) == LOW) {
    Serial.println("[WARNING] Motor limit reached!");
    stopGate();
    openGate();
    delay(500);
    stopGate();
    currentState = STOPPED;
  }
  
  // check for gate limits
  if(currentState == OPENING && digitalRead(GATE_LIMIT_PIN) == LOW) { // pull-up
    Serial.println("Gate open limit reached");
    stopGate();
    Serial.printf("Closing in %d seconds\n", stayOpenTime); 
    currentState = OPEN;
    openTimestamp = millis();
  }

  if(currentState == CLOSING && digitalRead(GATE_LIMIT_PIN) == LOW) { // pull-up
    Serial.println("Gate close limit reached");
    stopGate();
    currentState = CLOSED;
  }

  // check for current overload
  // if (analogRead(GATE_FORWARD_CURRENT_PIN) > currentThreshold || analogRead(GATE_REVERSE_CURRENT_PIN) > currentThreshold) {
  //   Serial.println("[ERROR] Too much current from gate!");
  //   stopGate();
  // }
  
}

void unlock() {
  Serial.println("Unlocking gate");
  digitalWrite(LOCK_PIN, HIGH); 
  unlockTimestamp = millis();
  isLocked = false;
}

void lock() {
  Serial.println("Locking gate");
  digitalWrite(LOCK_PIN, LOW);
  isLocked = true;
}

void handleButtonPress() {
  
  switch (currentState) {    
    case STOPPED:
      if (lastDirection == CLOSING)
        openGate();
      else
        closeGate();      
      break;
    case OPEN:
      closeGate();
      break;
    case CLOSED:
      unlock(); 
      openGate();  
      unlockTimestamp = millis();
      Serial.printf("Unlocking for %s seconds\n", String(unlockTime));
      break;
    case CLOSING:
    case OPENING:
    default:
      stopGate();
      break;
  }
  debugPrint();
}

void debugPrint() {
  // Debugging prints
//  delay(1500);
//  Serial.print("GATE_REVERSE_ENABLE_PIN: ");
//  Serial.println(digitalRead(GATE_REVERSE_ENABLE_PIN));
//  Serial.print("GATE_FORWARD_ENABLE_PIN: ");
//  Serial.println(digitalRead(GATE_FORWARD_ENABLE_PIN));
//  Serial.print("GATE_REVERSE_PWM_PIN: ");
//  Serial.println(analogRead(GATE_REVERSE_PWM_PIN));
//  Serial.print("GATE_FORWARD_PWM_PIN: ");
//  Serial.println(analogRead(GATE_FORWARD_PWM_PIN));
}
    


void openGate() {
  Serial.println("Opening gate");
  currentState = OPENING;
  lastDirection = OPENING;
  digitalWrite(GATE_REVERSE_ENABLE_PIN, LOW);
  digitalWrite(GATE_FORWARD_ENABLE_PIN, HIGH);
  digitalWrite(GATE_REVERSE_PWM_PIN, LOW);
  analogWrite(GATE_FORWARD_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
//  ledcWrite(GATE_FORWARD_PWM_CHANNEL, percentageToDutyCycle(gateSpeedPercent));
}

void closeGate() {
  Serial.println("Closing gate");
  currentState = CLOSING;
  lastDirection = CLOSING;
  digitalWrite(GATE_FORWARD_ENABLE_PIN, LOW);
  digitalWrite(GATE_REVERSE_ENABLE_PIN, HIGH);
  digitalWrite(GATE_FORWARD_PWM_PIN, LOW);
  analogWrite(GATE_REVERSE_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
//  ledcWrite(GATE_REVERSE_PWM_CHANNEL, percentageToDutyCycle(gateSpeedPercent));
}

void stopGate() {
  Serial.println("Stopping gate");
  digitalWrite(GATE_FORWARD_ENABLE_PIN, LOW);
  digitalWrite(GATE_REVERSE_ENABLE_PIN, LOW);
  analogWrite(GATE_FORWARD_PWM_PIN, 0);
  analogWrite(GATE_REVERSE_PWM_PIN, 0);
//  ledcWrite(GATE_FORWARD_PWM_CHANNEL, percentageToDutyCycle(0));
//  ledcWrite(GATE_REVERSE_PWM_CHANNEL, percentageToDutyCycle(0));
  currentState = STOPPED;
}

int percentageToDutyCycle(int percentage) {
  if (percentage < 0) {
      percentage = 0;  // Ensure the percentage is not negative
  } else if (percentage > 100) {
      percentage = 100;  // Cap the percentage at 100%
  }

  int dutyCycle = map(percentage, 0, 100, 0, 255);
  Serial.printf("Duty cycle set to %d => %d\n", percentage, dutyCycle);
  return dutyCycle;
}

void sendMessage(String msg, String address="") { // if no address is passed, broadcast
    if (address == "") 
        address = String(SERVER_ADDRESS);
    
    Serial1.println("AT+SEND=" + address + "," + String(msg.length()) + "," + msg);
}
