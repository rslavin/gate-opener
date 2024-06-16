#include <HardwareSerial.h>
#include <vector>

// LoRa
#define MY_ADDRESS 1301
// #define MY_ADDRESS 1400
#define SERVER_ADDRESS 1300

// pins
#define GATE_FORWARD_CURRENT_PIN 0
#define GATE_FORWARD_PWM_PIN 2
#define GATE_REVERSE_CURRENT_PIN 4
#define GATE_REVERSE_PWM_PIN 7

#define GATE_LIMIT_PIN 9
#define MOTOR_LIMIT_PIN 8
#define LOCK_PIN 10

//#define PWM_RESOLUTION 8

#define GATE_SPEED_PERCENT 100
#define UNLOCK_TIME 5 // time in seconds that the lock stays open
#define STAY_OPEN_TIME 4 // time the gate stays open before closing again
#define LIMIT_SWITCH_DEBOUNCE_TIME 7 // time after opening/closing that the limit switch is ignored

#define GATE_CURRENT_THRESHOLD 819 // 4 amp threshold for overcurrent condition (for 10-bit ADC: 0-1023)

// messages
#define SUCCESS_SETUP "1"
#define SUCCESS_LOCK "2"
#define ERROR_MOTOR_LIMIT "3"
#define ERROR_CURRENT_LIMIT_FORWARD "4"
#define ERROR_CURRENT_LIMIT_REVERSE "5"
#define REQUEST_FROM "99:"
#define REQUEST_DENIED "88:"

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
unsigned long previousMillis = 0;
unsigned long buttonPressedTime = 0;

// List of allowed addresses
std::vector<int> allowedAddresses;

void addAddress(int address) {
    allowedAddresses.push_back(address);
}

void setAddresses(const std::vector<int>& addresses) {
    allowedAddresses = addresses;
}

void listenForSignal();

void sendMessage(String msg, String address = String(SERVER_ADDRESS)) {
    String fullMsg = "AT+SEND=" + address + "," + String(msg.length()) + "," + msg;
    Serial.printf("Sending message \"%s\" to %s\n", fullMsg.c_str(), address.c_str());
    Serial1.println(fullMsg);
}

void setup() {
    // motor driver pin modes
    pinMode(GATE_FORWARD_CURRENT_PIN, INPUT);
    pinMode(GATE_FORWARD_PWM_PIN, OUTPUT);
    pinMode(GATE_REVERSE_CURRENT_PIN, INPUT);
    pinMode(GATE_REVERSE_PWM_PIN, OUTPUT);

    // other pin modes
    pinMode(GATE_LIMIT_PIN, INPUT_PULLUP);
    pinMode(MOTOR_LIMIT_PIN, INPUT_PULLUP);
    pinMode(LOCK_PIN, OUTPUT);

    // initialize pin states
    digitalWrite(GATE_FORWARD_PWM_PIN, LOW);
    digitalWrite(GATE_REVERSE_PWM_PIN, LOW);
    digitalWrite(LOCK_PIN, LOW);

    Serial.begin(115200); // USB
    Serial1.begin(115200, SERIAL_8N1); // UART TX/RX pins

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
    delay(500); // give it time to get to current
    unlockTimestamp = millis();
}

void lock() {
    Serial.println("Locking gate");
    digitalWrite(LOCK_PIN, LOW);
    isLocked = true;
}

void openGate() {
    Serial.println("Opening gate");
    sendMessage("Opening");
    digitalWrite(GATE_REVERSE_PWM_PIN, LOW);
    analogWrite(GATE_FORWARD_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
    currentState = OPENING;
    lastDirection = OPENING;
}

void closeGate() {
    Serial.println("Closing gate");
    sendMessage("Closing");
    digitalWrite(GATE_FORWARD_PWM_PIN, LOW);
    analogWrite(GATE_REVERSE_PWM_PIN, percentageToDutyCycle(gateSpeedPercent));
    currentState = CLOSING;
    lastDirection = CLOSING;
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
  buttonPressedTime = millis();

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
}


void loop() {
    listenForSignal();

    // lock gate after unlockTime seconds from being unlocked
    if (currentState == OPENING && !isLocked && ((millis() - unlockTimestamp) >= (unlockTime * 1000))) {
        lock();
    }

    if (currentState == OPEN && ((millis() - openTimestamp) >= stayOpenTime * 1000)) {
        buttonPressedTime = millis();
        closeGate();
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
    if (buttonPressedTime > 0 && millis() - buttonPressedTime >= LIMIT_SWITCH_DEBOUNCE_TIME * 1000) { 
        if (currentState == OPENING && digitalRead(GATE_LIMIT_PIN) == LOW) { // pull-up
            Serial.println("Gate open limit reached");
            stopGate();
            Serial.printf("Closing in %d seconds\n", stayOpenTime);
            currentState = OPEN;
            openTimestamp = millis();
        }
    
        if (currentState == CLOSING && digitalRead(GATE_LIMIT_PIN) == LOW) { // pull-up
            Serial.println("Gate close limit reached");
            stopGate();
            currentState = CLOSED;
        }
    }
}

void listenForSignal() {// check for remote trigger
    if (Serial1.available()) {
        String incomingData = Serial1.readStringUntil('\n');
        Serial.println("Message received: " + incomingData);
        if (incomingData.startsWith("+OK") || incomingData.startsWith("+ERR") || incomingData.length() == 0) {
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
