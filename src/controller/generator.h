//file: generator.h

// compile options

#define DEBUG false                  // send diagnostic info to the serial port?
#define WIFI_LOG false               // log WiFi connects and disconnects?
#define IFTTT_LOG false              // log IFTTT message attempts and results?
#define USE_SECS_FOR_MINS false      // convert the minute delay times to seconds for quick testing?

#define LCD_HW true                  // do we have the LCD hardware attached?
#define WIFI true                    // generate code to be a WiFi server and IFTTT client?
#define WATCHDOG true                // generate code for the watchdog reset?

#define DEBUGSER false               // special hardware serial port debugging
#define DEBUGPORT Serial4            //   on this port
#define DEBUGPIN 32                  //   using this transmit pin (see https://www.pjrc.com/teensy/td_uart.html)
#define HTML_SHOW_REQ true & DEBUG   // show HTML requests in debugging window?
#define HTML_SHOW_RSP true & DEBUG   // show HTML responses in debugging window?

#define IFTTT_RETRIES 5              // how many times to retry sending an IFTTT trigger
#define IFTTT_DELAY_SECS 60          // how many seconds before trying, and between retries?

#define CONNECT_DELAY_SECS 10        // how long to wait between network connect attempts
#define MAX_CONNECT_ATTEMPTS 3       // (after this we reset the WiFi module)
#define MAX_WIFI_RESETS 3            // (after this we drop and restart Wifi module power)

#define POWER_ON_WEB_DELAY_SECS 120  // how many seconds to avoid web access after power returns
#define WATCHDOG_SECS 60             // how many seconds of no action before the watchdog timer resets us
//                                      (Note that WiFi.begin() can block for up to 50 seconds!
#define SMIDGE 100                   // a small polling delay, in msec
#define LOOKSEE 2500                 // a delay for something to be seen, in msec
#define DEBOUNCE 50                  // switch debounce delay, in msec
#define NEVER (time_t)0x7fffffffUL   // a long time (there is confusion: time_t may be signed or unsigned!)
#define MINS_TO_SECS(x) (USE_SECS_FOR_MINS ? x : x*60)

#include <Arduino.h>
#if LCD_HW
   #include <LiquidCrystal.h>
#endif
#include <EEPROM.h>
#include <TimeLib.h>
#include <time.h>
#include <SPI.h>
#if WIFI
   #include <WiFiNINA.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include "generator_hw.h"
#include "Wifi_names.h"  // SSID and password, etc.

#define SEROUT(msg) if(DEBUGSER) DEBUGPORT.println(msg)

#define MAXLINE 500

#define DOWNARROW   "\x01"    // glyphs we define
#define UPARROW     "\x02"
#define RIGHTARROW  "\x7e"    // other non-ASCII glyphs in the character generator
#define LEFTARROW   "\x7f"

struct persistent_bool_t { // the state of a boolean input pin
   bool val;      // the current value
   bool changing; // is it perhaps changing?
   unsigned long last_change_millis;  // when it last changed
};

enum event_type_num { // log event types: must agree with event_names[]
   EV_STARTUP,
   EV_UTIL_FAIL, EV_POWER_BACK,
   EV_GEN_ON, EV_GEN_ON_FAIL,
   EV_GEN_OFF, EV_GEN_OFF_FAIL,
   EV_GEN_COOLDOWN,
   EV_GEN_CONNECT, EV_GEN_CONNECT_FAIL, EV_GEN_CONNECT_BADSTATE,
   EV_UTIL_CONNECT, EV_UTIL_CONNECT_FAIL, EV_UTIL_CONNECT_BADSTATE,
   EV_WIFI_RESET, EV_WIFI_CONNECTED, EV_WIFI_NOCONNECT, EV_WIFI_DISCONNECTED,
   EV_ASSERTION, EV_WATCHDOG_RESET, EV_BATTERY_READ, EV_BATTERY_WEAK, EV_CONFIG_UPDATED,
   EV_EXERCISE_START, EV_EXERCISE_END,
   EV_IFTTT_QUEUED, EV_IFTTT_SENDING, EV_IFTTT_SENT, EV_IFTTT_FAILED,
   EV_MISC, // useful for temporary logging; the info is in the optional message
   EV_NUM_EVENTS };

struct logfile_hdr_t {  // log file header info
   unsigned short num_entries;    // how many log entries are in use
   unsigned short newest;         // index of the newest
   unsigned short oldest;         // index of the oldest
};
struct logentry_t { // the log entries
   time_t datetime;  // time of the event in seconds since 1/1/1970
   byte event_type;
   short int extra_info;  // optional extra binary info
#define LOG_MSGSIZE 20
   char msg[LOG_MSGSIZE]; // optional message, NOT 0-terminated
};
void assert (bool test, const char *msg);
void update_bools(void);
char *format_datetime(time_t thetime, bool showsecs);
void log_event(byte event_type);
void log_event(byte event_type, short int extra_info);
void log_event(byte event_type, const char *msg);
void log_event(byte event_type, short int extra_info, const char *msg);
void log_eventf(byte event_type, const char *msg, ...);
void process_web(void);
void skip_blanks(char **pptr);
bool scan_key(char **pptr, const char *keyword);
bool scan_str(char **pptr, char *str);
bool scan_int(char **pptr, int *pnum, int min, int max);
bool prefix_str(char *str, char *prefix);
#if WIFI
   void show_wifi_mac_info(void);
   void show_wifi_network_info(void);
   void show_wifi_stats(void);
   void wifi_reset(void);
#endif
void lcdclear(void);
void lcdsetrow(byte row);
void lcdsetCursor(byte col, byte row);
void lcdprint(char ch);
void lcdprint(const char *msg);
void lcdprint(byte row, const char *msg);
void lcdprintf(byte row, const char *msg, ...);

extern bool button_webpushed[];
extern bool ifttt_do_trigger;
extern const char *ifttt_data;
extern int ifttt_retry_count;
extern long ifttt_queues;
extern long wifi_resets;
extern unsigned long ifttt_trytime_millis;
extern bool fatal_error;
void delay_looksee(void);
extern const char *fatal_msg;
extern bool athome;
extern char lcdbuf[4][21];
extern bool showing_screen;
extern struct persistent_bool_t util_on, gen_on, util_connected, gen_connected;
extern time_t last_poweron_time;
#define HAVE_POWER ((util_on.val && util_connected.val) || (gen_on.val && gen_connected.val))
extern const char *event_names[];
extern struct logfile_hdr_t logfile_hdr;
extern struct logentry_t logfile[];
extern int log_max_entries;

//*
