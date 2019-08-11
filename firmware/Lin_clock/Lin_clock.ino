
/*
 *note to myself:
 *===============
 *to save precious resources (RAM) don't use Serial.println("hello world");
 *but use the macro Serial.println(F("hello world")); 
 *This way the text is not copied to a RAM variable, but it is read from flash
 *this saves a lot of RAM because in a large program there will be a lot of print
 *statements! In this relatively simple program this little "trick" saved us
 *1.5kBytes that's is a very much !!!
 */

#include <ESP8266WiFi.h>

//#include <WiFiClient.h> 
//#include <WiFiClientSecure.h>

#include <Wire.h>             /*lib. required for I2C functionality*/
#include <FS.h>               /*lib. required for audio playback (which uses spiffs)*/
#include "WebConfig.h"        /*the webserver to which the user can connect to configure the Cassiopei*/
#include "NTP.h"              /*Network Time Protocol (required to get time and date from a timeserver somewhere on the web*/

#include "AudioFileSourceSPIFFS.h"  /*this sketch requires the library "ESP8266Audio-master.zip" to be installed ( https://github.com/earlephilhower/ESP8266Audio )*/
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2SNoDAC.h"  /*the cheap method of making sound*/
//#include <AudioOutputI2S.h>       /*the sophisticated way of making sound*/

/*Note to myself: if strange things happen when loading from SPIFFS, make sure that SPIFFS is still OK, by reloading it*/

/*------------------------------------------*/
/*           IO-pin definitions             */
/*------------------------------------------*/

//#define DEBUG_MODE  /*uncomment for debugging only (it disables all motor movements)*/

#define BAUDRATE          115200
#define TXD               1
#define RXD               3

#define SCL               0
#define SDA               2

#define SENSORBAR_UPDOWN  4   /*connected to the sensor bar for when the stepper is moving downwards*/
#define SENSORBAR_HOME    5   /*connected to the sensor bar for when the stepper is moving upwards*/

#define COIL_A            13  /*connected to orange wire of 28BYJ-48*/
#define COIL_B            14  /*connected to pink wire of 28BYJ-48*/
#define COIL_C            12  /*connected to yellow wire of 28BYJ-48*/
#define COIL_D            16  /*connected to blue wire of 28BYJ-48*/

#define LED               15  /*connected to speaker output*/

/*------------------------------------------*/
#define DOWN              0   /*direction definition for stepper motor*/
#define UP                1   /*direction definition for stepper motor*/

//#define STEPS_PER_REV     4096  /*the number of steps for a full rotation of the motor-shaft, we use M6, so a rev. is exactly 1 mm, the scale is 1mm/min, therefore 4096 steps=1min*/
#define STEPS_PER_REV     4076  /*the stepper isn't really 1:64 but 1:63.68395*/
/*for more 28BYJ-48 stepper info: https://grahamwideman.wikispaces.com/Motors-+28BYJ-48+Stepper+motor+notes */

#define HOME_POSITION     (11 * STEPS_PER_REV * 60) + (59 * STEPS_PER_REV) /*the number of steps away from 0:00*/
#define ALARM_THRESSHOLD  4     /*this is the halve of the width of the trigger block size in mm (or minutes)*/

/*----------------------------------------------------------------------------*/
/*the possible clock related functions*/
enum Clock_states      {CLOCK_IDLE,
                        CLOCK_HOME_TO_SENSOR_SETUP,
                        CLOCK_HOME_TO_SENSOR,
                        CLOCK_MOVE_TO_1159_SETUP,
                        CLOCK_MOVE_TO_1159,
                        CLOCK_OPERATE_SETUP,
                        CLOCK_OPERATE,
                        CLOCK_OPERATE_2,
                        CLOCK_OPERATE_3,
                        CLOCK_OPERATE_4,
                        CLOCK_ALARM_SETUP,
                        CLOCK_ALARM,
                        CLOCK_NTP_ERROR,
                        CLOCK_ERROR
                       }; 

/*----------------------------------------------------------------------------*/
unsigned long current_position = 0; /*use for stepper motor absolute position*/

/*----------------------------------------------------------------------------*/

void Clock_statemachine(void);
void MoveStepper(unsigned long steps, unsigned char dir, unsigned char spd);
void Motor_Off(void);
void Play_Chime_Melody(void);
void Play_Chime_Hour(void);
void Play_Chime_Quarter(void);
void Play_Chime_Magical(void);
void Play_Alarm(void);

/*----------------------------------------------------------------------------*/

AudioGeneratorWAV *wav;
AudioFileSourceSPIFFS *file;
AudioOutputI2SNoDAC *out;
//AudioOutputI2S *out;

/*============================================================================*/

void setup()
{ 
  //Wire.begin(SDA, SCL);       /*required for I2C functionality*/

  pinMode(SENSORBAR_UPDOWN, INPUT);
  pinMode(SENSORBAR_HOME, INPUT);
  pinMode(COIL_A, OUTPUT);      /*signal to stepper coildriver*/
  digitalWrite(COIL_A, LOW);    /*signal to stepper coildriver*/
  pinMode(COIL_B, OUTPUT);      /*signal to stepper coildriver*/
  digitalWrite(COIL_B, LOW);    /*signal to stepper coildriver*/
  pinMode(COIL_C, OUTPUT);      /*signal to stepper coildriver*/
  digitalWrite(COIL_C, LOW);    /*signal to stepper coildriver*/
  pinMode(COIL_D, OUTPUT);      /*signal to stepper coildriver*/
  digitalWrite(COIL_D, LOW);    /*signal to stepper coildriver*/
  pinMode(LED, OUTPUT);         /*indicator LED*/
  digitalWrite(LED, LOW);       /*indicator lights off*/ 

  Serial.begin(BAUDRATE);
  Serial.println();
  Serial.println(F("\r\n--== Linear clock ==--"));
  Serial.println(F("Firmware version:" __DATE__ ));
  
  #ifdef DEBUG_MODE        /*alaert the programmer we arein debugging mode*/
  Serial.println(F("ATTENTION: debugging mode, all movement disabled"));
  #endif
  
  //Serial.print(F("PGM flash size ="));
  //Serial.println(ESP.getFlashChipSize()); /*this will only give the correct results if the proper settings have been made. To do so simply select the proper board in the Arduino IDE: Tools -> board*/
  //Serial.print(F("Free heap size =")); /*show available RAM*/
  //Serial.println(ESP.getFreeHeap()); /*show available RAM*/

  Serial.println(F("Mounting FS...")); /*required for webserver, settings and sample playback*/
  if (SPIFFS.begin() == false)
  {
    Serial.println(F("Failed to mount file system"));
    ESP.restart();  /*force reset on failure*/
  } 
  
  digitalWrite(LED, HIGH);      /*indicator lights on, they will turn off when connected to a network*/   
  WebConfig_init();             /*get the configuration from the config.json file as stored in the SPIFFS filesystem, when connected to the network start the webserver*/
  Webserver_process();    
  digitalWrite(LED, LOW);       /*indicator lights off, we are connected to a network*/ 

  /*ATTENTION:, don't play samples before calling WebConfig_init() as it WILL crash the ESP (has something to do with declaring of the "out" object (don't ask me why, but it works better this way)*/
  out = new AudioOutputI2SNoDAC();  /*this initialisation is required for all audio playback code, initialze the object here because it is not good (and not needed) to do this in the playback routine*/  
  Play_Chime_Magical();             /*indicate that we've powered up and that we are connected to the specified network*/  
}


void loop()
{  
  while(1)
  {
    yield();                      /*pet the watchdog*/
    Webserver_process();          /*handle webserver and therefore stay as responsive as is practically possible*/    
    yield();                      /*pet the darn beast again... just to make sure it stays drowsy*/
    Clock_statemachine();         /*do what needs to be done to make this clock a clock*/
  }   
}

/*==================================================================*/

/*======================================================================================================================*/
/*                                        Statemachine for all Clock related functions                                  */
/*======================================================================================================================*/
void Clock_statemachine(void)
{
  static unsigned char Clock_state = CLOCK_IDLE;
  static long timeout = 0;  
  static unsigned char error_code = 0;
  static unsigned char lp = 0;
  static unsigned long new_position = 0;
  static unsigned long steps = 0;
  static unsigned char dir = UP;
  static unsigned char prev_hour = 0;
  static unsigned char prev_minute = 0;
  static unsigned char alarm_cnt = 0;
  static long value = 0;
  
  switch(Clock_state)
  {
    case CLOCK_HOME_TO_SENSOR_SETUP:
    {
      Serial.println(F("Starting homing procedure"));  /*home the runner to hit the limit-switch*/
      cfg.status_msg = "Homing indicator...";       /*update the status message*/
      timeout = 15 * STEPS_PER_REV * 60;            /*scale is 14 hours, so if we haven't found anything after a distance of 15hours, then there is a serious problem and we should abort*/
      Clock_state = CLOCK_HOME_TO_SENSOR;
      break;
    }
    
    case CLOCK_HOME_TO_SENSOR:
    {
      if(timeout > 256)
      {
        timeout = timeout - 256;
      }
      else
      {
        Serial.println(F("timeout exceeded, service required"));      
        Serial.println(F("limit sensor could not be detected"));
        Play_Alarm();     /*could not home the runner*/
        error_code = 3;
        Clock_state = CLOCK_ERROR;
        break;
      }

#ifndef DEBUG_MODE        /*for debugging purposes, it can be usefull to disable movement*/    
      MoveStepper(256, UP, 1);  /*move stepper 0.0625mm up before checking the sensor again*/    

      if(digitalRead(SENSORBAR_HOME) == false)        /*home the runner to hit the limit-switch (sensor signal goes low when home reached)*/
      {
        cfg.status_msg = "Home sensor detected";       /*update the status message*/        
        Clock_state = CLOCK_MOVE_TO_1159_SETUP;        
      }      
#else
      Clock_state = CLOCK_MOVE_TO_1159_SETUP;        
#endif      

      break;
    }

    case CLOCK_MOVE_TO_1159_SETUP:
    {
      Play_Chime_Quarter();
      Serial.println(F("home reached, moving to 11:59"));
      cfg.status_msg = "Moving to 11:59";       /*update the status message*/        
      new_position = 58*STEPS_PER_REV;  /*move the stepper to the point 11:59 on the scale*/
      Clock_state = CLOCK_MOVE_TO_1159;
      break;
    }
                       
    case CLOCK_MOVE_TO_1159:
    {          
      cfg.status_msg = "Moving indicator, ";            /*update the status message*/              
      cfg.status_msg += new_position;                   /*traveling the entire distance in one command would take too long*/
      cfg.status_msg += " steps";                       /*therefore we split up the move action in smaller steps. This requires*/
                               
      if(new_position > 256)                            /*much more code but it will have the benefit of a fully responsive web interface*/
      {
        new_position = new_position - 256;              /*decrement by the number of done steps*/
        MoveStepper(256, DOWN, 1); 
      }
      else
      {
        MoveStepper(new_position, DOWN, 1);             /*the last piece of the new_position movement*/
        current_position = HOME_POSITION;               /*the system is homed, therefore (re)set the position counter*/          
        Clock_state = CLOCK_OPERATE_SETUP;          
      }
      break;
    }

    case CLOCK_OPERATE_SETUP:
    {
      NTP_init(cfg.ntp);  /*initialize NTP functionality (in order to get time and date from a timeserver)*/
      Clock_state = CLOCK_OPERATE;
      break;
    }

    case CLOCK_OPERATE:
    {
      value = 60*60 * cfg.offset;   /*because the offset (timezone) and dst setting can be changed by the user at any time, it is best that we calculate and parse this value every time*/
      if (cfg.dst == true)          /*normally this would not be important, but in this case we want the user settings to take effect immediately*/
      {
        value = value + (60*60);    /*when DST is enabled, add another hour to the offset value*/
      }
      NTP_offset(value);            /*parse the value to the NTP routines*/
      
      NTP_statemachine();           /*get and/or update the time*/  

      cfg.status_msg = "Local time = ";      /*update the status message*/
      cfg.status_msg+= NTP_struct.year;    
      cfg.status_msg+= "-";
      cfg.status_msg+= NTP_struct.month;
      cfg.status_msg+= "-";
      cfg.status_msg+= NTP_struct.day;
      cfg.status_msg+= " ";
      cfg.status_msg+= NTP_struct.hour;
      cfg.status_msg+= ":";
      if(NTP_struct.minute < 10)  {cfg.status_msg+= "0";} /*check if we must add an additionl 0 to maintain the 2 digit representation*/
      cfg.status_msg+= NTP_struct.minute;
      cfg.status_msg+= ":";    
      if(NTP_struct.second < 10)  {cfg.status_msg+= "0";} /*check if we must add an additionl 0 to maintain the 2 digit representation*/
      cfg.status_msg+= NTP_struct.second;      
      
      if(NTP_struct.synced == true)     
      {
        cfg.status_msg+= " (synced)";
        if((NTP_struct.hour != prev_hour) || (NTP_struct.minute != prev_minute))  /*only update the clock when the time has changed*/
        {
          Clock_state = CLOCK_OPERATE_2;
        }
      }
      else                              
      {
        cfg.status_msg+= " (NTP err.)";
        Clock_state = CLOCK_NTP_ERROR;       
      }

#ifdef DEBUG_MODE        /*for debugging purposes, it can be usefull to disable movement*/    
      Serial.print(F("Status msg: "));
      Serial.println(cfg.status_msg);
      delay(1000);  /*slow down the stream of printf messages...*/
#endif      


      /*we must check the sensorbar signal as much as we can, this way we reduce the odds of a false trigger due to dirty/noisy contacts*/
      if(digitalRead(SENSORBAR_UPDOWN) == false)  
      {
        alarm_cnt = 0;  /*reset counter (event may be over or the previous trigger was false)*/
      }  
      break;
    }

    case CLOCK_OPERATE_2:
    {                            
      prev_hour = NTP_struct.hour;
      prev_minute = NTP_struct.minute;
  
      /*calculate how many steps since 0:00 we must do to reach the new time*/   
      if(NTP_struct.hour < 12)  
      {
        new_position = (NTP_struct.hour * STEPS_PER_REV * 60) + (NTP_struct.minute * STEPS_PER_REV);                        /*this is the number of steps from 0:00 (on the right side of the scale)*/
      }
      else                      
      {
        new_position = HOME_POSITION - ((STEPS_PER_REV * (NTP_struct.hour-12) * 60) + (STEPS_PER_REV * NTP_struct.minute)); /*this is the number of steps from 0:00 (on the right side of the scale)*/
      }
      
      Serial.print(F("current:"));
      Serial.print(current_position);
      Serial.print(F(", requested:"));
      Serial.println(new_position); /*move the steppermotor to the calculated position*/      

      if(new_position < current_position)
      {
        steps = current_position - new_position;
        dir = DOWN;
        Serial.print(F("down "));
        Serial.print(steps);
        Serial.println(F(" steps")); 
      }
      else
      {
        steps = new_position - current_position;
        dir = UP;        
        Serial.print(F("up "));
        Serial.print(steps);
        Serial.println(F(" steps"));    
      }
      
      Clock_state = CLOCK_OPERATE_3;
      break; 
    }

    case CLOCK_OPERATE_3:
    { 
      cfg.status_msg = "Moving indicator, ";              /*update the status message*/              
      cfg.status_msg += steps; 
      cfg.status_msg += " steps";
      Serial.println(cfg.status_msg);
      if(steps > 256)                                   /*much more code but it will have the benefit of a fully responsive web interface*/
      {
        steps = steps - 256;                            /*decrement by the number of done steps*/
        MoveStepper(256, dir, 1);
      }
      else
      {
        MoveStepper(steps, dir, 1);                     /*the last piece of the new_position movement*/
        Motor_Off();                                    /*shut down the motors to save energy*/                                            
        Clock_state = CLOCK_OPERATE_4;          
      }     
      break;
    }

    case CLOCK_OPERATE_4:
    { 
      /*chime on the whole hour and sound the number of hours*/
      if(cfg.chime == true)
      {
        if(NTP_struct.minute == 0)
        {
          cfg.status_msg = "Playing hourly chime";              /*update the status message*/                       
          Play_Chime_Melody();
          lp=20;
          while(lp>0)   /*small delay othwerwise the first hour chime will be not noticed*/
          { 
            yield();                      /*pet the watchdog*/
            Webserver_process();          /*handle webserver and therefore stay as responsive as is practically possible*/    
            delay(100);
            lp--;            
          }
          
          
          lp = NTP_struct.hour;
          if(lp > 12)    /*reduce the number of chimes to 12... technically 11*/
          {
            lp = lp - 12;
          }
          
          while(lp>0)
          { 
            yield();                      /*pet the watchdog*/
            Webserver_process();          /*handle webserver and therefore stay as responsive as is practically possible*/    
            Play_Chime_Hour();
            lp--;            
          }
        }
      
        /*chime every 15 minutes*/
        if((NTP_struct.minute == 15) || (NTP_struct.minute == 30) || (NTP_struct.minute == 45))
        {
          Play_Chime_Quarter();
        }
      }

      if(cfg.alarm == true)
      {
        if(digitalRead(SENSORBAR_UPDOWN) == true)
        {
          alarm_cnt++;    /*increment alarm counter, if high enough the thresshold will be passed and the alarm will sound*/
        }
        else
        {
          alarm_cnt = 0;  /*reset counter (event may be over or the previous trigger was false)*/
        }
        
        if(alarm_cnt == ALARM_THRESSHOLD)
        {            
          if(((NTP_struct.minute >= 57) || (NTP_struct.minute <= 3)) && ((NTP_struct.hour == 6) || (NTP_struct.hour == 18)))
          {
            Clock_state = CLOCK_ALARM_SETUP;  
          }
        }
      }
      Clock_state = CLOCK_OPERATE;
      break;
    }
    
    case CLOCK_ALARM_SETUP:
    {
      cfg.status_msg = "Alarm event";   /*update the status message*/      
      lp = 5; /*the number of times the alarm sound should be played*/
      Clock_state = CLOCK_ALARM;      
      break;
    }   

    case CLOCK_ALARM:
    {      
      Play_Alarm();
      delay(500);
      if(lp-- == 0)
      {  
        Clock_state = CLOCK_OPERATE;
      }
      break;
    }   

    case CLOCK_NTP_ERROR:
    {
      Serial.println(F("NTP time unavailable"));
      cfg.status_msg = "Error: #4";   /*update the status message*/
      for(lp=4; lp>0; lp--)
      {
        digitalWrite(LED, HIGH);  /*light the indicator LED, there is a problem*/                
        delay(500);
        digitalWrite(LED, LOW);  /*light the indicator LED, there is a problem*/                
        delay(500);
      }
      digitalWrite(LED, LOW);
      delay(3000);    
      Clock_state = CLOCK_OPERATE;      
      break;
    }   
       
    case CLOCK_ERROR:
    {
      /*blink the LED to indicate an error situation*/
      cfg.status_msg = "Error: #";      /*update the status message*/
      cfg.status_msg += error_code;     /*update the status message*/
      for(lp=error_code; lp>0; lp--)
      {
        digitalWrite(LED, HIGH);  /*light the indicator LED, there is a problem*/                
        delay(500);
        digitalWrite(LED, LOW);  /*light the indicator LED, there is a problem*/                
        delay(500);
      }
      digitalWrite(LED, LOW);
      delay(3000);    
      break;
    }   
 
    case CLOCK_IDLE:
    default:
    {
      Clock_state = CLOCK_HOME_TO_SENSOR_SETUP;
      break;
    }
  }
}

/*======================================================================================================================*/
/*                                                     other subroutines                                                */
/*======================================================================================================================*/
/*play the hourly melody before the actual chiming starts*/
void Play_Chime_Melody(void)
{ 
  yield();
  file = new AudioFileSourceSPIFFS("/clock_melody.wav"); 
  wav = new AudioGeneratorWAV();
  wav->begin(file, out);
  while(wav->isRunning())
  {
    yield();
    if (!wav->loop()) wav->stop();
  }
  delete file;  /*when done so time to clean up*/
  delete wav;  
  /*led output is also a pin that is used by the I2S port, therefore we must restore it back to IO when done playing the sample*/
  pinMode(LED, OUTPUT);         /*indicator LED*/
  digitalWrite(LED, LOW);       /*should be off*/
}

/*play the chime (indicating the hours) as many times as required, the last chime will be the sample with the extra long duration*/
void Play_Chime_Hour(void)
{
  yield();
  file = new AudioFileSourceSPIFFS("/clock_chime_short.wav");     
  wav = new AudioGeneratorWAV();
  wav->begin(file, out);
  while(wav->isRunning())
  {
    yield();    
    if (!wav->loop()) wav->stop();
  } 
  delete file;  /*when done so time to clean up*/
  delete wav;  
  /*led output is also a pin that is used by the I2S port, therefore we must restore it back to IO when done playing the sample*/
  pinMode(LED, OUTPUT);         /*indicator LED*/
  digitalWrite(LED, LOW);       /*should be off*/ 
}

/*play the hourly melody before the actual chiming starts*/
void Play_Chime_Quarter(void)
{ 
  yield();
  file = new AudioFileSourceSPIFFS("/clock_chime_quarter.wav"); 
  wav = new AudioGeneratorWAV();
  wav->begin(file, out);
  while(wav->isRunning())
  {
    yield();    
    if (!wav->loop()) wav->stop();
  }
  delete file;  /*when done so time to clean up*/
  delete wav;  
  /*led output is also a pin that is used by the I2S port, therefore we must restore it back to IO when done playing the sample*/
  pinMode(LED, OUTPUT);         /*indicator LED*/
  digitalWrite(LED, LOW);       /*should be off*/   
}

/*play a magical sound (for booting or to indicate ready)*/
void Play_Chime_Magical(void)
{ 
  yield();
  file = new AudioFileSourceSPIFFS("/clock_chime_magical.wav"); 
  wav = new AudioGeneratorWAV();
  wav->begin(file, out);
  while(wav->isRunning())
  {
    yield();    
    if (!wav->loop()) wav->stop();
  }
  delete file;  /*when done so time to clean up*/
  delete wav;  
  /*led output is also a pin that is used by the I2S port, therefore we must restore it back to IO when done playing the sample*/
  pinMode(LED, OUTPUT);         /*indicator LED*/
  digitalWrite(LED, LOW);       /*should be off*/   
}

/*play a old fashioned alarmclock-bell-ringing-sound*/
void Play_Alarm(void)
{ 
  yield();
  file = new AudioFileSourceSPIFFS("/clock_alarm.wav"); 
  wav = new AudioGeneratorWAV();
  wav->begin(file, out);
  while(wav->isRunning())
  {
    yield();    
    if (!wav->loop()) wav->stop();
  }
  delete file;  /*when done so time to clean up*/
  delete wav;  
  /*led output is also a pin that is used by the I2S port, therefore we must restore it back to IO when done playing the sample*/
  pinMode(LED, OUTPUT);         /*indicator LED*/
  digitalWrite(LED, LOW);       /*should be off*/   
}
/*................................................................*/

/*drive the stepper motor that moves the roller, use the "half-step" coil driving pattern*/
void MoveStepper(unsigned long steps, unsigned char dir, unsigned char spd)
{
  static unsigned char state = 0;
  unsigned long lp;

  uint8_t Ca; /*state of coil A*/
  uint8_t Cb; /*state of coil B*/
  uint8_t Cc; /*state of coil C*/
  uint8_t Cd; /*state of coil D*/

#ifdef DEBUG_MODE        /*for debugging purposes, it can be usefull to disable movement*/    
  if(dir == UP) {current_position = current_position + steps;}
  else          {current_position = current_position - steps;}
  return; /*exit immediately*/
#endif
   
  for(lp=steps;lp>0;lp--) /*number of sequences*/
  {
    if(dir == UP)
    {      
      state++;
      current_position++; /*adjust position counter*/
      if(state>7)
      {
        state = 0;
      }
    }
    else
    {
      current_position--; /*adjust position counter*/      
      if(state>0)
      {
        state--;
      }
      else
      {
        state = 7;
      }
    }

    //Serial.print(F("state="));
    //Serial.println(state);

    switch(state)
    {
      case 0: {Ca=LOW ; Cb=LOW ; Cc=HIGH; Cd=LOW ; break;} /*2*/
      case 1: {Ca=LOW ; Cb=HIGH; Cc=HIGH; Cd=LOW ; break;} /*6*/
      case 2: {Ca=LOW ; Cb=HIGH; Cc=LOW ; Cd=LOW ; break;} /*4*/
      case 3: {Ca=LOW;  Cb=HIGH; Cc=LOW ; Cd=HIGH; break;} /*5*/
      case 4: {Ca=LOW ; Cb=LOW ; Cc=LOW ; Cd=HIGH; break;} /*1*/
      case 5: {Ca=HIGH; Cb=LOW ; Cc=LOW ; Cd=HIGH; break;} /*9*/
      case 6: {Ca=HIGH; Cb=LOW ; Cc=LOW ; Cd=LOW ; break;} /*8*/
      case 7: {Ca=HIGH; Cb=LOW ; Cc=HIGH; Cd=LOW ; break;} /*10*/
      default:{Ca=LOW ; Cb=LOW ; Cc=LOW ; Cd=LOW ; break;} /*all coils off*/   
    }

    /*send the stepper motor coil signals to the driver*/
    digitalWrite(COIL_A, Ca);
    digitalWrite(COIL_B, Cb);
    digitalWrite(COIL_C, Cc);
    digitalWrite(COIL_D, Cd);
    
    delay(spd);
  }   
}

/*turn the motor coils off to reduce power consumption*/
void Motor_Off(void)
{
  digitalWrite(COIL_A, LOW);
  digitalWrite(COIL_B, LOW);
  digitalWrite(COIL_C, LOW);
  digitalWrite(COIL_D, LOW);  
}

