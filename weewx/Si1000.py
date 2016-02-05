#
#    Copyright (c) 2013 Andrew Tridgell
#
#    See the file LICENSE.txt for your full rights.
#
#
"""Driver for Si1000 radio receiver for a Davis weather station

See https://github.com/tridge/DavisSi1000 for details of the radio
receiver used

Driver based on simulator.py written by Tom Keffer

To use this driver, add the following to weewx.conf:

[Station]
    station_type = Si1000

[Si1000]
    driver = weewx.drivers.Si1000
    port = /dev/ttyAMA0
    baudrate = 57600

    # adjust wind direction based on install orientation
    wind_dir_adjust = 180

"""

from __future__ import with_statement
import math
import time
import json

import weedb
import weeutil.weeutil
import weewx.abstractstation
import weewx.wxformulas

def loader(config_dict, engine):
    station = Si1000(**config_dict['Si1000'])
    return station
        
class Si1000(weewx.abstractstation.AbstractStation):
    """Si1000 driver"""
    
    def __init__(self, **stn_dict):
        """Initialize the driver
        """
        import serial
        
        self.port = stn_dict.get('port', '/dev/ttyAMA0')
        self.baudrate = int(stn_dict.get('baudrate', 57600))
        self.wind_dir_adjust = int(stn_dict.get('wind_dir_adjust', 180))
        
        self.fd = serial.Serial(self.port, self.baudrate, timeout=3)

        self.last_rain = None

        '''
        a mapping between field names on the Si1000 and the weewx field names. The extra
        parameter is an optional conversion function
        '''
        self.fieldmap = {
            'wind_direction_degrees' : ('windDir',   self.adjust_wind_direction),
            'wind_speed_mph'         : ('windSpeed', None),
            'temperature_F'          : ('outTemp',   None),
            'humidity_pct'           : ('humidity',  None),
            'rain_spoons'            : ('rain',      self.convert_rain)
            }

    def convert_rain(self, rain_spoons):
        '''convert rain_spoons to rain in inches'''
        if self.last_rain is None:
            self.last_rain = rain_spoons
            return 0
        ret = rain_spoons - self.last_rain
        if ret < 0:
            # rain_spoons is 7 bit
            ret = 128 + ret
        self.last_rain = rain_spoons
        # each spoon is 0.01"
        return ret * 0.01

    def adjust_wind_direction(self, direction):
        '''adjust wind direction for installation'''
        direction += self.wind_dir_adjust
        if direction > 360:
            direction -= 360
        return direction

    def genLoopPackets(self):
        
        while True:
            line = self.fd.readline()
            line = line.strip()
            if not line:
                time.sleep(0.1)
                continue
            if line[0] != '{' or line[-1] != '}':
                time.sleep(0.1)
                continue
            values = json.loads(line)

            _packet = {'dateTime': int(time.time()+0.5),
                       'usUnits' : weewx.US }

            for k in values.keys():
                if k in self.fieldmap:
                    (name, conversion) = self.fieldmap[k]
                    value = values[k]
                    if conversion is not None:
                        value = conversion(value)
                    _packet[name] = value
            
            yield _packet

    @property
    def hardware_name(self):
        return "DavisSi1000"
        
if __name__ == "__main__":

    station = Si1000()
    for packet in station.genLoopPackets():
        print weeutil.weeutil.timestamp_to_string(packet['dateTime']), packet
    
