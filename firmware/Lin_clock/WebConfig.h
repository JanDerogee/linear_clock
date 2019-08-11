#ifndef _WEB_H_
#define _WEB_H_


#define CONFIGFILENAME  "/config.json"    /*this is the name of the configuration file where all settings are stored*/
#define WIFIHOSTNAME    "linear-clock"    /*the name of this device. This name is shown in the list of connected devices in your router*/

void WebConfig_init(void);                /*do SPIFFS.begin() before calling WebConfig_init(); This routine will allow for configuration of ALL settings even the SSID and KEY values of the home network*/
void Webserver_process(void);             /*handle the webserver*/


/*a simple struct to hold all settings*/
typedef struct
{
  String status_msg = "n.a.";       /*this is not a setting but a holder for the status message*/
  String ssid = "enter_SSID_here";  /*default value should be entered here (must be const char* otherwise it will not work with json related code)*/
  String key = "enter_KEY_here";    /*default value should be entered here (must be const char* otherwise it will not work with json related code)*/
  String ntp = "time.nist.gov";     /*default value should be entered here (must be const char* otherwise it will not work with json related code)*/
  float offset = 0;                 /*default value should be entered here*/
  bool dst = false;                 /*default value should be entered here*/
  bool alarm = true;                /*default value should be entered here*/
  bool chime = true;                /*default value should be entered here*/
} config_structTYPE;

extern config_structTYPE cfg;  /*structure holding all the settings that should be available to all callers who includes this .h file*/


/*=========================================================================================================
The HTML-Content is enclosed by R"=====( .... HTML ...  )=====".
This is nice technique to put huge amount of static data into a variable where
the beauty of it is that you don't have to escape any quotation marks or line feeds.
this technique is also used by: http://www.john-lassen.de/index.php/projects/esp-8266-arduino-ide-webconfig
==========================================================================================================*/

/*The HTML pages below are hardcoded and not on the SD-card because these files must be present all the time, therefore if*/
/*the SD-card has problems in its filestructure it should still be possible to upload new firmware trough the webbrowser*/

/*this piece of HTML code will redirect the current page to the current page with the addition of /edit*/
const char page_redirect2mainmenu[] PROGMEM = R"=====(
<html>
  <head>
    <meta http-equiv="refresh" content="0;URL=../index.htm"/>
  </head>
</html>
)=====";


#endif
