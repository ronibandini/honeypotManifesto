/*
Honeypot Manifesto 
Roni Bandini, June 2024, @RoniBandini
MIT License
Code based on Dale Giancono Captive Portal
Admin URL http://192.168.4.1/admin
Display letters A, B, C, D, E, F, H, L, O, P, U and dash-
*/


#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <ESPStringTemplate.h>
#include <ESPFlash.h>
#include <ESPFlashCounter.h>
#include <ESPFlashString.h>
#include "DFRobot_LedDisplayModule.h"

DFRobot_LedDisplayModule LED(&Wire, 0x48);

#define DEFAULT_SSID                  "StarbucksWIFI"
#define HTTP_USER_AUTH                "admin"
#define HTTP_PASS_AUTH                "123456"
#define SSID_FILEPATH                 "/ssid.txt"
#define CONNECTION_COUNTER_FILEPATH   "/connectionCounter"

#define AUDIO_DIRECTORY              "/mp3"
#define WEB_PAGE_FILEPATH             "/webpage.html"


static const char _PAGEHEADER[]         PROGMEM = "<!DOCTYPE html><html><head><style>body{background-color: #282828;font-family: Courier New;color:#00FF66;}</style></head><body>";
static const char _TITLE[]              PROGMEM = "<hr><h1>%TITLE%</h1><hr>";
static const char _SUBTITLE[]           PROGMEM = "<hr><h2>%SUBTITLE%</h2><hr>";

static const char _EMBEDAUDIO[]         PROGMEM = "<audio controls autoplay loop src=\"%AUDIOURI%\"/>Su navegador no soporta audios.</audio>";

static const char _EMBEDTEXT[]         PROGMEM = "<br><i>Buenos Aires, Argentina, Junio de 2024</i>";

static const char _RESETCOUNTER[]        PROGMEM = "<form action=\"/reset\"> <input type=\"submit\" value=\"reset\" name=reset></form>";
static const char _DELETEAUDIO[]        PROGMEM = "<form action=\"/delete\"> \"%AUDIOURI%\" <input type=\"submit\" value=\"delete\" name=\"%AUDIOURI%\"></form>";
static const char _ADDAUDIO[]           PROGMEM = "<form action=\"/admin\" method=\"post\" enctype=\"multipart/form-data\">MP3 to Upload:<input type=\"file\" value=\"uploadFile\" name=\"uploadFile\" accept=\"mp3/*\"><input type=\"submit\" value=\"Upload mp3\" name=\"submit\"></form>";
static const char _SSIDEDIT[]           PROGMEM = "<form action=\"/ssidedit\">Nuevo SSID: <input type=\"text\" value=\"\" name=\"SSID\"><input type=\"submit\" name=\"submit\"></form>";
static const char _FILEUPLOADMESSAGE[]  PROGMEM = "Subida terminada. click <a href=\"/admin\">aca</a> para seguir...";
static const char _BREAK[]              PROGMEM = "<br>";
static const char _PAGEFOOTER[]         PROGMEM = "</body></html>";

void handleCaptiveImagePortal(AsyncWebServerRequest *request);
void handleUploadPage(AsyncWebServerRequest *request);
void handleDelete(AsyncWebServerRequest *request);
void handleReset(AsyncWebServerRequest *request);
void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
void handleSsidEdit(AsyncWebServerRequest *request);

/* Create DNS server instance to enable captive portal. */
DNSServer dnsServer;
/* Create webserver instance for serving the StringTemplate example. */
AsyncWebServer server(80);
/*ESPFlash instance used for uploading files. */
ESPFlash<uint8_t> fileUploadFlash;
/*ESPFlash instance used for storing ssid. */
ESPFlashString ssid(SSID_FILEPATH, DEFAULT_SSID);
/*ESPFlash instance used for counting connections. */
ESPFlashCounter connectionCounter(CONNECTION_COUNTER_FILEPATH);
/*Buffer for webpage */
char buffer[3000];


/* Soft AP network parameters */
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);
Dir audioDirectory;
int pinButton=D3;
int serverStatus=0;

void setup()
{
  pinMode(pinButton, INPUT_PULLUP);

  SPIFFS.begin();
  pinMode(LED_BUILTIN, OUTPUT);

   while(LED.begin(LED.e4Bit) != 0)
  {
    Serial.println("Problema de conexion con el display :(");
    delay(1000);
  }

 
  LED.setDisplayArea(1,2,3,4);
  LED.print(".",".",".",".");
  
  /* Open the SPIFFS root directory and serve each file with a uri of the filename */
  audioDirectory = SPIFFS.openDir(AUDIO_DIRECTORY);
  while (audioDirectory.next())
  {
    server.serveStatic(
      audioDirectory.fileName().c_str(),
      SPIFFS,
      audioDirectory.fileName().c_str(),
      "no-cache");
  }
  
  /* Configure access point with static IP address */
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(ssid.get());

  /* Start DNS server for captive portal. Route all requests to the ESP IP address. */
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);

  server.on("/admin", HTTP_GET, handleUploadPage);
  server.on("/admin", HTTP_POST, handleUploadPage, handleFileUpload);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/ssidedit", HTTP_GET, handleSsidEdit);

  /* Define the handler for when the server receives a GET request for the root uri. */
  server.onNotFound(handleCaptiveImagePortal);

  /* Set the LED low (it is inverted) */
  digitalWrite(LED_BUILTIN, true);
  /* Begin the web server */
  //server.begin();
  delay(1000);
}

void loop()
{

  if (digitalRead(pinButton)==LOW and serverStatus==0){
    // button pressed
    server.begin();
    serverStatus=1;
    LED.print("U","P",".",".");
    delay(2000);
  }

  if (digitalRead(pinButton)==LOW and serverStatus==1){
    // button pressed
    server.end();
    serverStatus=0;
    LED.print("-","-","-","-");
    delay(2000);
  }

  static int previousNumberOfStations = -1;
  
  dnsServer.processNextRequest();
  
  int numberOfStations = WiFi.softAPgetStationNum();
  if(numberOfStations != previousNumberOfStations)
  {
    if(numberOfStations < previousNumberOfStations)
    {
      digitalWrite(LED_BUILTIN, true);
    }
    else
    {
      digitalWrite(LED_BUILTIN, false);
      connectionCounter.increment();
      LED.print(connectionCounter.get());

    }

    previousNumberOfStations = numberOfStations;
  }

  
}

void handleCaptiveImagePortal(AsyncWebServerRequest *request)
{  
  String filename;
  ESPStringTemplate pageTemplate(buffer, sizeof(buffer));
  /* This handler is supposed to get the next file in the SPIFFS root directory and
    server it as an embedded image on a HTML page. It does that by getting the filename
    from the next file in the rootDirectory Dir instance, and setting it as the IMAGEURI
    token in the _EMBEDIMAGE HTML element.*/

  /* Get next file in the root directory. */
  if(!audioDirectory.next())
  {
    audioDirectory.rewind();
    audioDirectory.next();
  }

  filename = audioDirectory.fileName();  
  pageTemplate.add_P(_PAGEHEADER);
  pageTemplate.add_P(_TITLE, "%TITLE%", "Contracultura Maker");
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Haga click para escuchar el manifiesto");
  if(filename != "")
  {    
    pageTemplate.add_P(_EMBEDAUDIO, "%AUDIOURI%", filename.c_str());
  }
  else
  {
    pageTemplate.add_P(PSTR("<h1>NO AUDIOS UPLOADED</h1>"));
  }

  pageTemplate.add_P(_EMBEDTEXT);
  pageTemplate.add_P(_PAGEFOOTER);
  request->send(200, "text/html", buffer);
}

void handleUploadPage(AsyncWebServerRequest *request)
{
  if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
  {
    return request->requestAuthentication();
  }
  String filename;
  ESPStringTemplate pageTemplate(buffer, sizeof(buffer));
  pageTemplate.add_P(_PAGEHEADER);
  pageTemplate.add_P(_TITLE, "%TITLE%", "Honeypot Manifesto Admin");
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Visitas");
  pageTemplate.add(String(connectionCounter.get()).c_str());
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Borrar mp3");

  audioDirectory.rewind();
  while (audioDirectory.next())
  {
    pageTemplate.add_P(_DELETEAUDIO, "%AUDIOURI%", audioDirectory.fileName().c_str());
  }
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Resetear contador");
  pageTemplate.add_P(_RESETCOUNTER);
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Agregar Audio");
  pageTemplate.add_P(_ADDAUDIO);
  pageTemplate.add_P(_SUBTITLE, "%SUBTITLE%", "Cambiar SSID");
  pageTemplate.add_P(_SSIDEDIT);
  pageTemplate.add_P(_PAGEFOOTER);

  request->send(200, "text/html", buffer);
}

void handleDelete(AsyncWebServerRequest *request)
{
  if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
  {
    return request->requestAuthentication();
  }
  for (int i = 0; i < request->params(); i++)
  {
    AsyncWebParameter* p = request->getParam(i);
    if((p->name().c_str() != ""))
    {
      SPIFFS.remove(p->name().c_str());
    }
  }
  request->redirect("/admin");
}

void handleReset(AsyncWebServerRequest *request)
{
  if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
  {
    return request->requestAuthentication();
  }

  while (connectionCounter.get()>0){
   connectionCounter.decrement();
  }
  
  LED.print(connectionCounter.get());
  request->redirect("/admin");
}

void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
{

  if (!index)
  {
    if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
    {
      return request->requestAuthentication();
    }
    String rootFileName;
    rootFileName = "/mp3/" + filename;
    fileUploadFlash.setFileName(rootFileName.c_str());
  }

  fileUploadFlash.appendElements(data, len);
  
  if (final)
  {
    server.serveStatic(
      fileUploadFlash.getFileName(),
      SPIFFS,
      fileUploadFlash.getFileName(),
      "no-cache");
  }
}

void handleSsidEdit(AsyncWebServerRequest *request)
{
  if(!request->authenticate(HTTP_USER_AUTH, HTTP_PASS_AUTH))
  {
    return request->requestAuthentication();
  }

  AsyncWebParameter* p = request->getParam("SSID");
  if(p->value() != "")
  {
    ssid.set(p->value());
    WiFi.softAPdisconnect(true);
    WiFi.softAP(p->value());  
  }
  else
  {
    request->redirect("/admin");
  }
}
