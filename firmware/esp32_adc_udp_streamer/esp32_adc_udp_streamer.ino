// ESP32-C3 real-time ADC acquisition streamed to a PC over Wi-Fi (UDP).
//
// A hardware timer ticks at SAMPLE_RATE. Its ISR only notifies the sampling
// task, which reads one ADC sample and pushes it into a FreeRTOS queue.
// A lower-priority task drains the queue and sends blocks of BUFFER_SIZE
// samples as one UDP packet.
//
// Requires the Arduino-ESP32 core 3.x (timerBegin(freq) / timerAlarm API).

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ---------------- Configuration ----------------
#define SAMPLE_RATE   8000   // Hz
#define BUFFER_SIZE   240    // samples per UDP packet (480 bytes)
#define ADC_PIN       4      // analog input

// Wi-Fi network and receiving PC: replace with your own values
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

IPAddress remoteIP(192, 168, 1, 100);   // IP address of the PC running pc/udp_receiver.py
const int remotePort = 4210;

// ---------------- Globals ----------------
WiFiUDP udp;
hw_timer_t* timer = NULL;
TaskHandle_t samplingTaskHandle;
QueueHandle_t sampleQueue;              // sampler -> Wi-Fi sender

// ---------------- Timer ISR ----------------
// Kept minimal: wake the sampling task, no ADC read inside the ISR.
void IRAM_ATTR onTimer() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(samplingTaskHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

// ---------------- Task 1: sampling (high priority) ----------------
void TaskSampling(void* pvParameters) {
  uint16_t sample;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // one wake-up per timer tick
    sample = analogRead(ADC_PIN);
    xQueueSend(sampleQueue, &sample, 0);       // non-blocking
  }
}

// ---------------- Task 2: Wi-Fi transmission (low priority) ----------------
void TaskWiFiSend(void* pvParameters) {
  uint16_t buffer[BUFFER_SIZE];
  int index = 0;

  while (true) {
    if (xQueueReceive(sampleQueue, &buffer[index], portMAX_DELAY)) {
      index++;

      if (index >= BUFFER_SIZE) {
        udp.beginPacket(remoteIP, remotePort);
        udp.write((uint8_t*)buffer, sizeof(buffer));
        udp.endPacket();

        Serial.println("Sent");
        index = 0;
      }
    }
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  analogSetAttenuation(ADC_11db);   // input range ~0-3.3 V

  // Wi-Fi connection
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(remotePort);

  // Queue sized to absorb short network delays
  sampleQueue = xQueueCreate(1024, sizeof(uint16_t));

  // Sampling first (priority 2), networking when the CPU is free (priority 1)
  xTaskCreate(TaskSampling, "SamplingTask", 2048, NULL, 2, &samplingTaskHandle);
  xTaskCreate(TaskWiFiSend, "WiFiTask", 4096, NULL, 1, NULL);

  // Hardware timer: counts at SAMPLE_RATE, alarm on every tick
  timer = timerBegin(SAMPLE_RATE);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 1, true, 0);

  Serial.println("ESP32-C3: 2 RTOS tasks + hardware timer started");
}

// ---------------- Loop ----------------
// Nothing to do here: all the work runs in the tasks and the ISR.
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
