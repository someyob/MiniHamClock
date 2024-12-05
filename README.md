# MiniHamClock

This device was inspired by the HamClock software popular with Shortwave Radio listeners and Ham Radio operators.  (https://qso365.co.uk/2024/01/hamclock/)

It's a pretty small and relatively trivial version of that, but it's main purpose was just to give me a quick look at what the current UTC time is without having to do arithmetic in my head (sad, I know).

Secondly, every 2 hours it downloads HF propagation conditions from the hamqsl.com website as xml data, parses it, and presents it to the user.  Along with the hamqsl.com info, it calculates and displays the day's sunrise and sunset times.

Thirdly, it talks to my MQTT broker (a Raspberry Pi) which has an ESP8266 device in my shed as a client.  It publishes the current weather on a 5 minute interval.  The clock then plots that data on 3 different line graphs: temperature, relative humidity and barometric pressure for the past 20 hours or so.

All of this information is presented on separate screens as shown below.  The user can select the various screens using the rotary encoder fitted to the top of the clock.  Left to itself, it will timeout after 60 seconds from the TIME SCREEN and cycle automatically through each of the other screens, each displayed for 10 seconds.

This particular clock is based on the ESP32 S3 in an arduino uno form factor, with an SPI driven TFT display.  It's just the hardware I happened to have lying around.  If you have other hardware, you can probably adapt the code accordingly without too much trouble.  The simple schematic is in this repo.

The STLs and SketchUp models are here for 3D printing, or you can pick them up at https://www.printables.com/model/1090178-esp32-s3-uno-clock-w-22-qvga-tft-display.  Note that the model is based on the Uno formfactor microcontroller.  

STARTUP SCREEN:
![PXL_20241205_163916620](https://github.com/user-attachments/assets/0983c437-5331-4b12-bc85-108717133926)

MAIN TIME SCREEN:
![PXL_20241205_163734877](https://github.com/user-attachments/assets/cdd95420-a9d9-4e9f-a81d-26e6c2d88b65)

PROPAGATION CONDITIONS:
![PXL_20241205_163801393](https://github.com/user-attachments/assets/a91157cd-9171-4eb2-9ab2-df38114c1765)

LOCAL WEATHER SUMMARY (it's been snowing for the past little bit):
![PXL_20241205_163815292](https://github.com/user-attachments/assets/af132da0-e66e-4044-8b22-d0b6b65fdbd8)

TEMPERATURE GRAPH:
![PXL_20241205_163824847](https://github.com/user-attachments/assets/19cead08-febb-4c69-8748-cc183c41bdcd)

RH GRAPH (it was pegged at 100% when I took this shot):
![PXL_20241205_163836313](https://github.com/user-attachments/assets/8527d066-0134-467a-859d-267d452329a5)

BAROMETRIC PRESSURE GRAPH:
![PXL_20241205_163846771](https://github.com/user-attachments/assets/8b4d81ce-5dbb-4fb3-a654-bf75484778b7)



