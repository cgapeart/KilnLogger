# KilnLogger
InternetOfKilns: A simple ESP8266duino based kiln or bbq temperature logger
This is just a toy for creating a live graph of the temperature of a backyard raku kiln or other hot object that you can access from your phone with a web browser.

![A sample of the display](media/sample.png)

## Pre-Requisites
* An ESP8266 based board
* An Adafruit MAX31856 Breakout
* Kiln or other thermocouple.  The MAX31846 supports a wide range
* The Arduino IDE
* The ESP8266 boards package plugin for the Arduino IDE
* Adafruit MAX31856 libraries installed from the library manager and all pre-requisites
* The ESP8266 sketch data upload plugin for the arduino IDE
* the ESPPerfectTime library (libraries manager)

## External Requirements/Licenses included with the package
* Includes chartist-js: https://gionkunz.github.io/chartist-js/  (MIT License)
* Includes momentjs: https://momentjs.com/ (MIT License)
* Includes chartist-plugin-axistitle https://github.com/alexstanbury/chartist-plugin-axistitle (MIT License)
* Includes chartist-plugin-legend https://github.com/CodeYellowBV/chartist-plugin-legend (Internet Systems Consortion license)

## TODO's if you want to use this
* Set your own WIFI SID and passwords STASSID, STAPSK, and STANAME
* Make sure the thermocouple type set in setup matches your thermocouple.  Typical kiln thermocouples are type K.
* Pin mappings:  This sketch uses bitbanged SPI because I couldn't figure out the hardware yet.  See xCS, xSDI, xSDO, xSCK, xFLT, xDRD.
* Solder the connections and keep them short.  Attach the thermocouple.
* Make sure to use the ESP8266 sketch data upload plugin to copy the files from data folder into the SPIFFS filesystem on the ESP8266
* The ESP has limited ram:  I have set it up to sample 5 hours worth of data, every 10 seconds (though the webpage will only refresh every 30 seconds)
* BUF_LEN is the number of samples to store in a circular buffer
* SAMPLE_PERIOD is the number of ms to wait between samples.
* Sample timestamps stored in the buffer are recorded to the second.  Any sample rate smaller than 1000 will have duplicated time stamps, and the chart will not look right.

## Safety Warning
* Released under [The MIT License](https://github.com/cgapeart/KilnLogger/blob/master/LICENSE)
* No safety or reliability testing has been completed.
* No safety certifications have been sought or received
* The author(s) accept no liability for use.
* You are responsible for monitoring dangerous equipment in a safe manor, and this isn't a safe or reliable manor.
* No warranty is expressed or implied for fitness of use for any purpose.  
* Use at your own risk
* Seriously, don't burn your self, your house or other buildings, animals, family, neighborhood, or city because you trusted your build of this device and it's software to tell you something about the state of a kiln or BBQ or any other hot thing you might want to monitor.
