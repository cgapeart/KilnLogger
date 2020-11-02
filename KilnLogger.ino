#include <TM1637Display.h>
#include <ArduinoOTA.h>
#include "Adafruit_MAX31856.h"
#include <memory>
#include <TZ.h>
//-----------------------------------------
//LOCAL SETTINGS - update in this block for your needs

#define STASSID "SSID"
#define STAPSK  "PSK"
#define STANAME "KilnLogger-1"

#define USE_CELCIUS 0


//See https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h 
//for available time zone constants
#define TIME_ZONE TZ_America_Edmonton 
 
#define NTP_SERVER "0.ca.pool.ntp.org"
#define THERMOCOUPLE_TYPE MAX31856_TCTYPE_K


//Pin mappings max31865 breakout pin = ESP8266 pin
const uint8_t xCS = D3;
const uint8_t xSDI = D2;
const uint8_t xSDO = D1;
const uint8_t xSCK = D0;
const uint8_t xFLT = D5;
const uint8_t xDRD = D6;

//Update the 7seg LED display more often.
#define UPDATE_LED7SEG 1000
//Pin mappings for TMC1637 7 segment display
const uint8_t xDIO = D7;
const uint8_t xCLK = D6;

//1800 samples at 10 seconds apart is 5 hours.
//How many samples to keep and graph
#define BUF_LEN 3600
//How often to sample the temperatures
#define SAMPLE_PERIOD 2000



//END LOCAL SETTINGS
//-----------------------------------------

#define RESP_BUFFER_LEN 5000
#define RESP_BUFFER_HIGH_WATER 4500


#if(USE_CELCIUS > 0)
#define UNIT "C"
#define TEMP(x) (x)
#else
#define UNIT "F"
#define TEMP(x) (x*9/5+32)
#endif

const char *ssid = STASSID;
const char *password = STAPSK;
const char *mdnsName = STANAME;

#include <ESPPerfectTime.h>
#include <sntp.h>
#include <LittleFS.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

TM1637Display  display7Seg(xCLK, xDIO);
struct sample_t
{
  time_t time;
  float temp;
};
volatile int current = 0;
sample_t buffer[BUF_LEN] = {0};


ESP8266WebServer server(80);

Adafruit_MAX31856 tempSensor(xCS, xSDI, xSDO, xSCK);

static const char PROGMEM rootPage[] = "<html>\n"
                                       "<head>\n"
                                       "<title>Internet Of Kilns - " STANAME "</title>\n"
                                       "<link rel=\"stylesheet\" href=\"/chartist.min.css\">\n"
                                       "<link rel=\"stylesheet\" href=\"/legend.css\">\n"
                                       "<link rel=\"shortcut icon\" href=\"/favicon.ico\">\n"
                                       "<style>\n"
                                       "body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\n"
                                       "</style>\n"
                                       "</head>\n"
                                       "<body>\n"
                                       "<H1>" STANAME "</h1>\n"
                                       "<p>Uptime: %02d:%02d:%02d</p>\n"
                                       "<p>Date and time: %04d-%02d-%02d %02d:%02d:%02d</p>\n"
                                       "<p>Outside Temp:%6.2f &deg;" UNIT "</p>\n"
                                       "<p>Current Temp:%6.2f &deg;" UNIT "</p>\n"
                                       "<p>Rate of change: %6.2f &deg;" UNIT "/minute</p>\n"
                                       "<p>Max Temperature: %6.2f &deg; " UNIT " <a href=\"/resetMax\">reset</a>\n"
                                       "<div class=\"ct-chart ct-octave\"></div>\n"
                                       "<script src=\"/chartist.min.js\"></script>\n"
                                       "<script src=\"/moment.min.js\"></script>\n"
                                       "<script src=\"/axistitle.min.js\"></script>\n"
                                       //"<script src=\"/legend.js\"></script>\n"
                                       "<script src=\"/data.js\"/></script>\n"
                                       "<script>\n"
                                       "new Chartist.Line('.ct-chart', data \n"
                                       "  ,{ axisX: \n"
                                       "     { \n"
                                       "       type: Chartist.AutoScaleAxis, \n"
                                       "       scaleMinSpace: 60, \n"
                                       "       labelInterpolationFnc: \n"
                                       "         function(value) \n"
                                       "          { \n"
                                       "            return moment(value).format('HH:mm:ss');\n"
                                       "          }\n"
                                       "     } \n"
                                       "   , axisY: \n"
                                       "     { \n"
                                       "        onlyInteger: true \n"
                                       "     } \n"
                                       "   , \n"
                                       "      chartPadding:{ top: 40, right:0, bottom:30, left: 20}, \n"
                                       "      showPoint: false,\n"
                                       "      plugins: [ \n"
                                       "        Chartist.plugins.ctAxisTitle({\n"
                                       "          axisX : { \n"
                                       "                     axisTitle:\"Time\", \n"
                                       "                     axisClass: \"ct-axis-title\",\n"
                                       "                     offset :{ x:0, y:50},\n"
                                       "                     textAnchor: \"middle\"\n"
                                       "                   },\n"
                                       "          axisY : {\n"
                                       "                     axisTitle:\"Temp (deg. " UNIT ")\", \n"
                                       "                     axisClass: \"ct-axis-title\",\n"
                                       "                     offset :{ x:0, y:0},\n"
                                       "                     flipTitle: false\n"
                                       "                  }\n"
                                       "               }\n"
                                       "          )\n"
                                       //"         ,\n"
                                       //"          Chartist.plugins.legend()\n"
                                       "         ]\n"
                                       "      }\n"
                                       ");\n"
                                       "</script>\n"
                                       "<a href=\"/data.json\" download=\"" STANAME ".json\">Raw data in .json format</a>\n"
                                       "</body>\n"
                                       "</html>\n";

float maxTemp = 0;
float deltaTemp = 0;
void handleRoot() 
{

  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  
  

  std::unique_ptr<char[]> temp(new char[RESP_BUFFER_LEN]);


  
  struct tm *localTime = pftime::localtime(nullptr);
    snprintf(temp.get(), RESP_BUFFER_LEN, rootPage,
           hr, min % 60, sec % 60, 
           localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday,
           localTime->tm_hour, localTime->tm_min, localTime->tm_sec,
           TEMP(tempSensor.readCJTemperature()), TEMP(tempSensor.readThermocoupleTemperature()), deltaTemp, maxTemp
           );
  server.sendHeader("Refresh", "30;url=/");
  server.sendHeader("Cache-Control", "no-cache", true);
  server.send(200, "text/html", temp.get());

}

void handleNotFound() {
  if (handleFileRead(server.uri()))
  {
    return;
  }

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);

}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".json")) return "application/json";
  return "text/plain";
}

bool handleFileRead(String path)
{
  if (SPIFFS.exists(path))
  {
    String contentType = getContentType(path);
    File file = SPIFFS.open(path, "r");
    server.sendHeader("Cache-Control", "max-age=7200", true);
    size_t bytes = server.streamFile(file, contentType, HTTP_GET);
    file.close();
    return true;
  }

  return false;
}



void setup(void) {
  pinMode(xDRD, OUTPUT);
  pinMode(xFLT, OUTPUT);
  Serial.begin(115200);

  display7Seg.setBrightness(4, true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  SPIFFS.begin();

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //ArduinoOTA.setPort(8266);
  //ArduinoOTA.setPassword("admin");
  ArduinoOTA.setHostname(STANAME);
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
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


  if (tempSensor.begin())
  {
    Serial.println("temp sensor started");
    tempSensor.setThermocoupleType(THERMOCOUPLE_TYPE);
    if (tempSensor.getThermocoupleType() != THERMOCOUPLE_TYPE)
    {
      Serial.println("SPI connection to MAX31856 is not working.  No point continuing.");
      while (1);
    }

    tempSensor.setNoiseFilter(MAX31856_NOISE_FILTER_60HZ);
    sampleData();

  }
  else
  {
    Serial.println("temp sensor did not start");
  }


  if (MDNS.begin(STANAME)) {
    Serial.println("MDNS responder started");
  }

  pftime::configTime(TIME_ZONE, NTP_SERVER);

  server.on("/", handleRoot);
  server.on("/data.json", handleDataJson);
  server.on("/data.js", handleDataJs);
  server.on("/resetMax", handleResetMax);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}



void sampleData()
{
  int last = current;
  current = (current + 1) % BUF_LEN;
  time_t t = pftime::time(nullptr);
  buffer[current].time = t;
  buffer[current].temp = TEMP(tempSensor.readThermocoupleTemperature());
  // Delta is the difference expressed as a rate.  The sample period is in ms, 
  // and the desired output is in degrees per minute:
  // delta (deg/samplerate(ms)) * 1000 ms/s *60 s/min = deg/min
  deltaTemp = (buffer[current].temp - buffer[last].temp)/((float)SAMPLE_PERIOD) * 60000.0 ;
  
  if(buffer[current].temp > maxTemp)
  {
    maxTemp = buffer[current].temp;
  }
  Serial.print("Read into buffer position: ");
  Serial.print(current);
  Serial.print(": ");
  Serial.println(buffer[current].temp);

    uint8_t fault = tempSensor.readFault();

    if (fault)
    {
      if (fault & MAX31856_FAULT_CJRANGE) Serial.println("Cold Junction Range Fault");
      if (fault & MAX31856_FAULT_TCRANGE) Serial.println("Thermocouple Range Fault");
      if (fault & MAX31856_FAULT_CJHIGH)  Serial.println("Cold Junction High Fault");
      if (fault & MAX31856_FAULT_CJLOW)   Serial.println("Cold Junction Low Fault");
      if (fault & MAX31856_FAULT_TCHIGH)  Serial.println("Thermocouple High Fault");
      if (fault & MAX31856_FAULT_TCLOW)   Serial.println("Thermocouple Low Fault");
      if (fault & MAX31856_FAULT_OVUV)    Serial.println("Over/Under Voltage Fault");
      if (fault & MAX31856_FAULT_OPEN)    Serial.println("Thermocouple Open Fault");
    }

  display7Seg.showNumberDec((int)(buffer[current].temp));
}


void Update7Seg()
{
  float tempValue = TEMP(tempSensor.readThermocoupleTemperature());
  uint8_t fault = tempSensor.readFault();

  if (fault)
  {
      if (fault & MAX31856_FAULT_CJRANGE) 
      { 
        Serial.println("Cold Junction Range Fault");
        
      }
      
      if (fault & MAX31856_FAULT_TCRANGE) 
      {
        Serial.println("Thermocouple Range Fault");
      }
      
      if (fault & MAX31856_FAULT_CJHIGH)  
      {
        Serial.println("Cold Junction High Fault");
      }
      
      if (fault & MAX31856_FAULT_CJLOW)  
      {
        Serial.println("Cold Junction Low Fault");
      }
      
      if (fault & MAX31856_FAULT_TCHIGH)  
      {
        Serial.println("Thermocouple High Fault");
      }
      if (fault & MAX31856_FAULT_TCLOW) 
      {
        Serial.println("Thermocouple Low Fault");
      }
      
      if (fault & MAX31856_FAULT_OVUV)    
      {
        Serial.println("Over/Under Voltage Fault");
      }
      
      if (fault & MAX31856_FAULT_OPEN)  
      {
        Serial.println("Thermocouple Open Fault");
      }

      display7Seg.showNumberHexEx((uint16_t)(fault),0xff,true);
  }
  else
  {
    display7Seg.showNumberDec((int)(tempValue));
  }
}


void handleResetMax()
{
  maxTemp = 0;
  
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}
void handleDataJson()
{
  handleData(false);
}

void handleDataJs()
{
  handleData(true);
}

void handleData(bool isJs)
{

  String responseBuffer;
  responseBuffer.reserve(RESP_BUFFER_LEN);

#define TEMPLEN 64
  char temp[TEMPLEN] = {0};
  snprintf(temp, TEMPLEN, "%s{ series: [ [", isJs ? "var data=" : "");
  responseBuffer = temp;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-cache", true);
  server.send(200, isJs ? "application/js" : "application/json", responseBuffer.c_str());
  
  responseBuffer.clear();


  int startFrom = current + 1;



  int points = 0;
  
  for (int i = 0 ; i < BUF_LEN; ++i)
  {
    int pos = (i + startFrom) % BUF_LEN;
    if(buffer[pos].time > 1000000)
    {
      ++points;
      snprintf(temp, TEMPLEN, "\n{x: %lu000, y: %6.2f},",
               buffer[pos].time, 
               buffer[pos].temp);
      responseBuffer += temp;
    }

    if(responseBuffer.length() > RESP_BUFFER_HIGH_WATER)
    {
      server.sendContent(responseBuffer.c_str());
      responseBuffer.clear();
    }
  }

  snprintf(temp, TEMPLEN, "\n]\n]\n}%s", isJs ? ";" : "");
  responseBuffer += temp;

   server.sendContent(responseBuffer.c_str());
   responseBuffer.clear();


  Serial.print("startFrom: ");
  Serial.print(startFrom);

  Serial.print(", points: ");
  Serial.println(points);
  
}

void loop(void)
{
  static unsigned long lastSample = 0;
  static unsigned long last7SegUpdate = 0;
  MDNS.update();
  server.handleClient();

  unsigned long now = millis();
  if (now - lastSample > SAMPLE_PERIOD)
  {
    lastSample = now;
    sampleData();
  }

  if (now - last7SegUpdate > UPDATE_LED7SEG)
  {
    last7SegUpdate = now;
    Update7Seg();
  }
  ArduinoOTA.handle();
  ESP.wdtFeed();
}
