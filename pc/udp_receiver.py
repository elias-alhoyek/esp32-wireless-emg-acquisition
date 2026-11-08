"""Receive the ESP32 UDP stream and save it to an Excel (.xls) file.

Each packet holds 240 uint16 ADC samples (480 bytes). The time axis is rebuilt
from the 8 kHz sampling rate, so it does not depend on packet arrival times.

Windows only (uses msvcrt to wait for the SPACE key).
"""
import socket
import struct
import time
import msvcrt

import xlwt

# ----- Configuration -----
udp_ip = "0.0.0.0"            # listen on all interfaces
udp_port = 4210               # must match remotePort in the firmware
excel_filename = "comparison_acquisition.xls"

# ----- Packet format: raw uint16_t buffer[240] sent by the ESP32 -----
samples_per_packet = 240
packet_size = samples_per_packet * 2

# ----- Sampling parameters -----
sampling_rate = 8000                                 # Hz
time_increment_us = int(1e6 / sampling_rate)         # 125 us
acquisition_duration = 1.0                           # seconds

# ----- Wait for the user -----
print("Press SPACE to start acquisition...")
while True:
    if msvcrt.kbhit():
        if msvcrt.getch() == b' ':
            print("Waiting for ESP32 data...")
            break

# ----- UDP socket -----
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((udp_ip, udp_port))
print(f"UDP socket bound to port {udp_port}...")

esp32_data = []          # (time_us, adc_value)
time_us = 0
start_time = time.time()

try:
    while True:
        raw, addr = sock.recvfrom(packet_size + 1024)
        if len(raw) < packet_size:
            continue
        raw = raw[:packet_size]

        samples = struct.unpack(f"{samples_per_packet}H", raw)   # H = uint16

        for sample in samples:
            esp32_data.append((time_us, sample))
            time_us += time_increment_us

        if time.time() - start_time >= acquisition_duration:
            print("Acquisition completed")
            break

except Exception as e:
    print("Error:", e)

sock.close()

# ----- Excel export -----
book = xlwt.Workbook()
sheet = book.add_sheet("Comparison")

sheet.write(0, 0, "Time (µs)")
sheet.write(0, 1, "ESP32 ADC value")
sheet.write(0, 2, "ESP32 Voltage (V)")

ADC_MAX = 4095    # 12-bit ADC
VREF = 3.3        # V

row = 1
for t, adc in esp32_data:
    voltage = (adc / ADC_MAX) * VREF
    sheet.write(row, 0, t)
    sheet.write(row, 1, adc)
    sheet.write(row, 2, voltage)
    row += 1

book.save(excel_filename)
print(f"Data saved to {excel_filename}")
