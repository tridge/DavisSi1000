#!/usr/bin/env python

import serial, sys, optparse, time

parser = optparse.OptionParser("davis_show")
parser.add_option("--type", type='int', default=None, help='msg type to show')
parser.add_option("--speed", action='store_true', default=False, help='show wind speed')

opts, args = parser.parse_args()

logfile = args[0]

def swap_bit_order(b):
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4)
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2)
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1)
    return b

def crc16_ccitt(buf):
    crc = 0
    for b in buf:
        crc ^= b << 8
        for i in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
        crc &= 0xFFFF
    return crc

def wind_speed(b):
    '''return wind speed in m/s'''
    return b / 2.6

def wind_direction(b):
    '''return wind direction in degrees. Zero degrees is the arrow poiting at the front of the unit, away
    from the solar panel'''
    return b * 360 / 255

def decode_12(b1, b2):
    '''decode the 4th and 5th bytes of the packet, using 12 bits to give a value for some field'''
    return (b1<<4) | (b2>>4)

def humidity(b1, b2):
    '''see https://github.com/dekay/im-me/blob/master/pocketwx/src/protocol.txt'''
    return b1 | ((b2>>4)<<8)

def rain(b1, b2):
    '''see https://github.com/dekay/im-me/blob/master/pocketwx/src/protocol.txt'''
    return b1 & 0x7F

def temperature(b1, b2):
    '''return temperature in celsius'''
    v = (b1 << 8) | b2;
    farenheit = v / 160.0
    celcius = (farenheit - 32) * 5 / 9
    return celcius

def process_line(line):
    a=line.split()
    bytes=[]
    for i in range(10):
        try:
            bytes.append(int(a[i],16))
        except Exception:
            print "INVALID"
            bytes.append(0xFF)
    t = float(a[10])
    if opts.speed:
        print("%u %.2f" % (t, wind_speed(bytes[1])))
        return
    v = decode_12(bytes[3], bytes[4])
    crc = crc16_ccitt(bytes[0:8])
    if crc != 0:
        print "BADCRC"
        return
    if opts.type is None:
        print "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %04x %u %.2f speed=%.1f wdirection=%u" % (
            bytes[0], bytes[1],
            bytes[2], bytes[3],
            bytes[4], bytes[5],
            bytes[6], bytes[7],
            bytes[8], bytes[9],
            crc16_ccitt(bytes[0:8]),
            v,
            t,
            wind_speed(bytes[1]),
            wind_direction(bytes[2]))
        return
    type = bytes[0] >> 4
    if type == opts.type or opts.type == -1:
        if type == 8:
            v = temperature(bytes[3], bytes[4])
        if type == 0xA:
            v = humidity(bytes[3], bytes[4])
        if type == 0xE:
            v = rain(bytes[3], bytes[4])
        print("%02X %X %X %X %X %X %X %04x %.2f %s" % (
            bytes[0],
            bytes[3]>>4,
            bytes[3]&0xF,
            bytes[4]>>4,
            bytes[4]&0xF,
            bytes[5]>>4,
            bytes[5]&0xF,
            crc16_ccitt(bytes[0:8]),
            v,
            time.ctime(t)))

log = open(logfile, mode="r")
for line in log:
    line = line.lstrip('.')
    line = line.rstrip()
    if line.startswith('Search'):
        continue
    try:
        process_line(line)
    except Exception:
        pass
    
