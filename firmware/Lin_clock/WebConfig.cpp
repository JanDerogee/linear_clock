#include <Arduino.h>      /*important to avoid all sorts of strange error messages*/
#include <ArduinoJson.h>  /*this can be installed using the arduino library manager, it is in the list, but not installed by default*/
#include <FS.h>
#include "WebConfig.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

/*-------------------------------------------------------------------------*/

ESP8266WebServer server(80);

/*--------------------------------------------------------------*/
config_structTYPE cfg;  /*structure holding all the settings and variables that should be available to all callers who includes this .h file*/
/*--------------------------------------------------------------*/

/*----------------------------------------------------------------*/
bool Config_load(void);                   /*load settings from JSON configuration file*/
bool Config_save(void);                   /*save settings to JSON configuration file*/
void Webserver_init(void);                /*init webserver routines*/

void settings_send(unsigned char var);

void returnOK(void);
void returnFail(String msg);
String getContentType(String filename);
bool handleFileRead(String path);
void handleNotFound(void);
void redirect_to_mainmenu(void);

/*================================================================/*

/*do SPIFFS.begin() before calling WebConfig_init();*/
/*this routine will open the SPIFFS, read the JSON configuration file and connect to the specified network*/
/*if that network can't be found then an acces point is created, so that the user can connect using his/her phone/tablet*/
/*and fill in the correct settings, if these settings are correct, then this function will exit and normal code can*/
/*continue, therefore no wifimanager or hardcoded settings are required. Everything is conveniently stored in the JSON file*/
/*these files are to be located along with the webserver files in the /data folder of the sketch folder*/
void WebConfig_init(void)
{
  unsigned int nmbr_of_networks=0;
  unsigned int i=0;
  bool network_ok = false;
  unsigned char timeout = 0;
  char c_ssid[32];  /*working register to port the value of the SSID to the wifi network routines (which require char and not String)*/
  char c_key[32];  /*working register to port the value of the key to the wifi network routines (which require char and not String)*/
  String prev_ssid = "";  /*variable to keep track of a changing SSID*/
  String prev_key = "";   /*variable to keep track of a changing KEY*/

  if(Config_load() == false)
  {
    ESP.restart();  /*force reset on failure*/
  } 

  while(1)  /*keep looping here until we are connected to a real network*/
  {
    prev_ssid = cfg.ssid;
    prev_key = cfg.key;
    
    Serial.println(F("scanning for networks"));   
    WiFi.disconnect();                /*only required when the Cassiopei is reset, this is not required when the cassiopei is power-on*/
    WiFi.persistent(false);           /*Do not memorise new wifi connections*/
    //ESP.flashEraseSector(0x3fe); // Erase remembered connection info. (Only do this once).
    WiFi.mode(WIFI_STA);              /*connect to an existing WiFi network (the ESP8266 will be used as a STATION)*/
    WiFi.hostname(WIFIHOSTNAME);      /*the name of this device. This name is shown in the list of connected devices in your router*/
    nmbr_of_networks = WiFi.scanNetworks(); /*WiFi.scanNetworks will return the number of networks found*/
    if (nmbr_of_networks > 0)
    {
      for (i=0; i<nmbr_of_networks; ++i)  /*show all the found networks*/
      {
        Serial.print(WiFi.SSID(i));
        Serial.print(F(" ("));
        Serial.print(WiFi.RSSI(i));
        Serial.print(F(" dB)"));
        Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
        delay(1); /*we could also yield*/
      }

      Serial.print(F("Searching for:"));       /*attempt to connect to preferred network*/
      Serial.print(cfg.ssid);
      network_ok = false;   /*unless we find something, we fail*/               
      for (i=0;i<nmbr_of_networks;++i)
      {
        delay(1); /*we could also yield*/
        if(cfg.ssid == WiFi.SSID(i))        /*compare network name with name from list, try them all until we find a match*/
        {
          Serial.println(F(",found"));                               /*network has been found, now try to connect*/
          cfg.ssid.toCharArray(c_ssid, (cfg.ssid.length()+1));    /*this crazy construction is required because wifi.begin cannot accept strings ?!?!?!*/
          cfg.key.toCharArray(c_key,(cfg.key.length()+1));        /*this crazy construction is required because wifi.begin cannot accept strings ?!?!?!*/       
          Serial.println(F("Connecting..."));
          WiFi.begin(c_ssid, c_key);        

          network_ok = true;  /*we are almost there, unless we timeout (wifi problem or password incorrect), we are ok*/          
          timeout = 30;       /*the timeout in seconds, if we do not succeed with the timeout limits we must accept failure and carry on*/
          while(WiFi.status() != WL_CONNECTED)
          {
            delay(1000);
            Serial.print(F("+"));
            if(timeout-- == 0)
            {
              Serial.println(F("Failed to connect, check key value"));
              cfg.status_msg = "Key is rejected";      /*update the status message*/
              network_ok = false;            
              break;
            }
          }
        }
      }

      Serial.println(F(""));   
      if(network_ok == true)
      {
        Serial.print(F("Network: "));    /*everything went fine, we are basically done here*/
        Serial.print(WiFi.SSID());
        Serial.print(F(", IP: "));
        Serial.println(WiFi.localIP());
        Serial.print(F("You may also go to: "));
        Serial.print(WIFIHOSTNAME);
        Serial.println(F("/"));
        Webserver_init();                             /*initialize webserver*/    
        return; /*exit this routine, return to "main"*/
      }
      else
      {         
        IPAddress Ip(192, 168, 1, 1);
        IPAddress NMask(255, 255, 255, 0);
        WiFi.softAPConfig(Ip, Ip, NMask);
        WiFi.mode(WIFI_AP_STA);   /*could not connect to network, so we set up our own network as a means to do allows for configuration*/
        Serial.println(WiFi.softAPIP());          
        Webserver_init();         /*initialize webserver*/            
        while(1)
        {
          Webserver_process();    /*keep serving the page as it is the only thing it can do, hoping that the user will modify the settings here so we can continue*/            
          if((prev_ssid != cfg.ssid) || (prev_key != cfg.key)) /*check if SSID and/or KEY settings have changed, if so then repeat the cycle, perhaps settings are correct now*/
          {
            Serial.println(F("Settings have changed, try again"));
            break;  /*try again but now with the new settings*/
          }
        }
      }
    }
  }
}

/*load settings from JSON configuration file*/
bool Config_load(void)
{ 
  String str=""; /*a tempstorage space for converion purposes*/
  
  File configFile = SPIFFS.open(CONFIGFILENAME, "r"); 
  if (!configFile)
  {
    Serial.println(F("Failed to open config file, attempting to create file"));
    Config_save();
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024)
  {
    Serial.println(F("Config file size is too large"));
    return false;
  }

  std::unique_ptr<char[]> buf(new char[size]);  /*Allocate a buffer to store contents of the file.*/
  configFile.readBytes(buf.get(), size);        /*We don't use String here because ArduinoJson library requires the input buffer to be mutable.*/

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success())
  {
    Serial.println(F("Failed to parse config file"));
    return false;
  }

  Serial.println(F("Loading settings"));
  /*copy all values from the JSON file into the proper variables*/
  cfg.ssid = json["ssid"].asString();   /*the .asString() is required to convert the type of the json library (which is const char*) to a regular String type*/
  cfg.key = json["key"].asString();     /*the .asString() is required to convert the type of the json library (which is const char*) to a regular String type*/
  cfg.ntp = json["ntp"].asString();     /*the .asString() is required to convert the type of the json library (which is const char*) to a regular String type*/
  str = json["offset"].asString();      /*the .asString() is required to convert the type of the json library (which is const char*) to a regular String type*/
  cfg.offset = str.toFloat();           /*convert the String to a real value we can store in an float*/
  if(json["dst"] == true)    {cfg.dst = true;}     else {cfg.dst = false;}
  if(json["alarm"] == true)  {cfg.alarm = true;}   else {cfg.alarm = false;}
  if(json["chime"] == true)  {cfg.chime = true;}   else {cfg.chime = false;}
 
//  Serial.println(cfg.ssid);
//  Serial.println(cfg.key);
//  Serial.println(cfg.ntp);
//  Serial.println(cfg.offset);
//  Serial.println(cfg.dst);    Serial.print(F(" stringvalue "));  str = json["dst"].asString();   Serial.println(str);
//  Serial.println(cfg.alarm);  Serial.print(F(" stringvalue "));  str = json["alarm"].asString(); Serial.println(str);  
//  Serial.println(cfg.chime);  Serial.print(F(" stringvalue "));  str = json["chime"].asString(); Serial.println(str);

  return true;
}

/*save settings to JSON configuration file*/
bool Config_save(void)
{
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
 
  json["ssid"] = cfg.ssid;
  json["key"] = cfg.key;
  json["ntp"] = cfg.ntp;
  json["offset"] = (float) cfg.offset;
  json["dst"] = (bool) cfg.dst;
  json["alarm"] = (bool) cfg.alarm;
  json["chime"] = (bool) cfg.chime;
  File configFile = SPIFFS.open(CONFIGFILENAME, "w");
  if (!configFile)
  {
    Serial.println(F("Failed to open config file for writing"));
    return false;
  }

  json.printTo(configFile);
  Serial.println(F("Data has been written to file"));
  return true;
}

/*----------------------------------------------------------------*/

/*when not specified, the browser may have send a submit or wants to download a file from the spiffs*/
void handleNotFound(void)
{
//  const char* temp[32];
  /*this routine will process the values that are send by the connected browswer when a user presses a submitbutton on the form*/
  if (server.args() > 0 )
  {
    Serial.println(F("Handling submit:"));    
    for ( uint8_t i = 0; i < server.args(); i++ )
    {
      Serial.print(server.argName(i));
      Serial.print(F("="));
      Serial.println(server.arg(i));      

      /*copy the received arguments into the corresponding variables*/
      if (server.argName(i) == "ssid")        {cfg.ssid=server.arg(i);}
      else if (server.argName(i) == "key")    {cfg.key=server.arg(i);}
      else if (server.argName(i) == "ntp")    {cfg.ntp=server.arg(i);}
      else if (server.argName(i) == "offset") {cfg.offset=server.arg(i).toFloat();}
      else if (server.argName(i) == "dst")    {if(server.arg(i)=="on") {cfg.dst=true;} else {cfg.dst=false;}}
      else if (server.argName(i) == "alarm")  {if(server.arg(i)=="on") {cfg.alarm=true;} else {cfg.alarm=false;}}
      else if (server.argName(i) == "chime")  {if(server.arg(i)=="on") {cfg.chime=true;} else {cfg.chime=false;}}
    }

    Serial.println(F("The data after processing:"));
    Serial.print(F("cfg.ssid  ="));  Serial.println(cfg.ssid);
    Serial.print(F("cfg.key   ="));  Serial.println(cfg.key);
    Serial.print(F("cfg.ntp   ="));  Serial.println(cfg.ntp);
    Serial.print(F("cfg.offset="));  Serial.println(cfg.offset);
    Serial.print(F("cfg.dst   ="));  Serial.println(cfg.dst);
    Serial.print(F("cfg.alarm ="));  Serial.println(cfg.alarm);
    Serial.print(F("cfg.chime ="));  Serial.println(cfg.chime); 
    
    Config_save();  /*save these new values to the JSON file*/
    redirect_to_mainmenu();
  }
  else if(!handleFileRead(server.uri())) /*check if the file is in the filesystem, if not then respond with the error message below*/
  {
    Serial.println(F("handling file request"));        
    String message = "Request file not found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET)?"GET":"POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i=0; i<server.args(); i++)
    {
      message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
    Serial.print(message);
  }
}


bool handleFileRead(String path)
{
  //Serial.println(F("handleFileRead: " + path));
  if(path.endsWith("/"))
  {
    path += "index.htm";
  }
  
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  {
    if(SPIFFS.exists(pathWithGz))
    {
      path += ".gz";
    }
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

/*A simple redirecting HTML page, required to force the user to go to the correct URL*/
/*This makes it possible for the user to enter only the IP-address in the browser to go to the main menu*/
void redirect_to_mainmenu(void)
{
  String message = page_redirect2mainmenu;
  server.send(200, "text/html", message);  
}

void returnOK(void)
{
  server.send(200, "text/plain", "");
}

void returnFail(String msg)
{
  server.send(500, "text/plain", msg + "\r\n");
}

String getContentType(String filename)
{
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

/*initialize the webserver*/
void Webserver_init(void)
{
  Serial.println(F("Initializing webserver"));
  server.on("/", HTTP_GET, redirect_to_mainmenu);
  server.on("/btn_MAINMENU", HTTP_GET, redirect_to_mainmenu);   /*used by filemanager only*/
  server.on("/status_message.txt", []() {server.send(200, "text/plain", cfg.status_msg);});   

//  server.on("/btn_dosomething", []() {message= "Timezone="; message+=var_timezone; server.send(200, "text/plain", message);});                                     

  server.onNotFound(handleNotFound); /*when none of the above then check the filesystem to see if it is there*/

  server.begin();
  Serial.println(F("HTTP server started"));  
}



/*just process all incoming/outgoing webserver related actions*/
void Webserver_process(void)
{
  Serial.print(F("Free=")); /*show available RAM*/
  Serial.println(ESP.getFreeHeap()); /*show available RAM*/            
  server.handleClient();  /*Handle incoming connections*/
}

