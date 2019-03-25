#define BLYNK_PRINT Serial /* Comment this out to disable prints and save space */
#define BLYNK_MAX_SENDBYTES 128

#include <BlynkSimpleEsp8266.h>
#include "ThingSpeak.h"
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include "Adafruit_HTU21DF.h"
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WidgetRTC.h>
#include <TimeLib.h>

char auth[] = "bc08f366b13147d6ab50d36f25a73438";
ESP8266WiFiMulti wifiMulti;
Adafruit_BMP085 bmp;
Adafruit_HTU21DF htu;// = Adafruit_HTU21DF();
WiFiClient  client;
BlynkTimer timer;
WidgetTerminal terminal(V10);
WidgetRTC rtc;
float HTU21_Temperature, HTU21_Humidity, BMP180_Temperature, BMP180_Pressure, BMP180_Altitude, TEMP6000_Light_Ave, TEMP6000_Light_Ave_prev, TEMP6000_Light_Ave_diff, TEMP6000_Light_Ave_prev_prev;
int analogInPin = A0;
int TEMP6000_Light, TEMP6000_Light_readings[20], light_cntr = 0;
bool light_ave_enable = true, first_run = true, LED_blink_enable = true, LED_blink_on;
int ave_num = 15;
int light_max_limit = 10;
int LED_blink_cntr;

unsigned long myChannelNumber = 662999;
const char * myWriteAPIKey = "HEGK4WE3XN7Y9JTE";

int LED_ANTENNA = 2;  // near the antenna

void setup(void)
{
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT); // pin 16 main board LED = LED_BUILTIN
  digitalWrite(LED_BUILTIN, LOW); // ON
  pinMode(LED_ANTENNA, OUTPUT); // pin 2 near the antenna
  digitalWrite(LED_ANTENNA, LOW); // ON

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("PLDTHOMEFIBRAntipoloPolice", "TAGAYUNFAMILY");

  Serial.print("Connecting Wifi ");

  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(LED_ANTENNA, LOW); // ON
    delay(100);
    digitalWrite(LED_ANTENNA, HIGH); // OFF
    delay(100);
  }

  if (wifiMulti.run() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_ANTENNA, HIGH);   // turn the ANTENNA LED off
    LED_blink_on = false;
  }

  ThingSpeak.begin(client);  // Initialize ThingSpeak
  Blynk.config(auth);

  if (!bmp.begin())
  {
    Serial.println("Couldn't find BMP sensor!");
    while (1)
    {
      digitalWrite(LED_BUILTIN, LOW); // ON
      delay(200);
      digitalWrite(LED_BUILTIN, HIGH); // OFF
      delay(200);
    }
  }

  if (!htu.begin()) {
    Serial.println("Couldn't find HTU sensor!");
    while (1)
    {
      digitalWrite(LED_BUILTIN, LOW); // ON
      delay(500);
      digitalWrite(LED_BUILTIN, HIGH); // OFF
      delay(500);
    }
  }

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

  timer.setInterval(120000L, ThingSpeakDatalog); // every 2mins log to ThingSpeak
  timer.setInterval(5000L, check_wifi_and_Light_and_email); // every 5secs check wifi
  timer.setInterval(200L, LED_blink); // every 200secs check wifi

}

BLYNK_CONNECTED() {
  // Synchronize time on connection
  rtc.begin();
}

void loop(void)
{
  timer.run();
  Blynk.run();
  ArduinoOTA.handle();
}

void LED_blink()
{
  if (LED_blink_enable)
  {
    if (LED_blink_on)
    {
      digitalWrite(LED_BUILTIN, LOW);   // turn the LED on
      LED_blink_on = false;
      LED_blink_cntr = 0;
    } else
    {
      digitalWrite(LED_BUILTIN, HIGH);   // turn the LED off
    }

    if (LED_blink_cntr == 5)
    {
      LED_blink_on = true;
    }

    LED_blink_cntr++;
  }
}

void ThingSpeakDatalog()
{
  rtc.begin();
  int hour_now = hour();
  String currentTime = String(hour_now) + ":" + minute() + ":" + second();
  String currentDate = String(day()) + " " + month() + " " + year() + " " + weekday();

  if ((hour_now >= 7) && (hour_now <= 17)) LED_blink_enable = true;

  delay(100);
  TEMP6000_Light = analogRead(analogInPin);
  //  terminal.print("TEMP6000_Light : ");
  //  terminal.println(TEMP6000_Light);
  //  terminal.flush();

  delay(20);
  HTU21_Temperature = htu.readTemperature();
  delay(20);
  HTU21_Humidity = htu.readHumidity();

  delay(20);
  BMP180_Temperature = bmp.readTemperature();
  delay(20);
  BMP180_Pressure = bmp.readPressure();
  delay(20);
  BMP180_Altitude = bmp.readAltitude();

  TEMP6000_Light_Ave_diff = TEMP6000_Light_Ave - TEMP6000_Light_Ave_prev;
  TEMP6000_Light_Ave_prev = TEMP6000_Light_Ave;

  if (first_run) // TEMP6000_Light_Ave_diff will be TEMP6000_Light_Ave if first run, so set zero to correct this
  {
    first_run = false;
    TEMP6000_Light_Ave_diff = 0;
  }

  // set the fields with the values
  ThingSpeak.setField(1, HTU21_Temperature);
  ThingSpeak.setField(2, HTU21_Humidity);
  ThingSpeak.setField(3, BMP180_Temperature);
  ThingSpeak.setField(4, BMP180_Pressure);
  ThingSpeak.setField(5, BMP180_Altitude);
  ThingSpeak.setField(6, TEMP6000_Light);
  ThingSpeak.setField(7, TEMP6000_Light_Ave);
  ThingSpeak.setField(8, TEMP6000_Light_Ave_diff);

  Blynk.virtualWrite(V0, currentTime);
  Blynk.virtualWrite(V1, HTU21_Temperature);
  Blynk.virtualWrite(V2, HTU21_Humidity);
  Blynk.virtualWrite(V4, BMP180_Pressure);
  Blynk.virtualWrite(V5, BMP180_Altitude);
  Blynk.virtualWrite(V6, TEMP6000_Light);
  Blynk.virtualWrite(V7, TEMP6000_Light_Ave);
  Blynk.virtualWrite(V11, currentDate);

  TEMP6000_Light_Ave = 0;
  light_ave_enable = true;
  light_cntr = 0;

  // write to the ThingSpeak channel
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (x == 200) {
    Serial.println("Channel update successful.");
    //    terminal.println("Channel update successful.");
  }
  else {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
    //    terminal.println("Problem updating channel. HTTP error code " + String(x));
  }

  //  terminal.println(currentTime);
  //  terminal.flush();

}

void check_wifi_and_Light_and_email() // every 5secs
{
  if (wifiMulti.run() == WL_CONNECTED) {
    digitalWrite(LED_ANTENNA, HIGH);   // turn the LED off
    LED_blink_enable = true;
  } else
  {
    digitalWrite(LED_ANTENNA, LOW);   // turn the LED ON
    LED_blink_enable = false;
  }

  //  terminal.print("light_ave_enable : ");
  //  terminal.println(light_ave_enable);
  //  terminal.print("light_cntr : ");
  //  terminal.println(light_cntr);

  if (light_ave_enable)
  {
    TEMP6000_Light_readings[light_cntr++] = analogRead(analogInPin);
    //    terminal.print("TEMP6000_Light_readings : ");
    //    terminal.println(TEMP6000_Light_readings[light_cntr - 1]);
    //    terminal.print("light_cntr : ");
    //    terminal.println(light_cntr);
    if (light_cntr >= ave_num)
    {
      light_ave_enable = false;
      //      terminal.print("light_ave_enable : ");
      //      terminal.println(light_ave_enable);
      //      terminal.print("light_cntr : ");
      //      terminal.println(light_cntr);
      for (int cntr = 0; cntr <= light_cntr ; cntr++)
      {
        TEMP6000_Light_Ave = TEMP6000_Light_Ave + TEMP6000_Light_readings[cntr];
      }
      //      terminal.print("TEMP6000_Light_Ave : ");
      //      terminal.println(TEMP6000_Light_Ave);
      TEMP6000_Light_Ave = (TEMP6000_Light_Ave / light_cntr);
      //      terminal.print("TEMP6000_Light_Ave : ");
      //      terminal.println(TEMP6000_Light_Ave);
    }
  }

  if (first_run)
  {
    if (light_cntr == 1)
    {
      // Clear the terminal content
      terminal.flush();
      terminal.clear();

      // This will print Blynk Software version to the Terminal Widget when
      // your hardware gets connected to Blynk Server
      terminal.println(F("Blynk v" BLYNK_VERSION ": Device started"));
      terminal.println(F("1--------0---------0---------0---------0--34"));
      terminal.flush();
    }

    // email remove email address in email widget
    switch (light_cntr) {
      case 1:
        Blynk.email("vic.tagayun@yahoo.com", "Weather Station", "Weather Station Reset or Power up");
        terminal.println("vic.tagayun@yahoo.com");
        terminal.flush();
        break;
      case 3:
        Blynk.email("victor.t@asmpt.com", "Weather Station", "Weather Station Reset or Power up");
        terminal.println("victor.t@asmpt.com");
        terminal.flush();
        break;
      case 5:
        Blynk.email("maritespesimotagayun@yahoo.com", "Weather Station", "Weather Station Reset or Power up");
        terminal.println("maritespesimotagayun@yahoo.com");
        terminal.flush();
        break;
      case 7:
        Blynk.email("colettetagayun@yahoo.com", "Weather Station", "Weather Station Reset or Power up");
        terminal.println("colettetagayun@yahoo.com");
        terminal.flush();
        break;
      case 9:
        Blynk.email("lecottetagayun@yahoo.com", "Weather Station", "Weather Station Reset or Power up");
        terminal.println("lecottetagayun@yahoo.com");
        terminal.flush();
        break;
      case 11:
        Blynk.email("colettetagayun@gmail.com", "Weather Station", "Weather Station Reset or Power up");
        terminal.println("colettetagayun@gmail.com");
        terminal.flush();
        break;
      case 13:
        Blynk.email("victorcoletagayun@yahoo.com", "Weather Station", "Weather Station Reset or Power up");
        terminal.println("victorcoletagayun@yahoo.com");
        terminal.flush();
        break;
    }
  }

  if (TEMP6000_Light_Ave_diff > light_max_limit)
  {
    switch (light_cntr) {
      case 1:
        Blynk.email("vic.tagayun@yahoo.com", "Master Room @ Tipolo Light Turned ON!", "Did Someone Turn the Light ON?");
        break;
      case 3:
        Blynk.email("victor.t@asmpt.com", "Master Room @ Tipolo Light Turned ON!", "Did Someone Turn the Light ON?");
        break;
        //      case 5:
        //        Blynk.email("maritespesimotagayun@yahoo.com", "Master Room @ Tipolo Light Turned ON!", "Did Someone Turn the Light ON?");
        //        break;
        //      case 7:
        //        Blynk.email("colettetagayun@yahoo.com", "Master Room @ Tipolo Light Turned ON!", "Did Someone Turn the Light ON?");
        //        break;
        //      case 9:
        //        Blynk.email("lecottetagayun@yahoo.com", "Master Room @ Tipolo Light Turned ON!", "Did Someone Turn the Light ON?");
        //        break;
        //      case 11:
        //        Blynk.email("colette_lecotte_victorcole@yahoo.com", "Master Room @ Tipolo Light Turned ON!", "Did Someone Turn the Light ON?");
        //        break;
        //      case 13:
        //        Blynk.email("victorcoletagayun@yahoo.com", "Master Room @ Tipolo Light Turned ON!", "Did Someone Turn the Light ON?");
        //        break;
    }
  }

  if (TEMP6000_Light_Ave_diff < -light_max_limit)
  {
    switch (light_cntr) {
      case 1:
        Blynk.email("vic.tagayun@yahoo.com", "Master Room @ Tipolo Light Turned OFF!", "Did Someone Turn the Light OFF?");
        break;
      case 3:
        Blynk.email("victor.t@asmpt.com", "Master Room @ Tipolo Light Turned OFF!", "Did Someone Turn the Light OFF?");
        break;
        //      case 5:
        //        Blynk.email("maritespesimotagayun@yahoo.com", "Master Room @ Tipolo Light Turned OFF!", "Did Someone Turn the Light OFF?");
        //        break;
        //      case 7:
        //        Blynk.email("colettetagayun@yahoo.com", "Master Room @ Tipolo Light Turned OFF!", "Did Someone Turn the Light OFF?");
        //        break;
        //      case 9:
        //        Blynk.email("lecottetagayun@yahoo.com", "Master Room @ Tipolo Light Turned OFF!", "Did Someone Turn the Light OFF?");
        //        break;
        //      case 11:
        //        Blynk.email("colette_lecotte_victorcole@yahoo.com", "Master Room @ Tipolo Light Turned OFF!", "Did Someone Turn the Light OFF?");
        //        break;
        //      case 13:
        //        Blynk.email("victorcoletagayun@yahoo.com", "Master Room @ Tipolo Light Turned OFF!", "Did Someone Turn the Light OFF?");
        //        break;
    }
  }
}
