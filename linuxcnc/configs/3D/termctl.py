#!/usr/bin/python

import serial
import sys
import time
import struct

HOTEND_EXTRUDER = 0
HOTEND_BED = 1

CMD_GET_DATA = 0x80
CMD_SET_TUNE_PARAMS = 0x81
CMD_SET_TEMPERATURE = 0x82
CMD_START_TUNING = 0x83
CMD_STOP_TUNING = 0x84
CMD_GET_TEMP = 0x85
CMD_SET_FAN = 0x86
CMD_SET_BETA25 = 0x87


class TemperatureControl:

    def __init__(self, port):
        self.port = port
        self.ser = serial.Serial(port, 9600, timeout=0)

    def close(self):
        self.ser.close()
        self.ser = None

    def _send_packet(self, hotend, cmd, data):
        self.ser.write(bytearray([0x06, 0x85, len(data) + 2, cmd, hotend]))
        cs = len(data) + 2
        cs ^= cmd
        cs ^= hotend
        for b in data:
            cs ^= b
        self.ser.write(data)
        self.ser.write(bytearray([cs]))

    def _read_byte(self):
        b = bytearray(self.ser.read(1))
        return b[0]

    def _receive_packet(self):
        start = time.time()
        rx_len = 0
        rx_buffer = bytearray([])
        while time.time() - start < 3:
            if rx_len == 0:
                if self.ser.in_waiting >= 3:
                    while self._read_byte() != 0x06:
                        if self.ser.in_waiting < 3:
                            return None
                    if self._read_byte() == 0x85:
                        rx_len = self._read_byte()
            if rx_len != 0:
                while self.ser.in_waiting > 0 and len(rx_buffer) <= rx_len:
                    rx_buffer.append(self._read_byte())
                if len(rx_buffer) - 1 == rx_len:
                    cs = rx_len
                    cs_get = rx_buffer[len(rx_buffer) - 1]
                    rx_buffer = rx_buffer[:-1]
                    for b in rx_buffer:
                        cs ^= b
                    if cs != cs_get:
                        print("Incorrect CRC")
                        return None
                    return rx_buffer
            time.sleep(.1)
        print("Serial Timeout. rxlen: {0}".format(rx_len))
        return None

    def get_data(self, hotend):
        data = bytearray([])
        self._send_packet(hotend, CMD_GET_DATA, data)
        time.sleep(0.1)
        data = self._receive_packet()
        if data is None:
            return None
        data = struct.unpack("IIfffB", bytes(data))
        return {
            "cur_temp": data[0],
            "temp": data[1],
            "Kp": data[2],
            "Ki": data[3],
            "Kd": data[4],
            "in_tune": data[5],
        }

    def get_temp(self, hotend):
        data = bytearray([])
        self._send_packet(hotend, CMD_GET_TEMP, data)
        data = self._receive_packet()
        if data is None:
            return None
        data = struct.unpack("II", bytes(data))
        return {
            "extruder": data[0],
            "hotbed": data[1],
        }

    def set_temperature(self, hotend, temp):
        data = bytearray(struct.pack("H", temp))
        self._send_packet(hotend, CMD_SET_TEMPERATURE, data)

    def set_tuning(self, hotend, Kp, Ki, Kd):
        data = bytearray(struct.pack("fff", float(Kp), float(Ki), float(Kd)))
        self._send_packet(hotend, CMD_SET_TUNE_PARAMS, data)

    def start_tuning(self, hotend, temp):
        data = bytearray(struct.pack("f", float(temp)))
        self._send_packet(hotend, CMD_START_TUNING, data)

    def stop_tuning(self, hotend):
        data = bytearray([])
        self._send_packet(hotend, CMD_STOP_TUNING, data)

    def set_fan(self, fanid, speed):
        data = bytearray(struct.pack("I", int(speed)))
        self._send_packet(fanid, CMD_SET_FAN, data)

    def set_beta25(self, hotend, val):
        data = bytearray(struct.pack("I", int(val)))
        self._send_packet(hotend, CMD_SET_BETA25, data)

