/*
Compiled for the "ESP32S3 Dev Module" board, runs correctly on the 
Aliexpress ESP32S3 Uno board  KJ 25 Nov 2024
and the qvga 2.2 tft spi 240x320 display
*/

//  Update credentials.h for your setup!
#include <credentials.h>

#define DEBUG 1
// #define DONT_PESTER_HAMQSL 1

#define VERSION_STRING "Ham Clock v0.15"
#define RELEASE_STRING "5 Dec 2024"

///////////////////////////////////////////////////////////////
// Wifi and NTP
#include <WiFi.h>
#include "WiFiUdp.h" 
// https://github.com/sstaub/NTP/tree/main
#include "NTP.h"

#include <HTTPClient.h>   // for get prop conditions

WiFiClient espClient;
WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

unsigned long next_second = 0;
int8_t day, hour, minute, second;
bool refresh, refresh_minute, refresh_hour, refresh_day;

////////////////////////////////////////////////////////////
// Sunrise and Sunset calculations

#include <sunset.h>
// https://github.com/buelowp/sunset/blob/master/examples/esp/example.ino
SunSet sun;
int sunrise;
int sunset;

////////////////////////////////////////////////////////////
//  MQTT
//                  This is all very specific to my setup,
//                  but I included it here for those who
//                  are curious as to how I use MQTT
// https://github.com/knolleary/pubsubclient/releases/tag/v2.8

#include <PubSubClient.h>

PubSubClient mqtt_client(espClient);

float temperature, humidity, barometer;
char last_report[16];
int temp_arr[256], rh_arr[256], bp_arr[256];
int next_sample;

#define BP_STEADY 0
#define BP_FALLING 1
#define BP_RISING 2
int8_t barometer_trend;

// get the current weather (Temp, RH and BP) from the broker when pubished
// (happens every 5 minutes)
void callback(char* topic, byte* payload, unsigned int length) {
  int index, prev_sample;
  String tempStr;
  String topicStr = String(topic);
  String dataStr = "";
  if (topicStr.indexOf("Wx1/thbp") >= 0) {
    for (int j=0;j<length;j++) 
      dataStr += (char)payload[j];

    // every 5 minutes, the shed Wx station will publish the local conditions
    // comma delimited, temp,rh,bp (floating point), eg '22.2,67.3,101.78'  
    #ifdef DEBUG
    Serial.print(topicStr);
    Serial.print("  ");
    Serial.println(dataStr);
    #endif
    index = dataStr.indexOf(',');
    tempStr = dataStr.substring(0, index);
    temperature = tempStr.toFloat(); 
    dataStr = dataStr.substring(index+1);
    index = dataStr.indexOf(',');
    tempStr = dataStr.substring(0, index);
    humidity = tempStr.toFloat(); 
    dataStr = dataStr.substring(index+1);
    barometer = dataStr.toFloat(); 
    temp_arr[next_sample] = (int) (10.0 * temperature);
    rh_arr[next_sample] = (int) (10.0 * humidity);
    bp_arr[next_sample] = (int) (100.0 * barometer);
    prev_sample = next_sample - 10;
    if (prev_sample < 0) prev_sample += 256;
    // Serial.print(bp_arr[prev_sample]);  Serial.print(" "); Serial.println(bp_arr[next_sample]);
    if ((bp_arr[next_sample] - bp_arr[prev_sample]) > 5)
      barometer_trend = BP_RISING;
    else if ((bp_arr[next_sample] - bp_arr[prev_sample]) < -5)
      barometer_trend = BP_FALLING;
    else 
      barometer_trend = BP_STEADY;
    next_sample++;
    if (next_sample == 256) next_sample = 0;
    sprintf(last_report, "%s", ntp.formattedTime("%T"));
    
    #ifdef DEBUG
    Serial.print(temperature);  Serial.print("  ");
    Serial.print(humidity);  Serial.print("  ");
    Serial.print(barometer);  Serial.print(", BP");
    if (barometer_trend == BP_STEADY)
      Serial.print(" STEADY ");
    else if (barometer_trend == BP_RISING)
      Serial.print(" RISING ");
    else
      Serial.print(" FALLING ");
    Serial.println(last_report);
    #endif
  }
  
}

void reconnect() {
  // Loop until we're reconnected
  while (!mqtt_client.connected()) {
    #ifdef DEBUG
    Serial.print("Attempting MQTT connection...");
    #endif
    // Attempt to connect
    if (mqtt_client.connect("HamClient")) {
      #ifdef DEBUG
      Serial.println("connected");
      #endif
      // Once connected, publish an announcement...
      mqtt_client.publish("HamClock/Hello","1");
      // ... and resubscribe
      mqtt_client.subscribe("System/Hello");
      mqtt_client.subscribe("Wx1/thbp");
    } else {
      #ifdef DEBUG
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      #endif
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/////////////////////////////////////////////////////////////
// Display
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"

// For the Adafruit shield, these are the default.
#define TFT_DC 19
#define TFT_CS 20

#define TFT_MOSI 17
#define TFT_MISO 18
#define TFT_CLK 21
#define TFT_RST 14

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);

#define GRAPH_X0 10
#define GRAPH_Y0 25
#define GRAPH_X1 300
#define GRAPH_Y1 215

//////////////////////////////////////////////////////////////
// Rotary encoder
// https://github.com/MaffooClock/ESP32RotaryEncoder/tree/main

#include <ESP32RotaryEncoder.h>

const uint8_t DI_ENCODER_A   = 8;
const uint8_t DI_ENCODER_B   = 9;
const int8_t  DI_ENCODER_SW  = 46;

RotaryEncoder rotaryEncoder( DI_ENCODER_A, DI_ENCODER_B, DI_ENCODER_SW );

int menu_encoder;
bool clear_screen;
#define FIRST_SCREEN 0
#define LAST_SCREEN 5

#define TIME_SCREEN 0
#define PROP_SCREEN 1
#define WX_SCREEN 2
#define TEMP_SCREEN 3
#define RH_SCREEN 4
#define BP_SCREEN 5

int screen_timeout;  // for auto scrolling thru each screen

#define TIME_SCREEN_TIMEOUT 60
#define OTHER_SCREEN_TIMEOUT 10

void knobCallback( long value ) {
  #ifdef DEBUG
	Serial.printf( "Encoder value: %i\n", value );
  #endif
  menu_encoder = value;
  refresh = true;
  clear_screen = true;
  // reset the screen timeout if the user has taken over
  screen_timeout = TIME_SCREEN_TIMEOUT;   // just arbitrary
}
 
void buttonCallback( unsigned long duration ) {
  // encoder button connected but not used in this version
  #ifdef DEBUG
	Serial.printf( "Button was down for %u ms\n", duration );
  #endif
}

/////////////////////////////////////////////////////////////////////////
//  Get propagation conditions
int next_prop;  // number of seconds till next check
#define READ_PROP_INTERVAL 7200

const char *bandNames[] = {"80m-40m","30m-20m","17m-15m", "12m-10m"};
const char *daynightNames[] = {"day", "night"};
int Soffset[] = {0, 2};
String conditions[4][2];
String updated;

void get_prop() {
  HTTPClient http;
  int sindex, findex, band, daynight;
  char Ssearch[32];
  // I wasn't able to get any XML parser from the standard arduino libraries to work
  // (or I didn't understand how to use them), so I just searched for what I needed
  //  the old fashioned way
  http.begin("https://www.hamqsl.com/solarxml.php");
  #ifdef DEBUG
  Serial.print("[HTTP] GET...\n");
  #endif
  int httpCode = http.GET();

  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    #ifdef DEBUG
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
    #endif
    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      #ifdef DEBUG
      // Serial.println(payload);
      #endif
      sindex = payload.indexOf("<updated>");
      findex = payload.indexOf("</updated>");
      updated = payload.substring(sindex+10, findex);
      for (daynight=0; daynight < 2; daynight++) {
        for (band = 0; band < 4; band++) {
        
          sprintf(Ssearch, "\"%s\" time=\"%s\">", bandNames[band], daynightNames[daynight]);
          #ifdef DEBUG
          //Serial.println(Ssearch);
          #endif
          sindex = payload.indexOf(String(Ssearch));
          #ifdef DEBUG
          //Serial.print(bandNames[band]);
          //Serial.print("  ");
          //Serial.print(daynightNames[daynight]);
          //Serial.print("  ");
          #endif
          conditions[band][daynight] = payload.substring(sindex+21+Soffset[daynight], sindex+25+Soffset[daynight]);
          #ifdef DEBUG
          //Serial.println(conditions[band][daynight]); 
          #endif
        }
      }
      #ifdef DEBUG
      Serial.print("\n\nUpdated: ");
      Serial.println(updated);
      Serial.println("  ");
      Serial.println("day   night");
      for (band = 0; band < 4; band++) {
        for (daynight=0; daynight < 2; daynight++) {
          Serial.print(conditions[band][daynight]);
          Serial.print("  ");
        }
        Serial.println(bandNames[band]);
      }
      #endif
    }
  }
  next_prop = READ_PROP_INTERVAL;
}


/////////////////////////////////////////////////////////////////////////
// Setup

void setup() {
  char payload[32];

  #ifdef DEBUG
  Serial.begin(115200);
  Serial.println(VERSION_STRING);
  Serial.println(RELEASE_STRING);
  #endif
 
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  #ifdef DEBUG
  // read diagnostics from display
  uint8_t x = tft.readcommand8(ILI9341_RDMODE);
  Serial.print("Display Power Mode: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDMADCTL);
  Serial.print("MADCTL Mode: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDPIXFMT);
  Serial.print("Pixel Format: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDIMGFMT);
  Serial.print("Image Format: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDSELFDIAG);
  Serial.print("Self Diagnostic: 0x"); Serial.println(x, HEX); 
  #endif
  
  tft.setTextColor(ILI9341_WHITE);  
  
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.println("Mini");
  tft.setTextSize(3);
  tft.println(VERSION_STRING);
  tft.setTextSize(2);
  tft.println("\nRelease date:");
  tft.setTextSize(3);
  tft.print(RELEASE_STRING);  
  tft.setTextColor(ILI9341_YELLOW);  
  tft.println("   KJ");
  tft.setTextSize(2);
  
  tft.println("\nConnecting to Wifi...");
  delay(1000);
  WiFi.begin(ssid, password);
  delay(1000); 
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    #ifdef DEBUG
    Serial.println("Connection Failed! Rebooting...");
    #endif
    delay(5000);
    ESP.restart();
  }
  delay(1000); 
  tft.println("IP address:");
  sprintf(payload, "%s", WiFi.localIP().toString().c_str());
  tft.println(payload);
  #ifdef DEBUG
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(payload);
  Serial.print("MAC Address:  ");
  Serial.println(WiFi.macAddress());
  #endif
  
  tft.println("Connected");

  ntp.ruleDST(DAYLIGHT_SAVINGS, Second, Sun, Mar, 2, (TIMEZONE-1)*60); 
  ntp.ruleSTD(STANDARD_TIME, First, Sun, Nov, 3, TIMEZONE*60); 

  ntp.begin();
  ntp.updateInterval(60000);  // updateInterval in ms, default = 60000ms
  #ifdef DEBUG
  Serial.println("started NTP");
  #endif
  tft.println("Started NTP");
  delay(1000);

  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
  tft.println("Connected to MQTT");
  delay(4000);

  clear_screen = true;
  tft.setTextColor(ILI9341_WHITE);  

  rotaryEncoder.setEncoderType( EncoderType::FLOATING );  // FLOATING = needs pullup
	rotaryEncoder.setBoundaries( FIRST_SCREEN, LAST_SCREEN, true );   // true = wrap
	rotaryEncoder.onTurned( &knobCallback );
	rotaryEncoder.onPressed( &buttonCallback );
	rotaryEncoder.begin();
  menu_encoder = TIME_SCREEN;

  refresh = true;
  refresh_minute = true;
  refresh_hour = true;
  refresh_day = true;
  hour = 0;
  minute = 0;
  second = 0;
  next_second = millis() + 1000;

  get_prop();   

  sun.setPosition(LATITUDE, LONGITUDE, TIMEZONE);
  sun.setTZOffset(TIMEZONE);

  temperature = 0.0;
  humidity = 0.0;
  barometer = 0.0;
  for (int i=0; i<256; i++) {
    temp_arr[i] = 100;
    rh_arr[i] = 500;
    bp_arr[i] = 10400;
  }
  next_sample = 0;
  screen_timeout = TIME_SCREEN_TIMEOUT;


}


/////////////////////////////////////////////////////////////////////
// Loop  - one gigantic do-loop :) because that's how we roll

void loop(void) {
  char buffer[32];

  bool DST;
  int8_t temp, toUTC;
  int max, min, span, high, low;
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.println("No Wifi");
    delay(5000);
    ESP.restart();
    #ifdef DEBUG
    Serial.println("Lost Wifi");
    #endif
    refresh = true;
  }
  
  ntp.update();

  if (!mqtt_client.connected()) 
    reconnect();
  mqtt_client.loop();

  if (next_prop <= 0) {
    #ifndef DONT_PESTER_HAMQSL     // when debugging, avoid hammering hamqsl.com
    get_prop();                    // with repeated requests
    #endif
  }
/*
  DST = ntp.isDST();

  if (refresh_day) {
    sun.setCurrentDate(ntp.year(), ntp.month(), ntp.day());
    sunrise = static_cast<int>(sun.calcSunrise());
    sunset = static_cast<int>(sun.calcSunset());
    if (DST) {
      sunrise += 60;
      sunset += 60;
    }
    #ifdef DEBUG
    sprintf(buffer, "%d/%d/%d", ntp.day(), ntp.month(), ntp.year());
    Serial.println(buffer);
    sprintf(buffer, "Sunrise at %02d:%02d", sunrise/60, sunrise%60);
    Serial.println(buffer);
    sprintf(buffer, "Sunset at %02d:%02d", sunset/60, sunset%60);
    Serial.println(buffer);
    #endif
  } */

  switch(menu_encoder) {
    case TIME_SCREEN:
      if (refresh) {

        next_second = millis() + 1000;

        DST = ntp.isDST();

        if (DST) toUTC = TIMEZONE - 1; else toUTC = TIMEZONE;
        temp = day;
        day = ntp.day();
        refresh_day = (temp != day);
        temp = hour;
        hour = ntp.hours() - toUTC;  // convert local to UTC
        if (hour >= 24) 
          hour = hour - 24;
        else if (hour < 0)
          hour = hour + 24;

        refresh_hour = (temp != hour);
        temp = minute;
        minute = ntp.minutes();
        refresh_minute = (temp != minute);
        second = ntp.seconds();

        if (clear_screen) {
          tft.fillScreen(ILI9341_BLACK); 
          clear_screen = false;
          refresh_day = true;
          refresh_hour = true;
          refresh_minute = true;

        }

        tft.setTextColor(ILI9341_WHITE);
        if (refresh_hour) {
          tft.setTextSize(8);
          sprintf(buffer, "%02d", hour);
          tft.fillRect(0, 0, 90, 60, ILI9341_BLACK);
          tft.setCursor(0, 0);
          tft.println(buffer);
        
          tft.setCursor(85, 0);
          tft.println(":");

          tft.setTextSize(4);
          tft.fillRect(0, 180, 45, 35, ILI9341_BLACK);
          int8_t l_hour = hour+toUTC;  // convert UTC back to local
          // Serial.print(hour);
          //Serial.print("  ");
          //Serial.println(l_hour);
          if (l_hour >= 24)
            l_hour = l_hour - 24;
          else if (l_hour <= 0)
            l_hour = l_hour + 24;
          //Serial.println(l_hour);
          sprintf(buffer, "%02d", l_hour);
          tft.setCursor(0, 180);
          tft.println(buffer);
          tft.setCursor(45, 180);
          tft.println(":");
        }
        if (refresh_minute) {
          tft.setTextSize(8);
          sprintf(buffer, "%02d", minute);
          tft.fillRect(120, 0, 90, 60, ILI9341_BLACK);
          tft.setCursor(120, 0);
          tft.println(buffer);

          tft.setTextSize(4);
          tft.fillRect(67, 180, 45, 35, ILI9341_BLACK);
          sprintf(buffer, "%02d", minute);
          tft.setCursor(67, 180);
          tft.println(buffer);

        }
        tft.setTextSize(4);
        sprintf(buffer, ":%02d", second);
        tft.setCursor(215, 0);
        tft.fillRect(238, 0, 46, 31, ILI9341_BLACK);
        tft.println(buffer);

        if (refresh_day) {
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(3);
          tft.setCursor(225, 38);
          tft.println("UTC");
          
          tft.fillRect(0, 80, 320, 55, ILI9341_BLACK);
          tft.setCursor(0, 80);
          tft.setTextColor(ILI9341_YELLOW);
          tft.println(ntp.formattedTime("%A"));
          tft.println(ntp.formattedTime("%d %B %Y"));
          tft.println("\nLocal:");

          tft.fillRect(125, 180, 55, 30, ILI9341_BLACK);
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(3);
          tft.setCursor(125, 180);
          if (DST)
            tft.println(DAYLIGHT_SAVINGS);
          else
            tft.println(STANDARD_TIME);
          if (refresh_day) {
            sun.setCurrentDate(ntp.year(), ntp.month(), ntp.day());
            sunrise = static_cast<int>(sun.calcSunrise());
            sunset = static_cast<int>(sun.calcSunset());
            if (DST) {
              sunrise += 60;
              sunset += 60;
            }
            #ifdef DEBUG
            sprintf(buffer, "%d/%d/%d", ntp.day(), ntp.month(), ntp.year());
            Serial.println(buffer);
            sprintf(buffer, "Sunrise at %02d:%02d", sunrise/60, sunrise%60);
            Serial.println(buffer);
            sprintf(buffer, "Sunset at %02d:%02d", sunset/60, sunset%60);
            Serial.println(buffer);
            #endif
          }
          refresh_day = false;
        }

        refresh = false;
        // delay(1000);
        
      }
      break;
    case PROP_SCREEN:
      if (refresh) {
        int band, daynight;
        if (clear_screen) {
          tft.fillScreen(ILI9341_BLACK); 
          clear_screen = false;
          
          tft.setTextSize(2);
          tft.setTextColor(ILI9341_WHITE);
          tft.setCursor(0, 0);
          tft.println("Propagation Conditions");
           tft.setTextColor(ILI9341_YELLOW);
          tft.println("              hamqsl.com");
           tft.setTextColor(ILI9341_WHITE);
          tft.println("Updated:");
          tft.println(updated);
        
          tft.println("\n         Day   Night");
          for (band = 0; band < 4; band++) {
            tft.setTextColor(ILI9341_WHITE);
            tft.print(bandNames[band]);
            tft.print("  ");
            
            for (daynight=0; daynight < 2; daynight++) {
              if (conditions[band][daynight][0] == 'G')
                tft.setTextColor(ILI9341_GREEN);
              else if (conditions[band][daynight][0] == 'F')
                tft.setTextColor(ILI9341_YELLOW);
              else
                tft.setTextColor(ILI9341_RED);
              tft.print(conditions[band][daynight]);
              tft.print("  ");
            }
            tft.println(" ");
          }
          tft.setTextColor(ILI9341_YELLOW);
          //tft.println("\nCourtesy of hamqsl.com\n");

          sprintf(buffer, "\nSunrise: %02d:%02d", sunrise/60, sunrise%60);
          tft.println(buffer);
          sprintf(buffer, " Sunset: %02d:%02d", sunset/60, sunset%60);
          tft.println(buffer);
        }
        refresh = false;
        }
      break;
    case WX_SCREEN:
      if (refresh) {
        if (clear_screen) {
          tft.fillScreen(ILI9341_BLACK); 
          clear_screen = false;
          
          tft.setTextSize(2);
          tft.setTextColor(ILI9341_WHITE);
          tft.setCursor(0, 0);
          tft.println("Local Weather Conditions");
          
          tft.setTextColor(ILI9341_YELLOW);
          tft.println("\nTemperature");
          tft.setTextSize(3);
          tft.setTextColor(ILI9341_WHITE);
          sprintf(buffer, "    %0.0f degC", temperature);
          tft.println(buffer);

          tft.setTextSize(2);
          tft.setTextColor(ILI9341_YELLOW);
          tft.println("\nHumidity");
          tft.setTextSize(3);
          tft.setTextColor(ILI9341_WHITE);
          sprintf(buffer, "    %0.1f%%", humidity);
          tft.println(buffer);

          tft.setTextSize(2);
          tft.setTextColor(ILI9341_YELLOW);
          tft.println("\nBarometer");
          tft.setTextSize(3);
          tft.setTextColor(ILI9341_WHITE);
          sprintf(buffer, "    %0.2f kPa", barometer);
          tft.println(buffer);
          if (barometer_trend == BP_STEADY) {
            tft.drawChar(260, 150, (char) (0x1B), ILI9341_YELLOW, ILI9341_BLACK, 3);
            tft.drawChar(275, 150, (char) (0x1A), ILI9341_YELLOW, ILI9341_BLACK, 3);
          } else if (barometer_trend == BP_RISING)
            tft.drawChar(260, 150, (char) (0x18), ILI9341_YELLOW, ILI9341_BLACK, 3);
          else
            tft.drawChar(260, 150, (char) (0x19), ILI9341_YELLOW, ILI9341_BLACK, 3);

          tft.setTextSize(2);
          tft.setTextColor(ILI9341_YELLOW);
          tft.println("\nUpdated");
          tft.setTextColor(ILI9341_WHITE);
          tft.println(last_report);
          }
        refresh = false;
        }
        break;
    case TEMP_SCREEN:
      if (refresh) {
        if (clear_screen) {
          tft.fillScreen(ILI9341_BLACK); 
          clear_screen = false;
          int i, sample;
          int x0, y0, x1, y1;
          tft.setTextSize(2);
          tft.setTextColor(ILI9341_WHITE);
          tft.setCursor(0, 0);
          tft.println("Temperature");
          max=-32767;
          min=32768;
          for (i=0; i<256; i++) {
            if (temp_arr[i] > max) max=temp_arr[i];
            if (temp_arr[i] < min) min=temp_arr[i];
            }
          span = abs(max-min);
          if (span < 5) span = 20;  // if less than 5 (near zero), make it at least 2 degrees)
          high = (int) (max/10.0);
          low = (int) (min/10.0);
          max = max + (int) (0.3*span);
          min = min - (int) (0.3*span);
          #ifdef DEBUG
          Serial.print("Temp min;max  "); Serial.print(min);  Serial.print(";"); Serial.println(max);
          #endif
          sample=next_sample;
          for (i=1; i<256; i++) {
            x0 = map(i-1, 0, 255, GRAPH_X0, GRAPH_X1);
            y0 = map(temp_arr[sample], min, max, GRAPH_Y1, GRAPH_Y0);
            sample++;
            if (sample == 256) sample = 0;
            x1 = map(i, 0, 255, GRAPH_X0, GRAPH_X1);
            y1 = map(temp_arr[sample], min, max, GRAPH_Y1, GRAPH_Y0);
            tft.drawLine(x0, y0, x1, y1, ILI9341_WHITE);
            #ifdef DEBUG
            // Serial.print(sample); Serial.print("  ");  Serial.print(temp_arr[sample]); Serial.print("   ");
            // Serial.print(x0);  Serial.print(","); Serial.print(y0); Serial.print(" "); Serial.print(x1); Serial.print(","); Serial.println(y1);
            #endif
            }
          // tft.drawRect(GRAPH_X0, GRAPH_Y0, GRAPH_X1-GRAPH_X0, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          tft.drawRect(0, GRAPH_Y0, 319, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          if ((min < 0) && (max>0)) {  // draw 0 degC
            y0 = map(0, min, max, GRAPH_Y1, GRAPH_Y0);
            tft.drawLine(0, y0, 319, y0, ILI9341_GREEN);
          }
          sprintf(buffer, "High %d, Low %d degC", high, low);
          tft.setCursor(0, 220);
          tft.println(buffer);
          refresh = false;
          }
        }
        break;
    case RH_SCREEN:
      if (refresh) {
        if (clear_screen) {
          tft.fillScreen(ILI9341_BLACK); 
          clear_screen = false;
          int i, sample;
          int x0, y0, x1, y1;
          tft.setTextSize(2);
          tft.setTextColor(ILI9341_WHITE);
          tft.setCursor(0, 0);
          tft.println("Relative Humidity");
          //tft.drawRect(GRAPH_X0, GRAPH_Y0, GRAPH_X1-GRAPH_X0, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          max=-32767;
          min=32768;
          for (i=0; i<256; i++) {
            if (rh_arr[i] > max) max=rh_arr[i];
            if (rh_arr[i] < min) min=rh_arr[i];
            }
          span = abs(max-min);
          if (span < 5) span = 50;   // if it's practically zero, make it at least 5% RH
          max = max + (int) (0.2*span);
          min = min - (int) (0.2*span);
          #ifdef DEBUG
          Serial.print("RH min;max  "); Serial.print(min);  Serial.print(";"); Serial.println(max);
          #endif
          sample=next_sample;
          for (i=1; i<256; i++) {
            x0 = map(i-1, 0, 255, GRAPH_X0, GRAPH_X1);
            y0 = map(rh_arr[sample], min, max, GRAPH_Y1, GRAPH_Y0);
            sample++;
            if (sample == 256) sample = 0;
            x1 = map(i, 0, 255, GRAPH_X0, GRAPH_X1);
            y1 = map(rh_arr[sample], min, max, GRAPH_Y1, GRAPH_Y0);
            tft.drawLine(x0, y0, x1, y1, ILI9341_WHITE);
            #ifdef DEBUG
            //Serial.print(sample); Serial.print("  ");  Serial.print(rh_arr[sample]); Serial.print("   ");
            //Serial.print(x0);  Serial.print(","); Serial.print(y0); Serial.print(" "); Serial.print(x1); Serial.print(","); Serial.println(y1);
            #endif
            }
          tft.drawRect(0, GRAPH_Y0, 319, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          // tft.drawRect(GRAPH_X0, GRAPH_Y0, GRAPH_X1-GRAPH_X0, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          sprintf(buffer, "Current %d%%", (int) (rh_arr[sample]/10.0));
          tft.setCursor(0, 220);
          tft.println(buffer);
          }
          refresh = false;
          }
        break;
    case BP_SCREEN:
      if (refresh) {
        if (clear_screen) {
          tft.fillScreen(ILI9341_BLACK); 
          clear_screen = false;
          int i, sample;
          int x0, y0, x1, y1;
          tft.setTextSize(2);
          tft.setTextColor(ILI9341_WHITE);
          tft.setCursor(0, 0);
          tft.println("Barometer");
          // tft.drawRect(GRAPH_X0, GRAPH_Y0, GRAPH_X1-GRAPH_X0, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          max=-32767;
          min=32768;
          for (i=0; i<256; i++) {
            if (bp_arr[i] > max) max=bp_arr[i];
            if (bp_arr[i] < min) min=bp_arr[i];
            }
          
          span = abs(max-min);
          max = max + (int) (0.2*span);
          min = min - (int) (0.2*span);
          #ifdef DEBUG
          Serial.print("BP min;max  "); Serial.print(min);  Serial.print(";"); Serial.println(max);
          #endif
          sample=next_sample;
          for (i=1; i<256; i++) {
            x0 = map(i-1, 0, 255, GRAPH_X0, GRAPH_X1);
            y0 = map(bp_arr[sample], min, max, GRAPH_Y1, GRAPH_Y0);
            sample++;
            if (sample == 256) sample = 0;
            x1 = map(i, 0, 255, GRAPH_X0, GRAPH_X1);
            y1 = map(bp_arr[sample], min, max, GRAPH_Y1, GRAPH_Y0);
            tft.drawLine(x0, y0, x1, y1, ILI9341_WHITE);
            #ifdef DEBUG
            //Serial.print(sample); Serial.print("  ");  Serial.print(rh_arr[sample]); Serial.print("   ");
            //Serial.print(x0);  Serial.print(","); Serial.print(y0); Serial.print(" "); Serial.print(x1); Serial.print(","); Serial.println(y1);
            #endif
            }
          // tft.drawRect(GRAPH_X0, GRAPH_Y0, GRAPH_X1-GRAPH_X0, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          tft.drawRect(0, GRAPH_Y0, 319, GRAPH_Y1-GRAPH_Y0, ILI9341_GREEN);
          tft.setCursor(0, 220);
          if (barometer_trend == BP_STEADY) {
            tft.println("STEADY");
          } else if (barometer_trend == BP_RISING)
            tft.println("RISING");
          else
            tft.println("FALLING");
          }
          refresh = false;
          }
        break;
    default:
      break;
  }

if (millis() > next_second) {
  refresh = true;
  next_second = millis() + 1000;
  next_prop--;   // decrement timer to fetch prop conditions
  screen_timeout--;
  if (screen_timeout == 0) {
    refresh = true;
    clear_screen = true;
    if (menu_encoder == LAST_SCREEN)
      menu_encoder = FIRST_SCREEN;
    else
      menu_encoder++;
    if (menu_encoder == TIME_SCREEN)
      screen_timeout = TIME_SCREEN_TIMEOUT;
    else
      screen_timeout = OTHER_SCREEN_TIMEOUT;
  }
  }

}




