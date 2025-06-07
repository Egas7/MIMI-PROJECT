import serial
import csv
from datetime import datetime

porta_serial = 'COM3'
baud_rate = 9600

arduino = serial.Serial(porta_serial, baud_rate, timeout=1)
print(f"Conectado à {porta_serial}")

with open("log_acesso.csv", mode="a", newline="") as file:
    writer = csv.writer(file)
    writer.writerow(["Data", "Hora", "Status"])

    while True:
        linha = arduino.readline().decode("utf-8").strip()
        if linha:
            agora = datetime.now()
            data = agora.strftime("%Y-%m-%d")
            hora = agora.strftime("%H:%M:%S")
            writer.writerow([data, hora, linha])
            print(f"[{data} {hora}] {linha}")
