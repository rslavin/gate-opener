#include <HardwareSerial.h>
#include <vector>
#include <queue>

// LoRa
#define MY_ADDRESS 1301
#define SERVER_ADDRESS 1300

// pins
#define GATE_FORWARD_CURRENT_PIN 0
#define GATE_FORWARD_PWM_PIN 2
#define GATE_REVERSE_CURRENT_PIN 4
#define GATE_REVERSE_PWM_PIN 7

#define GATE_LIMIT_CLOSE_PIN 8
#define GATE_LIMIT_OPEN_PIN 9
#define LOCK_PIN 10
// #define GATE_LIMIT_PIN 9
// #define MOTOR_LIMIT_PIN 8


#define GATE_SPEED_PERCENT 100
#define UNLOCK_TIME 5                 // time in seconds that the lock stays open
#define STAY_OPEN_TIME 4              // time the gate stays open before closing again
#define LIMIT_SWITCH_DEBOUNCE_TIME 1  // time after opening/closing that the limit switch is ignored

#define GATE_CURRENT_THRESHOLD 819  // 4 amp threshold for overcurrent condition (for 10-bit ADC: 0-1023)

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
unsigned long unlockTimestamp = 0;
unsigned long openTimestamp = 0;  // time at which the gate reaches open position
unsigned short gateSpeedPercent = GATE_SPEED_PERCENT;
unsigned short unlockTime = UNLOCK_TIME;
unsigned short stayOpenTime = STAY_OPEN_TIME;
unsigned short currentThreshold = GATE_CURRENT_THRESHOLD;

// List of allowed addresses
std::vector<int> allowedAddresses;
std::queue<String> messageQueue;
std::queue<String> pendingMessages;
bool awaitingResponse = false;

void addAddress(int address) { allowedAddresses.push_back(address); }

void setAddresses(const std::vector<int>& addresses) { allowedAddresses = addresses; }

void listenForSignal();

void sendMessage(String msg, String address = String(SERVER_ADDRESS)) {
  String fullMsg = "AT+SEND=" + address + "," + String(msg.length()) + "," + msg;
  Serial.printf("Queing message \"%s\" to %s\n", fullMsg.c_str(), address.c_str());
  pendingMessages.push(fullMsg);
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
  while (!Serial || !Serial1) {
    delay(10);
  }

  Serial.println("Setting local address to " + String(MY_ADDRESS));
  Serial1.println("AT+ADDRESS=" + String(MY_ADDRESS));
  delay(1000);
  Serial1.println("AT+ADDRESS?");

  while (true) {
    msg_received = Serial1.readStringUntil('\n');
    if (msg_received && msg_received.indexOf("+OK") >= 0) {
      Serial.print("Success: ");
      Serial.println(msg_received);
      break;
    } else {
      Serial.println("[WARNING] Failed to set transceiver address. Trying again.");
      ESP.restart();
    }
  }
  addAddress(SERVER_ADDRESS);
  addAddress(1311);
  addAddress(1312);
  addAddress(1313);
  addAddress(1314);
  sendMessage(SUCCESS_SETUP);
}

int percentageToDutyCycle(int percentage) {
  if (percentage < 0) {
    percentage = 0;  // Ensure the percentage is not negative
  } else if (percentage > 100) {
    percentage = 100;  // Cap the percentage at 100%
  }

  return map(percentage, 0, 100, 0, 255);
}

void unlock() {
  Serial.println("Unlocking gate");
  sendMessage("Unlocking");
  digitalWrite(LOCK_PIN, HIGH);
  isLocked = false;
  delay(500);  // give it time to get to current
  unlockTimestamp = millis();
}

void lock() {
  Serial.println("Locking gate");
  digitalWrite(LOCK_PIN, LOW);
  isLocked = true;
}

void openGate() {
  if (digitalRead(GATE_LIMIT_OPEN_PIN) != LOW) {
    Serial.println("Opening gate");
    sendMessage("Opening");
    digitalWrite(GATE_REVERSE_PWM_PIN, LOW);
    analogWrite(GATE_FORWARD_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
    currentState = OPENING;
    lastDirection = OPENING;
  } else {
    Serial.println("Attempted to open gate when it is already open");
    sendMessage("Attempted to open gate when it is already open");
  }
}

void closeGate() {
  if (digitalRead(GATE_LIMIT_CLOSE_PIN) != LOW) {
    Serial.println("Closing gate");
    sendMessage("Closing");
    digitalWrite(GATE_FORWARD_PWM_PIN, LOW);
    analogWrite(GATE_REVERSE_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
    currentState = CLOSING;
    lastDirection = CLOSING;
  } else {
    Serial.println("Attempted to close gate when it is already closed");
    sendMessage("Attempted to close gate when it is already closed");
  }
}

void stopGate() {
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
      sendMessage(REQUEST_DENIED + addressStr);
    }
  }
}
