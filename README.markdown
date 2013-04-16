# DavisSi1000 - Firmware for SiLabs Si1000 ISM radios for Davis ISS weather protocol

This firmware is based upon the SiK telemetry radio firmware from
http://github.com/tridge/SiK

It implements the receiving side of the Davis ISS 900MHz wireless
weather protocol. This will allow a 3DR telemetry radio to be
used as a USB enabled Davis weather console.

For more explanation see this discussion:

  http://www.wxforum.net/index.php?topic=18718.0


How To use
----------

First you need a 3DR 900MHz radio, either a TTL serial or USB version:

  http://store.diydrones.com/3DR_Radio_USB_915_Mhz_Ground_module_p/br-3drusb915.htm
  http://store.diydrones.com/3DR_Radio_915_Mhz_Air_module_p/br-3dr915.htm

plus an antenna:
  http://store.diydrones.com/ProductDetails.asp?ProductCode=WI-W1063-900mhz-2dbi

For really long range reception, get a RFD900 instead:

  http://rfdesign.com.au/index.php/rfd900

Then you need to load the DavisSi1000 firmware. You can build the
firmware yourself on Linux using sdcc and make, or you can download a
prebuilt firmware here:

  http://uav.tridgell.net/DavisSi1000/firmware

For the 3DR radio you will need the radio~hm_trp.ihx file. For the
RFD900 you will need radio~rfd900a.ihx.

To upload your firmware use the Firmware/tools/uploader.py python
script.

Connecting and Protocol
-----------------------

After the firmware is installed you can connect to the radio at a
baudrate of 57600. It will initially display a series of messages like
this:

Searching 3 at 907868377 Hz
Searching 4 at 922418792 Hz
Searching 5 at 905359466 Hz

that shows the time since reset in seconds, and the frequency it is
looking on.

Once it finds your ISS it will display messages like this once every
2.7 seconds:

{ "transmitter_id": 2, "RSSI": 128, "recv_packets": 22, "wind_speed_mph": 0, "wind_direction_degrees": 215, "temperature_F": 67.95, "humidity_pct": 63.6, "light": 1536, "rain_spoons": 12, "raw": "82 00 98 2A 29 00 E1 45 FF FF ", "version": "1.0" }

These are in JSON format. 
