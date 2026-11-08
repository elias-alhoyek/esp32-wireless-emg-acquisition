# ESP32 wireless acquisition: 8 kHz ADC streaming over Wi-Fi (UDP)

Real-time acquisition chain that samples an analog signal on an **ESP32-C3** at a fixed **8 kHz** and streams it wirelessly to a PC, where it is time-stamped and saved for analysis. I built it to acquire physiological (EMG) signals as part of my work on low-power EMG sensing, during my Master's in mechatronics at Polytech Annecy.

## The problem

Sampling a signal at a steady rate while sending it over a radio link on the same small microcontroller is a timing problem: network operations take an unpredictable amount of time, and if they block the sampling loop, samples arrive late or are lost. The goal was to keep sampling deterministic no matter what the wireless link is doing.

## Architecture

```
 hardware timer (8 kHz)
        │  ISR: notify only (no ADC read in the interrupt)
        ▼
 TaskSampling  (priority 2) ── analogRead() ──► FreeRTOS queue (1024 samples)
                                                     │
                                                     ▼
                                 TaskWiFiSend (priority 1)
                                 fills a 240-sample buffer
                                                     │  1 UDP packet = 240 × uint16 = 480 bytes
                                                     ▼
                                        PC: pc/udp_receiver.py
                                rebuilds the time axis (125 µs/sample)
                                          and saves to .xls
```

- **Timer ISR kept minimal:** it only wakes the sampling task (`vTaskNotifyGiveFromISR`), which keeps jitter low.
- **Sampling and transmission decoupled:** a high-priority task reads the ADC, a lower-priority task handles the network, and a 1024-sample queue absorbs short network delays.
- **Batching:** 240 samples per UDP packet (about 33 packets per second) instead of one packet per sample, to cut per-packet overhead.
- **UDP rather than TCP:** no connection handling or retransmissions that could block the sender.

## Why Wi-Fi and not Bluetooth Low Energy

The link was first prototyped in **Bluetooth Low Energy (BLE)**, for its low power consumption. In our setup BLE could not sustain a reliable 8 kHz stream (about 128 kbit/s of continuous data): the link was unstable and data was lost. I switched to **Wi-Fi with UDP**, which gave the margin needed for continuous streaming.

## Repository contents

| Path | What it is |
|---|---|
| `firmware/esp32_adc_udp_streamer/` | Arduino sketch for the ESP32-C3: timer ISR, two FreeRTOS tasks, queue, UDP sender |
| `pc/udp_receiver.py` | Python receiver: waits for SPACE, records for 1 s, rebuilds the 8 kHz time axis, exports time / raw ADC / voltage to an `.xls` file |

## How to run

**ESP32 (Arduino IDE, Arduino-ESP32 core 3.x)**
1. Open `firmware/esp32_adc_udp_streamer/esp32_adc_udp_streamer.ino`.
2. Set `ssid`, `password` and `remoteIP` (the IP address of your PC on the same network). Connect the signal to GPIO 4 (0–3.3 V).
3. Flash the board. The serial monitor (115200 baud) prints the ESP32's IP and "Sent" for each packet.

**PC (Windows, Python 3)**
1. `pip install -r pc/requirements.txt`
2. Allow UDP port 4210 through the firewall.
3. `python pc/udp_receiver.py`, then press SPACE to start a 1-second recording. The output is `comparison_acquisition.xls`, with columns Time (µs), ESP32 ADC value (0–4095) and ESP32 Voltage (V).

## Limitations and next steps

- **No packet numbering:** the PC rebuilds time by counting samples, so a lost UDP packet would shift the time axis without being detected. Next step: add a sequence number to each packet.
- **Windows-only receiver** (`msvcrt`) and **`.xls` export**, which is limited to 65,536 rows, so about 8 s at 8 kHz. Next step: CSV or binary logging with no length limit.
- **ESP32 ADC:** `analogRead()` is simple but not the most linear option; calibrated reads or an external ADC would improve accuracy.
- The queue is written without a timeout, so if the network stalls for longer than the queue can absorb, new samples are dropped.

## Author

Elias Al Hoyek, Master's student in mechatronics, Polytech Annecy (Université Savoie Mont Blanc).
Portfolio: https://elias-alhoyek.github.io/
