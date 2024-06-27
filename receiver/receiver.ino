#include <HardwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <vector>
#include <queue>

#define DEBUG 0

#if DEBUG == 1
  #define MY_ADDRESS 1401
#else
  #define MY_ADDRESS 1301
#endif

#define SERVER_ADDRESS 1300

// pins
#define GATE_FORWARD_CURRENT_PIN 0
#define GATE_FORWARD_PWM_PIN 2
#define GATE_REVERSE_CURRENT_PIN 4
#define GATE_REVERSE_PWM_PIN 7

#define GATE_LIMIT_CLOSE_PIN 5 //8 // TODO change to different pin; this one is for LED
#define GATE_LIMIT_OPEN_PIN 6 //9 // TODO change to a different pin; this one causes mcu not to boot
#define LOCK_PIN 10
// #define MOTOR_LIMIT_PIN 8


#define GATE_SPEED_PERCENT 100
#define UNLOCK_TIME 5                 // time in seconds that the lock stays open
#define STAY_OPEN_TIME 4              // time the gate stays open before closing again
#define LIMIT_SWITCH_DEBOUNCE_TIME 1  // time after opening/closing that the limit switch is ignored
#define LIGHT_TIME 2 // default time lights stay on for signals

// messages
#define SUCCESS_SETUP "101"
#define INFO_OPENING "201"
#define INFO_CLOSING "202"
#define INFO_LOCKING "203"
#define ERROR_MOTOR_LIMIT "301"
#define ERROR_CURRENT_LIMIT_FORWARD "302"
#define ERROR_CURRENT_LIMIT_REVERSE "303"
#define ERROR_OPEN_LIMIT "304"
#define ERROR_CLOSE_LIMNIT "305"
#define REQUEST_FROM "901"
#define REQUEST_DENIED "902"
#define STATE_STOPPED "401"
#define STATE_CLOSING "402"
#define STATE_OPENING "403"
#define STATE_CLOSED "404"
#define STATE_OPEN "405"

enum GateState {
  STOPPED,
  CLOSING,
  OPENING,
  CLOSED,
  OPEN,
};

Adafruit_NeoPixel led = Adafruit_NeoPixel(1, 8, NEO_GRB + NEO_KHZ800);
const int RED[] = {255, 0, 0};
const int GREEN[] = {0, 255, 0};
const int BLUE[] = {0, 0, 255};
const int YELLOW[] = {255, 255, 0};
const int ORANGE[] = {255, 165, 0};
const int MAGENTA[] = {255, 0, 255};
const int CYAN[] = {0, 255, 255};
const int WHITE[] = {255, 255, 255};
const int BLACK[] = {0, 0, 0};

String msgReceived;
GateState lastDirection = CLOSING;
GateState currentState = CLOSED;
bool isLocked = true;
unsigned long unlockTimestamp = 0;
unsigned long openTimestamp = 0;  // time at which the gate reaches open position
unsigned long ledOffTimestamp = 0;
// use defines as defaults, but keep these mutable so I can change them remotely later
unsigned short unlockTime = UNLOCK_TIME;
unsigned short stayOpenTime = STAY_OPEN_TIME;
unsigned short ledTime = LIGHT_TIME;
unsigned short gateSpeedPercent = GATE_SPEED_PERCENT;

std::vector<int> allowedAddresses;
std::queue<String> messageQueue;
std::queue<String> pendingMessages;
bool awaitingResponse = false;

void addAddress(int address) { 
  allowedAddresses.push_back(address); 
}

void setAddresses(const std::vector<int>& addresses) { 
  allowedAddresses = addresses;
}

void listenForSignal();

void sendMessage(String msg, String address = String(SERVER_ADDRESS)) {
  String fullMsg = "AT+SEND=" + address + "," + String(msg.length()) + "," + msg;
  Serial.printf("Queing message \"%s\" to %s\n", fullMsg.c_str(), address.c_str());
  pendingMessages.push(fullMsg);
}

void setLedColor(const int rgb[]) {
  led.setPixelColor(0, rgb[0], rgb[1], rgb[2]);
  led.show();
}

void timedLed(const int rgb[], unsigned short seconds = ledTime) {
  setLedColor(rgb);
  ledOffTimestamp = millis();
  ledTime = seconds;
}

void processPendingMessages() {
  if (!awaitingResponse && !pendingMessages.empty()) {
    String msg = pendingMessages.front();
    pendingMessages.pop();
    Serial.printf("Sending message \"%s\"\n", msg.c_str());
    Serial1.println(msg);
    awaitingResponse = true;
  }
}

void setup() {
  led.begin();
  led.setBrightness(100);
  setLedColor(BLUE);
  // motor driver pin modes
  pinMode(GATE_FORWARD_CURRENT_PIN, INPUT);
  pinMode(GATE_FORWARD_PWM_PIN, OUTPUT);
  pinMode(GATE_REVERSE_CURRENT_PIN, INPUT);
  pinMode(GATE_REVERSE_PWM_PIN, OUTPUT);

  // other pin modes
  pinMode(GATE_LIMIT_OPEN_PIN, INPUT_PULLUP);
  pinMode(GATE_LIMIT_CLOSE_PIN, INPUT_PULLUP);
  pinMode(LOCK_PIN, OUTPUT);

  // initialize pin states
  digitalWrite(GATE_FORWARD_PWM_PIN, LOW);
  digitalWrite(GATE_REVERSE_PWM_PIN, LOW);
  digitalWrite(LOCK_PIN, LOW);

  Serial.begin(115200);               // USB
  Serial1.begin(115200, SERIAL_8N1);  // UART TX/RX pins

  Serial.println("Waiting for serial");
  while (!Serial1 || ( DEBUG && !Serial)){     
    delay(10);
  }

  setLedColor(MAGENTA);
  Serial.println("Setting local address to " + String(MY_ADDRESS));
  Serial1.println("AT+ADDRESS=" + String(MY_ADDRESS));  
  Serial1.flush();
  Serial1.println("AT+ADDRESS?");

  while(1){
    unsigned long startTime = millis();
    while (Serial1.available() == 0) {      
      if (millis() - startTime > 500) { // Timeout after 500ms
        setLedColor(BLACK);
        Serial.println("Timeout waiting for `AT+ADDRESS?` response. Sending command again.");
        Serial1.println("AT+ADDRESS?"); 
        break;
      }
      setLedColor(MAGENTA);
    }
    msgReceived = Serial1.readStringUntil('\n');
  
    if (msgReceived.startsWith("+ADDRESS=" + String(MY_ADDRESS))) {
      Serial.print("Success:");
      Serial.println(msgReceived);
      break;
    } else {
      Serial.print("ERROR: ");
      Serial.println(msgReceived);
    }
    Serial.println("LoRa unavailable, trying again");
  }

  addAddress(SERVER_ADDRESS);
  addAddress(1311);
  addAddress(1312);
  addAddress(1313);
  addAddress(1314);
  sendMessage(SUCCESS_SETUP);
  setLedColor(GREEN);
  delay(1000);
  setLedColor(BLACK);

}

int percentageToDutyCycle(int percentage) {
  if (percentage < 0) {
    percentage = 0;
  } else if (percentage > 100) {
    percentage = 100; 
  }

  return map(percentage, 0, 100, 0, 255);
}

void unlock() {
  Serial.println("Unlocking");
  sendMessage("Unlocking");
  digitalWrite(LOCK_PIN, HIGH);
  isLocked = false;
  delay(500);  // give it time to get to current
  unlockTimestamp = millis();
}

void lock() {
  Serial.println("Locking");
  sendMessage("Locking");
  digitalWrite(LOCK_PIN, LOW);
  isLocked = true;
  delay(200); // stops false LOW reading on gate limit pins
}

void openGate() {
  if (digitalRead(GATE_LIMIT_OPEN_PIN) != LOW) {
    setLedColor(GREEN);
    Serial.println("Opening gate");
    sendMessage("Opening");
    digitalWrite(GATE_REVERSE_PWM_PIN, LOW);
    analogWrite(GATE_FORWARD_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
    currentState = OPENING;
    lastDirection = OPENING;
  } else {
    Serial.println("Attempted to open gate when it is already open");
    sendMessage("Attempted to open gate when it is already open");
    closeGate();
  }
}

void closeGate() {
  if (digitalRead(GATE_LIMIT_CLOSE_PIN) != LOW) {
    setLedColor(RED);
    Serial.println("Closing gate");
    sendMessage("Closing");
    digitalWrite(GATE_FORWARD_PWM_PIN, LOW);
    analogWrite(GATE_REVERSE_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
    currentState = CLOSING;
    lastDirection = CLOSING;
  } else {
    Serial.println("Attempted to close gate when it is already closed");
    sendMessage("Attempted to close gate when it is already closed");
    openGate();
  }
}

void stopGate() {
  setLedColor(BLACK);
  Serial.println("Stopping gate");
  sendMessage("Stopping");
  analogWrite(GATE_FORWARD_PWM_PIN, 0);
  analogWrite(GATE_REVERSE_PWM_PIN, 0);
  currentState = STOPPED;
  lock();
}

void handleButtonPress() {
  switch (currentState) {
    case STOPPED:
      if (lastDirection == CLOSING)
        openGate();
      else
        closeGate();
      break;
    case CLOSED:
      unlock();
      openGate();
      Serial.printf("Unlocking for %s seconds\n", String(unlockTime));
      break;
    case CLOSING:
    case OPENING:
    case OPEN:
    default:
      stopGate();
      break;
  }
}

void loop() {
  listenForSignal();
  processPendingMessages();

  if (ledOffTimestamp > 0 && (millis() - ledOffTimestamp) >= (ledTime * 1000)) {
    setLedColor(BLACK);
    ledOffTimestamp = 0;
  }

  // lock gate after unlockTime seconds from being unlocked
  if (currentState == OPENING && !isLocked && ((millis() - unlockTimestamp) >= (unlockTime * 1000))) {
    lock();
  }

  // close gate after it's been opened for stayOpenTime
  if (currentState == OPEN && ((millis() - openTimestamp) >= stayOpenTime * 1000)) {
    closeGate();
  }


  // check for gate limits
  if (currentState == OPENING && digitalRead(GATE_LIMIT_OPEN_PIN) == LOW) {  // pull-up
    stopGate();
    setLedColor(YELLOW);
    Serial.println("Gate open limit reached");
    Serial.printf("Closing in %d seconds\n", stayOpenTime);
    currentState = OPEN;
    openTimestamp = millis();
  }

  if (currentState == CLOSING && digitalRead(GATE_LIMIT_CLOSE_PIN) == LOW) {  // pull-up
    stopGate();
    Serial.println("Gate close limit reached");
    currentState = CLOSED;
  }
}

void listenForSignal() {  // check for remote trigger
  if (Serial1.available()) {
    String incomingData = Serial1.readStringUntil('\n');
    Serial.println("Message received: " + incomingData);
    if (incomingData.startsWith("+OK") || incomingData.startsWith("+ERR") || incomingData.length() == 0) {
      awaitingResponse = false;
      processPendingMessages();
      return;
    }

    // Parse the incoming message to extract the address
    int startIdx = incomingData.indexOf("+RCV=") + 5;
    int endIdx = incomingData.indexOf(',', startIdx);
    String addressStr = incomingData.substring(startIdx, endIdx);
    int address = addressStr.toInt();

    // Check if the address is in the allowed list
    bool addressAllowed = false;
    for (int allowedAddress : allowedAddresses) {
      if (allowedAddress == address) {
        addressAllowed = true;
        break;
      }
    }

    if (addressAllowed) {
      sendMessage(REQUEST_FROM + addressStr);
      handleButtonPress();
    } else {
      timedLed(RED);
      sendMessage(REQUEST_DENIED + addressStr);
    }
  }
}
