
#include "Adafruit_MAX31856.h"
//-----------------------------------------
//LOCAL SETTINGS - update in this block for your needs

#define STASSID "WIFI SSID"
#define STAPSK  "WIFI PASSWD"
#define STANAME "HOSTNAME"
#define TIME_ZONE "MDT-6"
#define NTP_SERVER "0.ca.pool.ntp.org"
#define THERMOCOUPLE_TYPE MAX31856_TCTYPE_J

//Pin mappings max31865 breakout pin = ESP8266 pin
const uint8_t xCS = D3;
const uint8_t xSDI = D2;
const uint8_t xSDO = D1;
const uint8_t xSCK = D0;
const uint8_t xFLT = D5;
const uint8_t xDRD = D6;

//END LOCAL SETTINGS
//-----------------------------------------


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


struct sample_t
{
  time_t time;
  float outsideTemp;
  float insideTemp;
};
#define BUF_LEN 20
volatile int current = 0;
sample_t buffer[BUF_LEN] = {0};
#define SAMPLE_PERIOD 10000

#define RESP_BUFFER_LEN 24000

ESP8266WebServer server(80);

Adafruit_MAX31856 tempSensor(xCS,xSDI,xSDO,xSCK);

void handleRoot() {
  
  char temp[RESP_BUFFER_LEN];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  
  struct tm *localTime = pftime::localtime(nullptr);
  
  snprintf(temp, RESP_BUFFER_LEN,
            "<html>\n"
              "<head>\n"
                "<meta http-equiv=\"refresh\" content=\"20\">\n"
                "<title>Internet Of Kilns - " STANAME "</title>\n"
                "<link rel=\"stylesheet\" href=\"/chartist.min.css\">\n"
                "<link rel=\"shortcut icon\" href=\"/favicon.ico\">\n"
                "<style>\n"
                  "body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\n"
                "</style>\n"
              "</head>\n"
              "<body>\n"
                "<H1>" STANAME "</h1>\n"
                "<p>Uptime: %02d:%02d:%02d</p>\n"
                "<p>Date and time: %04d-%02d-%02d %02d:%02d:%02d</p>\n"
                "<p>Outside Temp:%f degrees C</p>\n"
                "<p>Measured Temp:%f degrees C</p>\n"
                "<div class=\"ct-chart ct-perfect-fourth\"></div>\n"
                "<script src=\"/chartist.min.js\"></script>\n"
                "<script src=\"/data.js\"/></script>\n"
                "<script>\n"
                  "new Chartist.Line('.ct-chart', data);\n"
                "</script>\n"
                "<a href=\"/data.json\">Raw data in .json format</a>\n"
              "</body>\n"
            "</html>\n",
           hr, min % 60, sec % 60,
           localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday,
           localTime->tm_hour, localTime->tm_min, localTime->tm_sec,
           buffer[current].outsideTemp,buffer[current].insideTemp);

  server.sendHeader("Cache-Control", "no-cache", true);
  server.send(200, "text/html", temp);
  
}

void handleNotFound() {
  if(handleFileRead(server.uri()))
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
  if(SPIFFS.exists(path))
  {
    String contentType = getContentType(path);
    File file = SPIFFS.open(path, "r");
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


  if(tempSensor.begin())
  {
    Serial.println("temp sensor started");
    tempSensor.setThermocoupleType(THERMOCOUPLE_TYPE);
    if(tempSensor.getThermocoupleType() != THERMOCOUPLE_TYPE)
    {
      Serial.println("SPI connection to MAX31856 is not working.  No point continuing.");
      while(1);
    }

    tempSensor.setTempFaultThreshholds(-50.0, 2500.0);
    tempSensor.setColdJunctionFaultThreshholds(-10,40);
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
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}



void sampleData()
{
  current = (current +1) % BUF_LEN;
  buffer[current].time = pftime::time(nullptr);
  buffer[current].outsideTemp =tempSensor.readCJTemperature();
  buffer[current].insideTemp = tempSensor.readThermocoupleTemperature();
  
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

  
  Serial.print(buffer[current].outsideTemp);
  Serial.print(",");
  Serial.println(buffer[current].insideTemp);
  


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

  time_t now = pftime::time(nullptr);
  char temp[RESP_BUFFER_LEN];
  int pos = 0;
  pos = snprintf(temp, RESP_BUFFER_LEN-pos, "%s{labels: [", isJs?"var data=":"");
    
  int startFrom = current + 1;

  for(int i = 0 ; i < BUF_LEN; ++i)
  {

    if(buffer[(i+startFrom)% BUF_LEN].time == 0)
    {
      pos += snprintf(&temp[pos], RESP_BUFFER_LEN-pos, "\"\",");
    }
    else
    {
      struct tm *tm = localtime(&buffer[(i+startFrom)% BUF_LEN].time);
        pos += snprintf(&temp[pos], RESP_BUFFER_LEN-pos, "\"%02d:%02d:%02d\",", 
        tm->tm_hour, tm->tm_min, tm->tm_sec );
    }
  }
  pos += snprintf(&temp[pos], RESP_BUFFER_LEN-pos, "], series: [[");

  for(int i = 0 ; i < BUF_LEN; ++i)
  {
    pos += snprintf(&temp[pos], RESP_BUFFER_LEN-pos, "%f,", buffer[(i+startFrom) % BUF_LEN].outsideTemp);
  }

  pos += snprintf(&temp[pos], RESP_BUFFER_LEN-pos, "],[");
  for(int i = 0 ; i < BUF_LEN; ++i)
  {
    pos += snprintf(&temp[pos], RESP_BUFFER_LEN-pos, "%f,", buffer[(i+startFrom) % BUF_LEN].insideTemp);
  }
  

  pos += snprintf(&temp[pos], RESP_BUFFER_LEN-pos, "]]}%s", isJs?";":"");



  server.sendHeader("Cache-Control", "no-cache", true);
  server.send(200, isJs?"application/js":"application/json", temp);
}

void loop(void) 
{
  static unsigned long last=0;
  MDNS.update();
  server.handleClient();
  unsigned long now = millis();
  if(now - last > SAMPLE_PERIOD)
  {
    last = now;
    sampleData();
  }
 
}
