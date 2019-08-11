#ifndef __NTP_H
#define __NTP_H

/*------------------------------------------*/

void NTP_init(String ntpserv);
void NTP_offset(long value);
void NTP_statemachine(void);
void NTP_print_time(void);

/*a simple struct to hold all settings*/
typedef struct
{
  unsigned char synced;   /*this flag is true when the time has been fethed from the timeserver*/
  unsigned long epoch;    /*time in seconds since 1970*/
  unsigned int  year;
  unsigned char month;    /*month, ranges from 1 to 12, where 1=january*/
  unsigned char day;
  unsigned char hour;
  unsigned char minute;
  unsigned char second;
  unsigned char weekday;  /*weekday, ranges from 1 to 7, where 1=sunday*/
} NTP_structTYPE;

extern NTP_structTYPE NTP_struct;   /*structure holding all the settings and variables that should be available to all callers who includes this .h file*/

#endif

