#include <HardwareSerial.h>
#include <Adafruit_TinyUSB.h>

#define BUTTON_PIN 9
#define LED_PIN 8
#define WAKE_DURATION 4000 // 40 seconds in milliseconds
#define MY_ADDRESS 1311
#define DESTINATION_ADDRESS 1400
#define BROADCAST_ADDRESS 0

/********** AT COMMAND SET ************
AT+SEND=<destination_address>,<length_of_data>,<data>

AT: This prefix is used for "Attention" and is common to all AT commands, indicating the start of a command line.
+SEND: This command is used to send data through the LoRa module.
0: This is the first parameter of the +SEND command, which typically specifies the destination address where the data should be sent. In the case of 0, it often denotes a broadcast to all devices on the network or a default address if addressing has not been specifically set up.
1: The second parameter represents the number of bytes of data that will be sent in the message. In this case, 1 indicates that a single byte of data will be transmitted.
1: The third parameter is the actual data to be sent. Here, 1 is the data byte being transmitted. This could represent a command or a value, such as turning a device on or off, depending on the context of your application.
*/

String msg_sent;
String msg_received;

unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers
unsigned long wakeUpTime = 0;
SemaphoreHandle_t xSemaphore;
int buttonState;

void setup() {
  // put your setup code here, to run once:
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP_SENSE);
  // nrf_gpio_cfg_sense_input(BUTTON_PIN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), wakeUp, FALLING);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1);

  while (!Serial1){ 
    delay(10);
  }

  Serial.println("Setting local address to " + String(MY_ADDRESS));
  Serial1.println("AT+ADDRESS=" + String(MY_ADDRESS));
  delay(500);
  Serial1.println("AT+ADDRESS?"); 

  while(1){
    unsigned long startTime = millis();
    while (Serial1.available() == 0) {
      if (millis() - startTime > 500) { // Timeout after 500ms
        Serial.println("Timeout waiting for response");
        break;
      }
    }
    msg_received = Serial1.readStringUntil('\n');
  
    if (msg_received.startsWith("+OK")) {
      Serial.print("Success:");
      Serial.println(msg_received);
      break;
    } 
    Serial.println("No response, trying again");
  }

  // Create the semaphore
  xSemaphore = xSemaphoreCreateBinary();
  if (xSemaphore == NULL) {
    Serial.println("Failed to create semaphore");
    while (1);
  }

  Serial.println("Setup complete");
  enterDeepSleep();
}

void loop() {
  // block until the wakeUp() is called as an interrupt and gives the semaphore 
  // this chip also goes into a low-power state when blocking
  xSemaphoreTake(xSemaphore, portMAX_DELAY);

  // Set the wake up time when we wake up from deep sleep
  if (wakeUpTime == 0) {
    wakeUpTime = millis();
    Serial.printf("Going back to sleep in %d ms\n", WAKE_DURATION);
  }

  buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == LOW) {
    digitalWrite(LED_PIN, HIGH);

    // check if the button is pressed (button pin goes LOW) and the last debounce time has elapsed
    if ((millis() - lastDebounceTime) > debounceDelay) {
      lastDebounceTime = millis();  // reset the debouncing timer

      // print and toggle LED
      Serial.println("Button pressed");

      msg_sent = "AT+SEND=" + String(DESTINATION_ADDRESS) + ",1,1";

      // send message
      Serial1.println(msg_sent); 
      Serial.println("Message transmitted: " + msg_sent);

      // wait for button release to reset the debounce timer
      while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);  // small delay to help with debouncing
      }
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  if (millis() - wakeUpTime >= WAKE_DURATION) {
    wakeUpTime = 0;
    enterDeepSleep();
  } else { // next loop
    xSemaphoreGive(xSemaphore); // increment the semaphore once so the next loop can execute
  }
}

void enterDeepSleep() {
  Serial.println("Entering deep sleep");
  // NRF_POWER->SYSTEMOFF = 1;
  NRF_POWER->TASKS_LOWPWR = 1;
  __WFI();
}

void wakeUp() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(xSemaphore, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  Serial.println("Woke from deep sleep");
}

// void QSPIF_sleep(){
//   flashTransport.begin();
//   flashTransport.runCommand(0xB9);
//   flashTransport.end();  
// }

