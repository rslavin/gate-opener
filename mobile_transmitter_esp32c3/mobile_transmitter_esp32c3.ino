#include <HardwareSerial.h>

#define DEBUG 0

#if DEBUG == 1
#define DESTINATION_ADDRESS 1300
#else
#define DESTINATION_ADDRESS 1301
#endif

#define MY_ADDRESS 1313

// XIAO ESP32C3  only supports GPIO wakeup on pins 2-5
#define BUTTON1_PIN 4
#define BUTTON2_PIN 5
#define LED_PIN 9


String msgSent;
String msgReceived;
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers
unsigned long wakeUpTime = 0;
int buttonState;


void setup() {
  gpio_hold_dis(static_cast<gpio_num_t>(LED_PIN));
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  gpio_deep_sleep_hold_dis();

  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 20, 21);


  // wait for LoRa module serial
  while (!Serial1 || ( DEBUG && !Serial)){ 
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
  esp_deep_sleep_enable_gpio_wakeup((1ULL << BUTTON1_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_enable_gpio_wakeup((1ULL << BUTTON2_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);

  // shut it down
  digitalWrite(LED_PIN, LOW);
  gpio_deep_sleep_hold_en();
  gpio_hold_en(static_cast<gpio_num_t>(LED_PIN));
  Serial1.end();
  Serial.end();
  esp_deep_sleep_start();
}

void loop() {
  
}

