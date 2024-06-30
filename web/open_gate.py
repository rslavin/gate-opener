import serial
import time

from models import LogEntry
from extensions import db

def open_gate(user_note=''):
    access_code = LogEntry(user=user_note)
    db.session.add(access_code)
    db.session.commit()

    address_response = send_uart_message("AT+ADDRESS=1313")
    send_response = send_uart_message("AT+SEND=1301,1,1")
    return send_response

def send_uart_message(message):
    try:
        ser = serial.Serial('/dev/ttyAMA2', 115200, timeout=1)

        if not ser.is_open:
            print("Error: Serial port did not open.")
            return "ERROR"

        # flush the input and output buffers
        ser.flush()

        ser.write(f"{message}\r\n".encode('utf-8'))

        time.sleep(1)  # Wait for the device to respond

        response = ser.readline().decode('utf-8')

        ser.close()
        return response
    except Exception as e:
        print(f"Error: {e}")
        return "ERROR"

