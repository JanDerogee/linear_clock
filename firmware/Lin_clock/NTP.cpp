/* Udp NTP Client
 * ============== 
 * Get the time from a Network Time Protocol (NTP) time server 
 * Demonstrates use of UDP sendPacket and ReceivePacket 
 * For more on NTP time servers and the messages needed to communicate with them, 
 * see http://en.wikipedia.org/wiki/Network_Time_Protocol  
 * 
 * 2010-09-04 created (by Michael Margolis)
 * 2012-04-09 modified (by Tom Igoe)
 * 2015-04-12 updated for the ESP8266 (by Ivan Grokhotkov)
 * 2017-02-20 created statemachine, with retry mechanism and internal clock for constant time update (without having to constantly request time from the NTP server (by Jan Derogee)
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "NTP.h"

/*--------------------------------------------*/
enum NTP_states {NTP_INITIALIZE, NTP_REQUEST, NTP_WAITFORPACKET, NTP_CLOCK};  /*state of the TAP file handling statemachine*/

char ntpServerName[32];   /*valid server names are "time.nist.gov" or  "time4.google.com" but there are many many others*/
long offset = 0;          /*use value = 3600 (1*60*60) for UTC+1, use value -3600 (-1*60*60) for UTC-1 etc.*/

const int NTP_PACKET_SIZE = 48;     /*NTP time stamp is in the first 48 bytes of the message*/
unsigned int localPort = 2390;      /*local port to listen for UDP packets*/
IPAddress timeServerIP; /*time.nist.gov NTP server address (Don't hardwire the IP address or we won't get the benefits of the pool)*/

/*required for breaking down time*/
#define LEAP_YEAR(Y)     ((Y>0) && !(Y%4) && ((Y%100) || !(Y%400)))       /*leap year calulator expects time in years AC*/
static  const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31};  /*API starts months from 1, this array starts from 0*/
unsigned char months, days_in_month;
unsigned long days;
/*------*/

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;   /*A UDP instance to let us send and receive packets over UDP*/

NTP_structTYPE NTP_struct;  /*structure holding all the settings and variables that should be available to all callers who includes this .h file*/

/*------------------------------------------------------------------------------------------*/
unsigned long sendNTPpacket(IPAddress& address);
void breaktime(unsigned long timeInput);
/*------------------------------------------------------------------------------------------*/

/*initialize the NTP function*/
void NTP_init(String ntpserv)
{  
  int ntpserv_len = ntpserv.length() + 1;             /*Length (with one extra character for the null terminator)*/
  char char_array[ntpserv_len];                       /*Prepare the character array (the buffer)*/
  ntpserv.toCharArray(ntpServerName, ntpserv_len);    /*Copy it over*/
}

/*method for parsing the offset, which could be made of a timezone value and DST*/
void NTP_offset(long value)
{  
  offset = value;
}

/*Request time from NTP server, in order to get the local time, the timezone must be specified, the value must be in seconds from UTC, so an hour difference*/
/*from UTC would require the timezone value to be 1*60*60 (this could also be a negative value)*/
void NTP_statemachine(void)
{
  static unsigned char retry_count = 0;
  static unsigned char NTP_state = NTP_INITIALIZE;
  static unsigned long current_millis = 0;
  static unsigned long last_millis = 0;
  static unsigned long remainder = 0;
  static int sync_countdown = 0;  
  static int sec_passed;

  switch(NTP_state)
  {   
    case NTP_INITIALIZE: /*initialize the NTP*/
    {
      NTP_struct.synced = false;
      Serial.println(F("Starting UDP"));
      udp.begin(localPort);
      Serial.print(F("Local port: "));
      Serial.println(udp.localPort());
      retry_count = 3;    /*allow ... retries in getting time from the NTP server*/
      NTP_state = NTP_REQUEST;      
      break;      
    }    
       
    case NTP_REQUEST:
    {
      Serial.println(F("Requesting NTP packet..."));      
      last_millis = millis();                       /*get the time since reset (in ms)*/
      remainder = 0;      
      WiFi.hostByName(ntpServerName, timeServerIP); /*get a random server from the pool*/
      sendNTPpacket(timeServerIP);                  /*send an NTP packet to a time server*/
      NTP_state = NTP_WAITFORPACKET;      
      break;      
    }    

    case NTP_WAITFORPACKET:
    {
      if (udp.parsePacket())                                  /*when we receive a packet we process it immediately*/
      {      
        unsigned long highWord;
        unsigned long lowWord;
        unsigned long secsSince1900;
        const unsigned long seventyYears = 2208988800UL; // - 15UL;      /*Unix time starts on Jan 1 1970. In seconds, that's 2208988800*/ 
        /*day 1 : compensate 12 seconds ?!?!?!*/
        /*day 2 : compensate 15 seconds ?!?!?!*/
        /*day 3 : compensate 17 seconds ?!?!?!*/
        udp.read(packetBuffer, NTP_PACKET_SIZE);              /*read the packet into the buffer*/
        highWord = word(packetBuffer[40], packetBuffer[41]);  /*the timestamp starts at byte 40 of the received packet and is four bytes (or two words) long. First, extract the two words*/   
        lowWord = word(packetBuffer[42], packetBuffer[43]);   /**/
        secsSince1900 = highWord << 16 | lowWord;             /*combine the four bytes (two words) into a long integer (this is NTP time (seconds since Jan 1 1900))*/   
        NTP_struct.epoch = secsSince1900 - seventyYears;      /*subtract seventy years in order to convert the NTP time since 1900 into everyday time*/
        breaktime(NTP_struct.epoch);                          /*convert time in a more readable format*/     
        NTP_struct.synced = true;                             /*time is now synced to the time of the timeserver, so we raise the flag to indicate that the time and date are available*/
        NTP_state = NTP_CLOCK;                                /*in the next state we maintain the current time by updating it using the internal clock of the ESP8266*/
      }            
      else
      {
        current_millis = millis();      
        if((current_millis - last_millis) > 5000)             /*when the timeserver does not respond in ...ms we may assume that our request could not be handled*/
        {                                                     /*and we must do another request*/
          Serial.print(F("Timeout exceeded, "));
          retry_count--;
          if(retry_count > 0)
          {
             Serial.println(F("request new packet"));              
             NTP_state = NTP_REQUEST;                         /*timeout exceeded, do a new request*/            
          }
          else
          {
            Serial.println(F("retry failed"));            
            NTP_state = NTP_CLOCK;                           /*stop requesting, try again later, use internal time for now*/
          }
        }
      }
      sync_countdown = 3600;  /*sync every ... seconds*/
      break;      
    }    
    
    case NTP_CLOCK:  /*using the onboard clock/timer we can maintain the time without having to poll the timeserver too many times*/
    { 
      current_millis = millis();
      if(current_millis > last_millis)                        /*check for overflow, if so then ignore the situation and accept the inaccuracy it causes, it will be compensated when the NTP time is requested*/
      {
        sec_passed = (int)(current_millis - last_millis)/1000;
        NTP_struct.epoch = NTP_struct.epoch + (unsigned long)sec_passed;
        remainder = (current_millis - last_millis)%1000;      /*calculate the error (the part less then a second)*/
        last_millis = current_millis - remainder;             /*the part (less then a second) that could not be used, will be remembered and used the next time we calculate time*/        
        breaktime((NTP_struct.epoch + offset)); /*add the offset to the UTC time and then convert it into a more readable format*/
      }
      else
      {
        sec_passed = 0;
        last_millis = millis();
        remainder = 0;
      }

      /*check if it is time to sync with the NTP server again*/
      if(sync_countdown-sec_passed <= 0)
      {
        retry_count = 3;    /*allow ... retries in getting time from the NTP server*/
        NTP_state = NTP_REQUEST;                         /*timeout exceeded, do a new request*/                    
      }
      else
      {  
        NTP_state = NTP_CLOCK;      
      }
      break;      
    }    

    default:
    {    
      NTP_state = NTP_INITIALIZE;     
      break;
    }

    //NTP_print_time();                                   /*Print complete time information*/
  }
}

/*convert seconds since 1970 into a more user readable format*/
void breaktime(unsigned long timeInput)
{
  //Serial.print(F("timeInput=");
  //Serial.println(timeInput);
  NTP_struct.second = timeInput % 60;
  timeInput /= 60; /*and from here time is in minutes*/
  NTP_struct.minute = timeInput % 60;
  timeInput /= 60; /*and from here time is in hours*/
  NTP_struct.hour = timeInput % 24;
  timeInput /= 24; /*/*and from here time is in days*/
  NTP_struct.weekday = ((timeInput + 4) % 7) + 1;  /*Sunday is day 1*/
      
  NTP_struct.year = 1970;   /*year is offset from 1970*/
  days = 0;
  while((unsigned)(days += (LEAP_YEAR(NTP_struct.year) ? 366 : 365)) <= timeInput)
  {
    NTP_struct.year++;
  }
  
  days -= LEAP_YEAR(NTP_struct.year) ? 366 : 365;
  timeInput  -= days; /*now it is days in this year, starting at 0*/
  
  days=0;
  months=0;
  days_in_month=0;
  for (months=0; months<12; months++)
  {
    if (months==1)
    { /*0=january, 1=february, etc.*/
      if (LEAP_YEAR(NTP_struct.year)) {days_in_month=29;}
      else                            {days_in_month=28;}
    }
    else
    {
      days_in_month = monthDays[months];
    }
    
    if (timeInput >= days_in_month)   {timeInput -= days_in_month;}
    else                              {break;}
  }
  NTP_struct.month = months + 1;      /*add one, so now 1=january, 2=february, etc.*/
  NTP_struct.day = timeInput + 1;     /*day of month*/
}


/*Send an NTP request to the time server at the given address*/
unsigned long sendNTPpacket(IPAddress& address)
{
  //Serial.println(F("sending NTP packet...");
  memset(packetBuffer, 0, NTP_PACKET_SIZE);       /*set all bytes in the buffer to 0*/
  /*Initialize values needed to form NTP request (see URL above for details on the packets)*/
  packetBuffer[0] = 0b11100011;   /*LI, Version, Mode*/
  packetBuffer[1] = 0;            /*Stratum, or type of clock*/
  packetBuffer[2] = 6;            /*Polling Interval*/
  packetBuffer[3] = 0xEC;         /*Peer Clock Precision*/
  /*the bytes 4-11 (8 bytes) of zero for Root Delay & Root Dispersion*/
  packetBuffer[12]= 49;
  packetBuffer[13]= 0x4E;
  packetBuffer[14]= 49;
  packetBuffer[15]= 52;

  /*all NTP fields have been given values, now you can send a packet requesting a timestamp*/
  udp.beginPacket(address, 123);              /*NTP requests are to port 123*/
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

/*print the time (mostly for debugging purposes)*/
void NTP_print_time(void)
{
  Serial.print(F("Local time "));           /*show the time*/    
  Serial.print(NTP_struct.year);     
  Serial.print(F("-"));
  Serial.print(NTP_struct.month);
  Serial.print(F("-"));
  Serial.print(NTP_struct.day);
  Serial.print(F(" "));    
  Serial.print(NTP_struct.hour);     
  Serial.print(F(":"));
  Serial.print(NTP_struct.minute);
  Serial.print(F(":"));
  Serial.print(NTP_struct.second);
  Serial.print(F(" it is now day "));
  Serial.println(NTP_struct.weekday);  
}

