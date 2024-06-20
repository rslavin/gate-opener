#include <HardwareSerial.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_FlashTransport.h>

#define MY_ADDRESS 1311 // mom
// #define MY_ADDRESS 1319
#define DESTINATION_ADDRESS 1301
// #define DESTINATION_ADDRESS 1400

#define BUTTON1_PIN 5
#define BUTTON2_PIN 8
#define LED_PIN 9
#define WAKE_DURATION 4000


String msgSent;
String msgReceived;
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers
unsigned long wakeUpTime = 0;
int buttonState;
Adafruit_FlashTransport_QSPI flashTransport;


void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1);

  // wait for LoRa module serial
  while (!Serial1){ 
    delay(10);
  }
  delay(50);

  Serial.println("Setting local address to " + String(MY_ADDRESS));
  Serial1.println("AT+ADDRESS=" + String(MY_ADDRESS));
  Serial1.flush();
  Serial1.println("AT+ADDRESS?"); 

  while(1){
    unsigned long startTime = millis();
    while (Serial1.available() == 0) {
      if (millis() - startTime > 500) { // Timeout after 500ms
        Serial.println("Timeout waiting for `AT+ADDRESS?` response. Sending command again.");
        Serial1.println("AT+ADDRESS?"); 
        break;
      }
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

  // send message
  msgSent = "AT+SEND=" + String(DESTINATION_ADDRESS) + ",1,1";
  Serial1.println(msgSent); 
  Serial.println("Message transmitted: " + msgSent);
  delay(200);

  // put LoRa into sleep mode 
  Serial1.println("AT+MODE=1");
  delay(200);

  // configure interrupts for waking back up on button press
  pinMode(BUTTON1_PIN, INPUT_PULLUP_SENSE);
  nrf_gpio_cfg_sense_input(BUTTON1_PIN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  pinMode(BUTTON2_PIN, INPUT_PULLUP_SENSE);
  nrf_gpio_cfg_sense_input(BUTTON2_PIN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

  // shut it down
  digitalWrite(LED_PIN, LOW);
  flashTransport.begin();
  flashTransport.runCommand(0xB9);
  flashTransport.end();
  Serial1.end();
  Serial.end();
  // the following sequence optimizes power
  __WFI();
  __SEV();
  __WFI();
  NRF_POWER->SYSTEMOFF = 1;  
}

void loop() {
  
}

