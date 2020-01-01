/* -----------------------------------------------------------------------------------

       Controller for a standby generator

   This is the software for a custom-built controller that adds
   additional functionality to a standby generator system that uses
     - a Cummins "Quiet Connect" Series generator (20-40 KW), and
     - a Cummins RA series transfer switch

   The controller provides flexibility to program more sophisticated responses to power
   outages, such as  "wait one hour, then if the power is still out run the generator
   for a half hour every two hours until the utility power returns".

   It also acts as a web server, so the behavior can be monitored and controlled
   from anywhere using a standard web browser on a computer or smartphone.

   The controller box, which is connected only to signals inside the transfer switch,
   contains the following components:

   - a 4-line 20-character backlit LCD display
   - seven pushbuttons
   - six lights that indicate current status
   - voltage transducers for reading the utility and generator voltage levels
       using two TM2UA76 DIN-mounted units from Omni Instruments inside the transfer switch:
       https://www.omniinstruments.co.uk/tm2u-ac-voltage-isolated-transducer-rms.html
   - current transducers for the load, using two VC-100 toroids from Phidgets inside the transfer switch:
       https://www.phidgets.com/?tier=3&catid=16&pcid=14&prodid=378
   - two relays for controlling the generator and the transfer switch
   - an Adafruit AirLift ESP32 WiFi Co-Processor module
       https://www.adafruit.com/product/4201
   - a Teensy 3.5 processor, which is fast and has lots of I/O pins
        https://www.pjrc.com/store/teensy35.html
   - various noise-supression techniques were added, because switching 100+ amps nearby causes havoc with electronics
        Littelfuse SP721 transient voltage suppressor diode arrays on signals from outside the box
          https://www.mouser.com/datasheet/2/240/Littelfuse_TVS_Diode_Array_SP721_Datasheet.pdf-777167.pdf
        Vishay ILQ30 optical isolators on digital signals from outside the box
          https://www.mouser.com/datasheet/2/427/ild55-106155.pdf
        Littelfuse V18ZA3P MOV surge suppressor on the power input
          https://www.mouser.com/datasheet/2/240/Littelfuse_Varistor_ZA_Datasheet.pdf-467759.pdf
        Bourns 5900-152-RC 1.5 mH low-pass inductor (plus 33 uF capacitor) on the power input
          https://www.mouser.com/datasheet/2/54/900_series-777308.pdf
        Bud Industries CN-5711 metal enclosure surrounding our electronics
          https://www.budind.com/view/Die+Cast+Aluminum+Enclosure/CN-Series
        Ground plane pour on the unused area of both sides of the PC board
        A deadman watchdog timer that resets us if a glitch causes us to be catatonic
        Resetting the LCD display after power is switched
        Conductive transparent silver nanofilm on the window in front of the LCD display
          https://www.ebay.com/itm/272698061806
      All that seems to have done the job. It's not clear which really did the trick, but the rest can't hurt.

   The current draw from the generator battery is about 150 ma,
   increasing to 250 ma when the WiFi module is transmitting.

   The wiring inside the RA-style transfer box can include a toggle switch that
   disables this controller and uses the factory-provided functionality instead.
   See the interconnect schematic for details.

   For more information including photos and schematics, see
   https://github.com/LenShustek/generator_controller

   ----------------------------------------------------------------------------------------
*** CHANGE LOG ***

   1 Apr 2019, V1.0, L. Shustek
    - initial version
   12 Jul 2019, V2.0, L. Shustek
    - add Wifi support: we're a web server!
    - add support for deadman timer using the Analog Devices ADM6316 supervisor watchdog
    - cope with the generator taking a long time to shut off, and being on when we start up
   10 Oct 2019, V2.1, L. Shustek
    - allow MENU button to work when power is out
   28 Oct 2019, V2.2, L. Shustek
    - refresh power-out display status after MENU button
    - if GEN ON button during rest, set to cycle, not forever on
    - if GEN OFF button during run, set to cycle, not forever off
   16 Nov 2019, V2.3, L. Shustek
    - use the processor watchdog timer (instead of special hardware)
      to cause a reset if we're catatonic
    - implement the "at home" light and switch to simplify the user interface
    - show Wifi firmware level and MAC address
    - add a current limit that cancels generator rest periods if exceeded
    - Use IFTTT ("If This Then That") to send an SMS and email when certain events happen.
    - Accommodate transfer switches (like the RSS-series with controller) that keep control
      of the switch position.
    - Implement an exercise schedule, for switch/generator combinations that don't do it
      when we are wired into the system.
    - Add "extra info" to log file entries, whose interpretation depends on the type
    - Reset the Wifi module whenever we lose connection, and delay trying again.
    - Reset the LCD display whenever power is switched, because it is vulnerable
      to radiated electrical noise.
   23 Dec 2019, V2.4, L. Shustek
    - Don't create log entries for Wifi disconnect/connect; happens too frequently.
      Instead, keep statistics and report from the "show wifi info" submenu.
    - Add ; to end of &xx HTML character references like &nbsp; some browsers insist.

   Bugs:
   - battery voltage at power fail is too low? Change caps for A-to-D? Delay before reading?

   Ideas:
   - util_on and gen_on: also check voltage sense? Needed for RSS transfer switch?

   ------------------------------------------------------------------------------------------------
   Copyright (c) 2019, Len Shustek
   The MIT License (MIT)
   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
   and associated documentation files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or
   substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
   BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
   ------------------------------------------------------------------------------------------------*/

#include "generator.h"
#define VERSION "2.4"

// Here are the defaults that are put into non-volatile memory the first time.
// They may be changed using the configuration menu.
#define DEFAULT_GEN_DELAY_MINS 2*60  // delay until generator starts (if not "at home")
#define DEFAULT_GEN_RUN_MINS 30      // how long the generator runs for
#define DEFAULT_GEN_REST_MINS 3*60   // how long the generator rests for *uf not "at home")
#define DEFAULT_GEN_COOLDOWN_MINS 5  // how long the generator should cool down with no load
#define DEFAULT_UTIL_RETURN_MINS 12  // how long utility power must be on before using it
//                                      (should be more than TDEN=10 for RSS-type controllers)
#define DEFAULT_EXER_DURATION_MINS 0 // exercise duration: 0 (no exercise)
#define DEFAULT_EXER_WDAY 2          // exercise day: monday
#define DEFAULT_EXER_HOUR 11         // exercise hour: 11am
#define DEFAULT_EXER_WEEKS 2         // exercise periodicity: 2 weeks

// Here are fixed timeouts that can only be changed by recompiling.
#define TIMEOUT_GEN_START_SECS 30       // how long we give the generator to start
#define TIMEOUT_GEN_CONNECT_SECS 10     // how long we give the switch to connect to the generator
//                                         (should be more than TDNE=5 for RSS-type controllers)
#define TIMEOUT_UTIL_CONNECT_SECS 10    // how long we give the switch to connect to the utility
#define TIMEOUT_GEN_STOP_SECS 5         // how long we give the generator to stop
//                                         (not obeyed for RS22-type generators)

#define BATTERY_WARNING_LEVEL 12.0      // volts below which to warn about the starter battery
#define BATTERY_CHECK_DELAY_MSEC 1000   // delay after power fail before checking battery and then maybe starting gen
#define BATTERY_WARNING_HYSTERESIS 0.2  // hysteresis gap to avoid repeated warnings

#define GEN_REST_CURRENT_LIMIT 25       // amps above which we won't rest the generator

#define BUTTON_REPEAT_DELAY_MSEC 1000   // how long a button needs to be held before it repeats
#define BUTTON_REPEAT_PERIOD_MSEC 250   // the time between repeats

const byte button_pins[NUM_BUTTONS] = {
   GEN_BUTTON_PIN, MENU_BUTTON_PIN,
   LEFT_BUTTON_PIN, RIGHT_BUTTON_PIN,
   UP_BUTTON_PIN, DOWN_BUTTON_PIN,
   ATHOME_BUTTON_PIN };
enum button_indexes { // (used as byte values)
   GEN_BUTTON, MENU_BUTTON, LEFT_BUTTON, RIGHT_BUTTON, UP_BUTTON, DOWN_BUTTON, ATHOME_BUTTON };
bool button_awaiting_release[NUM_BUTTONS] = { // is button pushed and awaiting release?
   false };
unsigned long button_repeat_time[NUM_BUTTONS] = {0 }; // millis() when button should be repeated
bool button_webpushed[NUM_BUTTONS] = {false }; // buttons with pending "pushes" from the web

bool rungenrelay = false, connectgenrelay = false;
bool athome = false;
bool have_wifi_module = false;
bool exercising = false;
unsigned long exercise_start_millis;
bool ifttt_do_trigger = false;
const char *ifttt_data; // "failed", or "restored", or "test"
time_t last_poweron_time = 0; // When power last came on, either generator or utility

const char *event_names[] = { // must match enum in generator.h
   "controller started",
   "power failed",  "power restored",
   "generator start", "generator didn't start",
   "generator stop", "generator didn't stop",
   "generator cooldown",
   "connect to generator", "couldn't connect to generator", "gen connect with gen off!",
   "connect to utility", "couldn't connect to utility", "util connect with util off!",
   "WiFi module reset", "Wifi connected", "WiFi no connect", "WiFi disconnected",
   "assertion error", "watchdog reset", "starter battery weak", "configuration updated",
   "exercise started", "exercise ended",
   "IFTTT queued", "IFTTT sending", "IFTTT sent", "IFTTT failed" };
// If the following gets a compile error, there is a mismatch with the enum declaration.
typedef char event_name_error[sizeof(event_names) / sizeof(event_names[0]) == EV_NUM_EVENTS ? 1 : -1];

//****  EEPROM storage for configuration info and the event log

#define EEPROM_SIZE 4096   // Teensy 3.5 uses the MK64FX512VMD12 Cortex M4
// processor running at 120 MHz, with 512K Flash, 192K RAM, and 4K EEPROM

#define CONFIG_HDR_LOC 0
struct { // local copy of the configuration data in EEPROM
   char id[6];  // "GENnn"      // unique header ID w/ format version number
#define ID_STRING "GEN03"
   unsigned short gen_delay_mins;    // how many minutes to wait before turning generator on
   unsigned short gen_run_mins;      // how many minutes to run the generator for
   unsigned short gen_rest_mins;     // how many minutes to rest the generator between runs
   unsigned short gen_cooldown_mins; // how long the generator should cool down without load
   unsigned short util_return_mins;  // how many minutes before utility is reconnected
   byte exer_duration_mins;          // how many minutes to exercise for
   byte exer_wday;                   // which day of the week (sun=1)
   byte exer_hour;                   // starting at which hour (0=midnight to 23)
   byte exer_weeks;                  // every how many weeks (1..)
   time_t exer_last;                 // the last time we started an exercise period
#define FOREVER 0xffff
   unsigned short rsvd1, rsvd2, rsvd3; } config_hdr;

#define LOGFILE_HDR_LOC sizeof(config_hdr)
struct logfile_hdr_t logfile_hdr;
#define LOGFILE_LOC (LOGFILE_HDR_LOC+sizeof(logfile_hdr))
#define LOG_MAX ((EEPROM_SIZE - sizeof(config_hdr)) / sizeof(struct logentry_t))
struct logentry_t logfile[LOG_MAX];  // the log entries
int log_max_entries = LOG_MAX;

//------------------------------------------------------------------------------
//    watchdog timer routines, which cause a hard reset if we become catatonic
//------------------------------------------------------------------------------

void watchdog_setup(unsigned msec) { // enable the watchdog timer
   noInterrupts();
   WDOG_UNLOCK = 0xc520; // this two-part unlocking sequence must complete within 20 cycles
   WDOG_UNLOCK = 0xd928;
   __asm__ volatile ("nop\n\t"); // wait one bus cycle (1/2 CPU clock on Teensy)
   __asm__ volatile ("nop\n\t");
   __asm__ volatile ("nop\n\t");
   // we now have 256 bus cycles to program the watchdog timer
   WDOG_TOVALH = msec >> 16;    // how frequently the watchdog needs to be poked
   WDOG_TOVALL = msec & 0xffff; // given that we use the 1 Khz low-power oscillator (LPO)
   WDOG_WINH = 0;
   WDOG_WINL = 0;
   WDOG_PRESC = 0;
   WDOG_STCTRLH = WDOG_STCTRLH_WAITEN | WDOG_STCTRLH_STOPEN | WDOG_STCTRLH_DBGEN
                  | WDOG_STCTRLH_ALLOWUPDATE | WDOG_STCTRLH_WDOGEN;
   interrupts(); }

uint16_t watchdog_counter(void) { // how many times the watchdog has done a reset since poweron
   return WDOG_RSTCNT; }

void watchdog_poke(void) { // poke the watchdog to prevent a reset
   // The K64 processor has a bug: if you refresh the watchdog timer too frequently (more than once
   // per 1 msec watchdog clock timer period?) then the refresh is ignored and the watchdog timer
   // will eventually fire. The bug is discussed in these forums:
   // https://community.nxp.com/thread/474814
   // https://www.silabs.com/community/mcu/8-bit/knowledge-base.entry.html/2016/11/28/wdt_e101_-_restricti-Vqe5
   // My solution is to skip the refresh if the watchdog counter hasn't advanced by at least two.
   static uint16_t last_timerlow = 0;
   noInterrupts();
   uint16_t current_timerlow = WDOG_TMROUTL;
   if ((uint16_t)(current_timerlow - last_timerlow) > 2
         && digitalReadFast(DOWN_BUTTON_PIN) != 0  // TEMP to test watchdog timer
      ) { // don't refresh too frequently
      last_timerlow = current_timerlow;
      WDOG_REFRESH = 0xa602; // this two-part refresh sequence must complete within 20 cycles
      WDOG_REFRESH = 0xb480; }
   interrupts(); }

//-----------------------------------------------------------------------
//    LCD display routines
//-----------------------------------------------------------------------

// We keep a software simulation of the LCD for web access,
// and write to the real LCD hardware only if we have it.

char lcdbuf[4][21]; // our simulated LCD buffer, with 0 string terminators
int lcdrow /* 0..3 */, lcdcol /* 0..19 */;

#if LCD_HW
LiquidCrystal lcdhw (LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

void lcd_start(void) {
   static uint8_t downarrow_char[8] = {
      B00000,
      B00100,
      B00100,
      B00100,
      B10101,
      B01110,
      B00100,
      B00000 };
   static uint8_t uparrow_char[8] = {
      B00100,
      B01110,
      B10101,
      B00100,
      B00100,
      B00100,
      B00100,
      B00000 };

   lcdhw.begin(20, 4); // start LCD display
   lcdhw.createChar(DOWNARROW[0], downarrow_char);
   lcdhw.createChar(UPARROW[0], uparrow_char);
   lcdhw.noCursor(); }

void lcd_restart(void) {
   lcd_start(); // restart the hardware
   for (int row = 0; row < 4; ++row) // copy from our internal buffer to the display
      for (int col = 0; col < 20; ++col) {
         lcdhw.setCursor(col, row);
         lcdhw.print(lcdbuf[row][col]); }
   lcdhw.setCursor(lcdcol, lcdrow); }
#endif

void lcdsetrow( byte row) {
   assert(row <= 3, "bad lcdsetrow", row);
   #if LCD_HW
   lcdhw.setCursor(0, row);
   #endif
   lcdrow = row; lcdcol = 0; }

void lcdsetCursor(byte col, byte row) {
   assert (row <= 3 && col <= 19, "bad lcdsetcursor", (col << 16) | row);
   #if LCD_HW
   lcdhw.setCursor(col, row);
   #endif
   lcdcol = col; lcdrow = row; }

void lcdclear(void) {
   #if LCD_HW
   lcdhw.clear();
   #endif
   memset(lcdbuf, ' ', sizeof(lcdbuf)); // blank the buffer
   for (int row = 0; row < 4; ++row) lcdbuf[row][20] = 0; // insert string terminators
   lcdrow = lcdcol = 0; }

void lcdblink(void) {
   #if LCD_HW
   lcdhw.blink();
   #endif
}
void lcdnoBlink(void) {
   #if LCD_HW
   lcdhw.noBlink();
   #endif
}

bool showing_screen = false; // was the screen the last thing shown?

void lcddumpscreen(void) { // dump the screen image to the terminal
#define CURSOR_UP7 "\e[7A" // works for puTTY VT100, but not for Arduino's Serial Monitor
   #if DEBUG && !LCD_HW
   if (showing_screen) Serial.println(CURSOR_UP7); // rewrite the screen
   Serial.println("|--------------------|");
   for (int row = 0; row < 4; ++row) {
      Serial.print('|');
      for (char *src = lcdbuf[row]; *src; ++src)
         Serial.print( // translate arrows to best ASCII substitutes
            *src == LEFTARROW[0] ? '<' :
            *src == RIGHTARROW[0] ? '>' :
            *src == UPARROW[0] ? '^' :
            *src == DOWNARROW[0] ? 'v' :
            *src
         );
      Serial.println('|'); }
   Serial.println("|--------------------|");
   showing_screen = true;
   #endif
}

void lcdprint(char ch) {
   assert(lcdcol < 20 && lcdrow < 4, "bad lcdprint ch", (lcdcol << 16) | lcdrow);
   #if LCD_HW
   lcdhw.print(ch);
   #endif
   /* if (lcdcol < 20)*/ lcdbuf[lcdrow][lcdcol] = ch;
   if (lcdcol < 19) ++lcdcol;
   lcddumpscreen(); }

void lcdprint(const char *msg) {
   int length = strlen(msg);
   if (length > 20 - lcdcol) {
      Serial.print(length); Serial.print("=len, lcdcol="); Serial.println(lcdcol);
      Serial.println(msg); }
   assert (length <= 20 - lcdcol, "bad lcdprint", (length << 16) | lcdcol);
   #if LCD_HW
   lcdhw.print(msg);
   #endif
   //int length = min((int)strlen(msg), 20 - lcdcol);
   for (int ndx = 0; length--; ++ndx ) {
      if (lcdcol < 20) lcdbuf[lcdrow][lcdcol] = msg[ndx];
      if (lcdcol < 19) ++lcdcol; }
   lcddumpscreen(); }

void lcdprint(byte row, const char *msg) {
   lcdsetrow(row);
   lcdprint(msg); }

void lcdprintf(byte row, const char *msg, ...) {
   char buf[40];
   va_list arg_ptr;
   va_start(arg_ptr, msg);
   vsnprintf(buf, sizeof(buf), msg, arg_ptr);
   assert(strlen(buf) <= 20, "bad lcdprint(msg,..)");
   lcdprint(row, buf);
   va_end(arg_ptr); }

void lcdWiFi_poweron(void) { // power on display and WiFi module
   pinMode(POWER_DISPLAY_WIFI, OUTPUT);
   digitalWrite(POWER_DISPLAY_WIFI, 0); }

void lcdWiFi_poweroff(void) { // power off display and WiFi module
   pinMode(POWER_DISPLAY_WIFI, INPUT);  // leave power control at high-Z for "off"
   // If we decide to do this under some catastrophic condition, after powering them
   // back on we have to reinitialize the display and probably reset the wifi module.
}

//-----------------------------------------------------------------------------------------
//  message routines
//-----------------------------------------------------------------------------------------

bool fatal_error = false;
const char *fatal_msg;

void assert (bool test, const char *msg) {
   assert(test, msg, 0); }

void assert (bool test, const char *msg, short int extra_info) {
   if (!test) {
      lcdclear(); lcdprint("** INTERNAL ERROR **");
      lcdprint(1, "Assertion failed:");
      lcdprint(2, msg);
      char string[30]; sprintf(string, "%08X", extra_info);
      lcdprint(3, string);
      log_event(EV_ASSERTION, extra_info, msg);
      if (DEBUG) {
         Serial.print("** ASSERTION FAILED: ");
         Serial.print(msg);
         Serial.print(", "); Serial.println(extra_info, HEX); }
      fatal_error = true;
      fatal_msg = msg;
      if (have_wifi_module) { // sit here until watchdog timer resets us, but allow web inquiries
         while (true) if (have_wifi_module && digitalRead(WIFI_LED) == WIFI_LED_ON) process_web(); }
      else { // sit here forever, for debugging
         while (true) idle(); }  } }

void center_message (byte row, const char *msg) { // show a one- or two-line message
   byte len = strlen(msg);
   assert (row < 4, "center_message 1", row);
   if (len <= 20) {
      byte nblanks = (20 - len) >> 1;
      lcdsetCursor(0, row);
      for (byte i = 0; i < nblanks; ++i) lcdprint(' ');
      lcdprint(msg);
      for (byte i = 0; i < (20 - nblanks) - len; ++i) lcdprint(' '); }
   else { // break a long message into two lines at a blank
      byte breakpt;
      for (breakpt = 19; breakpt > 0; --breakpt)
         if (msg[breakpt] == ' ') break;
      char string [40];
      strncpy(string, msg, breakpt);
      string[breakpt] = 0;
      center_message(row, string); // show first part (recursive!)
      assert(strlen(msg + breakpt + 1) <= 20 && row < 3, "center_message 2", (breakpt << 16) | row);
      center_message(row + 1, msg + breakpt + 1); // show second part (recursive!)
   }
   lcddumpscreen(); }

//void long_message(byte row, const char *msg) { // print a string multiple lines long
//   center_message(row, ""); lcdsetrow(row);
//   for (byte i = 0; i < (byte)strlen(msg); ++i) {
//      lcdprint(msg[i]);
//      if (i % 20 == 19) {
//         center_message(++row, ""); lcdsetrow(row); } } }

void show_error(byte event_type) {
   log_event(event_type);
   center_message(2, event_names[event_type]);
   delay_looksee(); }

void timeleft_message (const char *msg, unsigned long secs_left) {
   center_message(2, msg);
   unsigned secs = secs_left > 32000 ? 0  // wrapped around to negative?
                   : secs_left;
   char string[40];
   if (secs >= 3600)
      sprintf(string, "%u hr %u min %u sec", secs / 3600, (secs % 3600) / 60, secs % 60);
   else if (secs >= 60)
      sprintf(string, "%u min %u sec", secs / 60, secs % 60);
   else  sprintf(string, "%u seconds", secs);
   center_message(3, string);
   delay(SMIDGE);
   idle(); }

//----------------------------------------------------------------
//    status pin routines
//----------------------------------------------------------------

struct persistent_bool_t util_on = {0 }, gen_on = {0 },
util_connected = {0 }, gen_connected = {0 };

// We don't record a change to those booleans until they have persisted for a while.
// That avoids jitter, contact bounce, etc.
#define BOOL_PERSIST_MSEC 500  // what "a while" means

bool readpin(byte pin) {
   if (digitalRead(pin) == HIGH) return false;
   else return true; }

bool update_bool(struct persistent_bool_t *b, byte pin) {
   // return true if the boolean was false and just became true
   bool pinnow = readpin(pin);
   if (b->changing) {
      if (pinnow == b->val) // it's now the same as before: change was only temporary
         b->changing = false;
      else if (millis() > b->last_change_millis + BOOL_PERSIST_MSEC) { // change has persisted
         b->val = pinnow; // record the change
         b->changing = false;
         if (b->val) return true; } }
   else if (pinnow != b->val) { // we have a tentative change
      b->changing = true;
      b->last_change_millis = millis(); }
   return false; }

void power_switched(void) {
   last_poweron_time = now();
   #if LCD_HW
   lcd_restart(); // restart the LCD display, which power switching may have disrupted
   #endif
}

void update_bools(void) { // update global power status booleans,
   // and keep track of the last time power was switched on
   if (update_bool(&util_on, UTIL_ON_PIN) && util_connected.val)
      power_switched();
   if (update_bool(&gen_on, GEN_ON_PIN) && gen_connected.val)
      power_switched();
   if (update_bool(&gen_connected, GEN_CONNECTED_PIN) && gen_on.val)
      power_switched();
   if (update_bool(&util_connected, UTIL_CONNECTED_PIN) && util_on.val)
      power_switched(); }

void idle(void) {   // the idling routine while we're waiting for something
   if (WATCHDOG) watchdog_poke();
   update_bools();
   if (have_wifi_module) process_web(); }

void delay_long(unsigned long timeleft) {  // a long delay, during which web processing happens
   while (timeleft > 0) {
      delay(SMIDGE);
      idle();
      timeleft -= SMIDGE; } }

void delay_looksee(void) { // a long delay that allows for viewing something
   delay_long(LOOKSEE); }

//-----------------------------------------------------------------------------------------
// scan routines
//-----------------------------------------------------------------------------------------

void skip_blanks(char **pptr) {
   while (**pptr == ' ' || **pptr == '\t')++*pptr; }

bool scan_key(char **pptr, const char *keyword) {
   skip_blanks(pptr);
   char *ptr = *pptr;
   do if (toupper(*ptr++) != *keyword++) return false;
   while (*keyword);
   *pptr = ptr;
   skip_blanks(pptr);
   return true; }

bool scan_str(char **pptr, char *str) { // scan a string up to the next blank or control character
   skip_blanks(pptr);
   if (iscntrl(**pptr)) return false;
   for (int nch = 0; nch < MAXLINE - 1 && (**pptr != ' ' && !iscntrl(**pptr)); ++nch, ++*pptr) *str++ = **pptr;
   *str = '\0';
   skip_blanks(pptr);
   return true; }

bool scan_int(char **pptr, int *pnum, int min, int max) {
   int num;  int nch;
   if (sscanf(*pptr, "%d%n", &num, &nch) != 1
         || num < min || num > max ) return false;
   *pnum = num;
   *pptr += nch;
   skip_blanks(pptr);
   return true; }

bool prefix_str(char *str, char *prefix) {
   return strncmp(prefix, str, strlen(prefix)) == 0; }

//-----------------------------------------------------------------------------------------
//    button routines
//-----------------------------------------------------------------------------------------

// the "at home" button is never returned, but it updates "bool athome" and the ATHOME_LED

bool not_athome(byte button) {
   if (button == ATHOME_BUTTON) {
      athome = !athome;
      if (DEBUG) {
         Serial.print("athome = "); Serial.println(athome);
         showing_screen = false; }
      digitalWrite(ATHOME_LED, athome ? ATHOME_LED_ON : ATHOME_LED_OFF);
      return false; }
   return true; }

byte check_for_button (void) {  // check for a button push (or repeat); return button or 0xff if none
   byte button;
   idle();
   for (button = 0; button < NUM_BUTTONS; ++button) {
      if (digitalRead(button_pins[button]) == HIGH) {  // button is released
         //*** BUG? Will this happen too late if we don't call check_for_button() enough?
         if (button_awaiting_release[button]) {
            button_awaiting_release[button] = false;
            delay (DEBOUNCE); } }
      else { // button is pushed
         if (button_awaiting_release[button]) { // it has already been noticed once
            if (button != ATHOME_BUTTON && millis() > button_repeat_time[button]) { // time to repeat it
               button_repeat_time[button] = millis() + BUTTON_REPEAT_PERIOD_MSEC;
               return button; } }
         else {  // initial push
            delay (DEBOUNCE);
            button_awaiting_release[button] = true; // setup to await release later
            button_repeat_time[button] = millis() + BUTTON_REPEAT_DELAY_MSEC;
            if (not_athome(button)) return button; // return this button
         } }
      if (button_webpushed[button]) { // if we queued a button "push" from the web
         if (DEBUG) {
            Serial.print("web client pushed button "); Serial.println(button);
            showing_screen = false; }
         button_webpushed[button] = false;
         if (not_athome(button)) return button; } }
   return 0xff; }

byte wait_for_button (void) { // wait for a button to be pushed
   byte button;
   while ((button = check_for_button()) == 0xff) idle();
   return button; }

bool menu_button_pushed;
bool yesno(byte row, bool return_if_menu_button, const char *msg) {
   assert(strlen(msg) <= 20, "bad msg in yesno", strlen(msg));
   center_message(row, msg);
   assert(row < 3, "bad row in yesno", row);
   center_message(row + 1, UPARROW " if yes, " DOWNARROW " if no");
   byte button;
   do {
      idle();
      button = wait_for_button();
      menu_button_pushed = button == MENU_BUTTON; }
   while (button != UP_BUTTON && button != DOWN_BUTTON
          && (!return_if_menu_button || button != MENU_BUTTON));
   return button == UP_BUTTON; }

//-------------------------------------------------------
//    analog input routines
//-------------------------------------------------------

// don't change the display too often, to avoid twitchiness
#define ANALOG_CHANGE_MSEC 500

float analog(byte pin, float example_value, float example_analogV) {
   unsigned raw = analogRead(pin);
   return (float)raw * ANALOG_REF / 1024 * example_value / example_analogV; }

int last_max_current = 0;

void show_voltage_current(byte row) {
   static unsigned long show_voltage_time = 0;
   if (millis() - show_voltage_time > ANALOG_CHANGE_MSEC) {
      show_voltage_time = millis();
      int voltage = (int)analog(util_connected.val ? UTIL_VOLTAGE : GEN_VOLTAGE, VOLTAGE_EXAMPLE, VOLTAGE_ANALOG);
      int Ph1A = (int)analog(LOAD_CURRENT1, CURRENT_EXAMPLE, CURRENT_ANALOG);
      int Ph2A = (int)analog(LOAD_CURRENT2, CURRENT_EXAMPLE, CURRENT_ANALOG);
      if (digitalRead(LEFT_BUTTON_PIN) == 0) Ph2A = 75; //TEMP for testing
      last_max_current = max(Ph1A, Ph2A);
      char string[40];
      sprintf(string, "%d VAC  %dA, %dA", voltage, Ph1A, Ph2A);
      center_message(row, string); } }

void show_battery_voltage(byte row) {
   float battV = analog(BATT_VOLTAGE, BATT_EXAMPLE, BATT_ANALOG) + BATT_VOLTAGE_ADJ;
   char string[40];
   sprintf(string, "Gen battery %.1fV", battV);
   center_message(row, string); }

// We only check for low starter battery voltage during a power failure
// without the generator running, because otherwise we're really just
// seeing the generator's battery charger voltage.

bool do_battery_warning = false;
float poweroff_battery_voltage;

void check_battery_voltage(void) {
   delay(SMIDGE); // make sure voltage is stable
   poweroff_battery_voltage = analog(BATT_VOLTAGE, BATT_EXAMPLE, BATT_ANALOG) + BATT_VOLTAGE_ADJ;
   if (poweroff_battery_voltage < BATTERY_WARNING_LEVEL - BATTERY_WARNING_HYSTERESIS / 2) {
      if (!do_battery_warning) log_event(EV_BATTERY_WEAK, (short int) (poweroff_battery_voltage * 10));
      do_battery_warning = true; }
   else if (poweroff_battery_voltage > BATTERY_WARNING_LEVEL + BATTERY_WARNING_HYSTERESIS / 2)
      do_battery_warning = false; }

void show_battery_warning(int row) {
   center_message(row, "starter battery weak");
   char string[40];
   sprintf(string, "It was %.1fV at the last power failure", poweroff_battery_voltage);
   center_message(row + 1, string); }

void clear_battery_warning(void) {
   do_battery_warning = false; }

//-------------------------------------------------------
//     non-volatile EEPROM routines
//-------------------------------------------------------

void eeprom_write(int addr, int length, byte *srcptr) {
   while (length--)
      EEPROM.write(addr++, *srcptr++); }

void eeprom_read(int addr, int length, byte *dstptr) {
   while (length--)
      *dstptr++ = EEPROM.read(addr++); }

void read_config(void) {
   eeprom_read(CONFIG_HDR_LOC, sizeof(config_hdr), (byte *)&config_hdr);
   if (memcmp(config_hdr.id, ID_STRING, sizeof(config_hdr.id)) != 0
         || digitalRead(MENU_BUTTON_PIN) == 0) { // menu button during powerup reinitializes EEPROM
      // initialize the config and log
      memset(&config_hdr, 0, sizeof(config_hdr));
      strcpy(config_hdr.id, ID_STRING);
      config_hdr.gen_delay_mins = DEFAULT_GEN_DELAY_MINS;
      config_hdr.gen_run_mins = DEFAULT_GEN_RUN_MINS;
      config_hdr.gen_rest_mins = DEFAULT_GEN_REST_MINS;
      config_hdr.gen_cooldown_mins = DEFAULT_GEN_COOLDOWN_MINS;
      config_hdr.util_return_mins = DEFAULT_UTIL_RETURN_MINS;
      config_hdr.exer_duration_mins = DEFAULT_EXER_DURATION_MINS;
      config_hdr.exer_wday = DEFAULT_EXER_WDAY;
      config_hdr.exer_hour = DEFAULT_EXER_HOUR;
      config_hdr.exer_weeks = DEFAULT_EXER_WEEKS;
      eeprom_write(CONFIG_HDR_LOC, sizeof(config_hdr), (byte *)&config_hdr);
      memset(&logfile_hdr, 0, sizeof(logfile_hdr));
      eeprom_write(LOGFILE_HDR_LOC, sizeof(logfile_hdr), (byte *)&logfile_hdr);
      center_message(1, "EEPROM initialized");
      delay(LOOKSEE); }
   else { // read the log file
      eeprom_read(LOGFILE_HDR_LOC, sizeof(logfile_hdr), (byte *)&logfile_hdr);
      unsigned count = logfile_hdr.num_entries;
      unsigned ndx = logfile_hdr.oldest;
      while (count--) {
         eeprom_read(LOGFILE_LOC + ndx * sizeof(struct logentry_t),
                     sizeof(struct logentry_t), (byte *)&logfile[ndx]);
         if (++ndx >= LOG_MAX) ndx = 0; } }
   char string[40];
   sprintf(string, "log: %d of %d", logfile_hdr.num_entries, LOG_MAX);
   center_message(2, string);
   delay(LOOKSEE); }

void update_config(void) {
   eeprom_write(CONFIG_HDR_LOC, sizeof(config_hdr), (byte *)&config_hdr); }

//--------------------------------------------------------------------------
//      event log routines
//--------------------------------------------------------------------------

void log_event(byte event_type) {  // make event log entry with no extra info
   #if DEBUG
   Serial.print("log: "); Serial.print(event_names[event_type]);
   #endif
   do_log_event(event_type, 0, ""); }

void log_event(byte event_type, short int extra_info) { // make event log entry with extra info
   #if DEBUG
   Serial.print("log: "); Serial.print(event_names[event_type]);
   Serial.print(", "); Serial.print(extra_info, HEX);
   #endif
   do_log_event(event_type, extra_info, ""); }

void log_event(byte event_type, const char *msg) {
   #if DEBUG
   Serial.print("log: "); Serial.print(event_names[event_type]);
   Serial.print(", "); Serial.print(msg);
   #endif
   do_log_event(event_type, 0, msg); }

void log_event(byte event_type, short int extra_info, const char *msg) {
   #if DEBUG
   Serial.print("log: "); Serial.print(event_names[event_type]);
   Serial.print(", "); Serial.print(extra_info, HEX);
   Serial.print(", "); Serial.print(msg);
   #endif
   do_log_event(event_type, extra_info, msg); }

void do_log_event(byte event_type, short int extra_info, const char *msg) {
   #if DEBUG
   Serial.print(" at "); Serial.println(format_datetime(now()));
   showing_screen = false;
   #endif
   if (logfile_hdr.num_entries == 0) logfile_hdr.num_entries = 1;
   else {
      if (++logfile_hdr.newest >= LOG_MAX) logfile_hdr.newest = 0;
      if (logfile_hdr.num_entries >= LOG_MAX) {
         if (++logfile_hdr.oldest >= LOG_MAX) logfile_hdr.oldest = 0; }
      else ++logfile_hdr.num_entries; }
   logfile[logfile_hdr.newest].datetime = now();
   logfile[logfile_hdr.newest].event_type = event_type;
   logfile[logfile_hdr.newest].extra_info = extra_info;
   if (msg) // a string, but not necessarily stored 0-terminated
      strncpy(logfile[logfile_hdr.newest].msg, msg, LOG_MSGSIZE);
   else memset(logfile[logfile_hdr.newest].msg, 0, LOG_MSGSIZE);
   eeprom_write(LOGFILE_HDR_LOC, sizeof(logfile_hdr), (byte *)&logfile_hdr);
   eeprom_write(LOGFILE_LOC + logfile_hdr.newest * sizeof(struct logentry_t),
                sizeof(struct logentry_t),
                (byte *) &logfile[logfile_hdr.newest]); }

void show_event_log(void) {
   int ndx = -1;
   int num = logfile_hdr.num_entries;
   char string[40];
   if (logfile_hdr.num_entries == 0) {
      lcdclear(); center_message(1, "log is empty");
      delay_looksee();
      return; }
   sprintf(string, "%d log events", logfile_hdr.num_entries);
   center_message(0, string);
   center_message(1, "");
   center_message(2, LEFTARROW " " RIGHTARROW " to scroll");
   center_message(3, "then MENU to exit");
   while (1) {
      switch (wait_for_button()) {
         case MENU_BUTTON:
            return;
         case RIGHT_BUTTON:
            if (ndx == -1) ndx = logfile_hdr.newest;
            else if (ndx != logfile_hdr.newest && ++num && ++ndx >= (int)LOG_MAX) ndx = 0;
            break;
         case LEFT_BUTTON:
            if (ndx == -1) ndx = logfile_hdr.newest;
            else if (ndx != logfile_hdr.oldest && --num && --ndx < 0) ndx = LOG_MAX - 1;
            break;
         default: ; // ignore others
      }
      if (logfile_hdr.num_entries > 0) {
         if (ndx == -1) ndx = logfile_hdr.newest;
         sprintf(string, "event %u of %u", num, logfile_hdr.num_entries);
         center_message(0, string);
         show_datetime(1, logfile[ndx].datetime);
         center_message(3, ""); // the event type description might be 1 or 2 lines
         center_message(2, event_names[logfile[ndx].event_type]); // show it
         if (logfile[ndx].msg[0]) { // display optional message, if any
            memcpy(string, logfile[ndx].msg, LOG_MSGSIZE);
            string[LOG_MSGSIZE] = 0; // make sure it's 0-terminated
            center_message(3, string); } // (might overwrite 2nd line of description)
         short int extra_info = logfile[ndx].extra_info;
         switch (logfile[ndx].event_type) {  // display extra info, if any
               char string[30]; // (in which case the title should be one line)
            case EV_BATTERY_WEAK: // voltage in tenths of a volt
               sprintf(string, "%d.%1dV", extra_info / 10, extra_info % 10);
               center_message(3, string);
               break;
            case EV_ASSERTION:
               sprintf(string, "%04X", extra_info);
               center_message(3, string);
               break;
            case EV_WATCHDOG_RESET:
               sprintf(string, "%d time%c", extra_info, extra_info > 1 ? 's' : ' ');
               center_message(3, string);
               break; } } } }

void clear_log(void) {
   logfile_hdr.num_entries = 0;
   logfile_hdr.newest = 0;
   logfile_hdr.oldest = 0;
   eeprom_write(LOGFILE_HDR_LOC, sizeof(logfile_hdr), (byte *)&logfile_hdr); }

//--------------------------------------------------------------------
//    realtime clock routines
//--------------------------------------------------------------------

// TimeElements is 7 bytes: Second, Minute, Hour, Wday (sun=1), Day, Month, Year from 1970

static const char *months[] = { // index 1..12 from realtime clock
   "???", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const byte days_in_month []  = { // index 1..12 from realtime clock
   99, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static const char *weekdays[] = { // index 1..7
   "???", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

void set_datetime_compile(void) {
   static TimeElements compile_time = {50, 3, 17, 0, 28, 4, 49 }; // when we first wrote the code
   setTime(makeTime(compile_time)); }

bool get_am(TimeElements * timeparts) { // convert from 24-hour to 12-hour clock
   if (timeparts->Hour > 11) {
      if (timeparts->Hour > 12) timeparts->Hour -= 12;
      return false; }
   if (timeparts->Hour == 0) timeparts->Hour = 12;
   return true; }

void use_am (TimeElements * timeparts, bool am) { // convert back to 24-hour clock
   if (am) {
      if (timeparts->Hour == 12) timeparts->Hour = 0; }
   else { //pm
      if (timeparts->Hour < 12) timeparts->Hour += 12; } }

char *format_datetime(time_t thetime) {
   //WARNING: returns pointer to static string
   static char string[40];
   TimeElements timeparts;
   breakTime(thetime, timeparts);
   if (timeparts.Year < 30) { // something is very wrong with the time
      sprintf(string, "?? ??? ???? ??:?? ??"); }
   else {
      bool am = get_am(&timeparts);
      sprintf(string, "%2u %3.3s 20%02u %2u:%02u %cM",
              min(timeparts.Day, 99),
              months[timeparts.Month > 12 ? 0 : timeparts.Month],
              min(timeparts.Year - 30, 99),
              min(timeparts.Hour, 99), min(timeparts.Minute, 99),
              am ? 'A' : 'P'); }
   return string; }

void show_datetime(byte row, time_t thetime) {
   lcdprint(row, format_datetime(thetime)); }

time_t getTeensy3Time() {
   return Teensy3Clock.get(); }

//-----------------------------------------------------------------------
//    Configuration programming
//-----------------------------------------------------------------------

#define CONFIG_ROW 1
bool config_changed;

// Define the position of configurable fields in each programming message
// using the 0-origin offset of the rightmost character of the field.
byte config_date_columns[] = { // if setting date and time:
   1, 5, 10, 13, 16, 19, 0xff }; // day, month, year, hour, minute, am/pm
byte config_time_columns [] = { // if setting a time in minutes
   4 + 2, 0xff }; // minutes
byte config_exercise_columns [] { // if setting exercise period
   1, 9, 12, 14, 16, 0xff }; // mins, weekday, hour, am/pm, weeks

int8_t get_config_changes (byte columns[], byte * field) {
   // process arrow keys and return field value change (+1, -1), or 0 to stop
   while (1) {
      lcdsetCursor(columns[*field], CONFIG_ROW);
      lcdblink();
      switch (wait_for_button()) {  // wait for a button push
         case MENU_BUTTON:
            lcdnoBlink();
            return 0;  // done
         case UP_BUTTON:
            config_changed = true;
            return +1;  // return "increment"
         case DOWN_BUTTON:
            config_changed = true;
            return -1;  // return "decrement"
         case RIGHT_BUTTON:
            if (columns[++*field] == 0xff) *field = 0;
            break;
         case LEFT_BUTTON:
            if (*field == 0)
               while (columns[++*field] != 0xff) ;
            --*field;
            break;
         default: ; // ignore all other buttons
      } } }

// adjust a field up or down, within specified bounds
int bound (int value, int delta /* only +-1 */, int min, int max) {
   if (value <= min && delta < 0) return max;
   if (value >= max && delta > 0) return min;
   return value + delta; }

void set_date(bool parameter) {  //********* change the current date and time
   int delta;
   TimeElements timeparts;
   time_t newdatetime = now();
   byte field = 0; // start with first field
   while (true) {
      show_datetime(CONFIG_ROW, newdatetime); // curent values
      breakTime(newdatetime, timeparts);
      bool am = get_am(&timeparts);
      delta = get_config_changes(config_date_columns, &field);
      if (delta == 0) break;
      switch (field) {
         case 0: // day
            timeparts.Day = bound (timeparts.Day, delta, 1, days_in_month[timeparts.Month]);
            break;
         case 1: // month
            timeparts.Month = bound (timeparts.Month, delta, 1, 12);
            if (timeparts.Day > days_in_month[timeparts.Month])
               timeparts.Day = days_in_month[timeparts.Month];
            break;
         case 2: // year
            timeparts.Year = bound (timeparts.Year, delta, 0, 99);
            break;
         case 3:  // hour
            timeparts.Hour = bound (timeparts.Hour, delta, 1, 12);
            break;
         case 4:  // minutes
            timeparts.Minute = bound (timeparts.Minute, delta, 0, 59);
            break;
         case 5: // am/pm switch
            am = !am;  // just reverse
            break; }
      use_am (&timeparts, am);
      newdatetime = makeTime(timeparts); }
   if (config_changed) {
      Teensy3Clock.set(newdatetime); // write it into the realtime clock
      setTime(newdatetime);  //and use it
      if (0 && DEBUG) {
         Serial.println("clock updated");
         showing_screen = false; } } }

char *format_minutes (char *string, unsigned short mins) {
   if (mins == FOREVER) sprintf(string, "forever");
   else { // "12 hours 59 minutes" is 19 characters
      unsigned short hour = mins / 60;
      unsigned short minsleft = mins % 60;
      if (hour == 0) sprintf(string, "%2u minutes", minsleft);
      else {
         if (minsleft == 0) sprintf(string, "%u hour%s", hour, hour == 1 ? "" : "s");
         else sprintf(string, "%u hour%s %2u minutes", hour, hour == 1 ? "" : "s",  minsleft); } }
   return string; }

void set_time (unsigned short * mins, bool allow_forever) { //********* change a numeric time parameter
   char string[25];
   int delta;
   byte field = 0; // start with first (and only) field
   while (true) {
      center_message(CONFIG_ROW, format_minutes(string, *mins));
      delta = get_config_changes(config_time_columns, &field);
      if (delta == 0) break;
      if (field == 0) { // minutes from 0 to 999, or maybe "forever" if below zero
         if (allow_forever && *mins == 0 && delta == -1) *mins = FOREVER;
         else if (*mins == FOREVER) {
            if (delta == +1) *mins = 0; }
         else *mins = bound (*mins, delta, 0, 999); } } }

void set_gen_delay(bool allow_forever) {
   set_time(&config_hdr.gen_delay_mins, allow_forever); }

void set_gen_runtime(bool allow_forever) {
   set_time(&config_hdr.gen_run_mins, allow_forever); }

void set_gen_resttime(bool allow_forever) {
   set_time(&config_hdr.gen_rest_mins, allow_forever); }

void set_gen_cooltime(bool allow_forever) {
   set_time(&config_hdr.gen_cooldown_mins, allow_forever); }

void set_util_returntime(bool allow_forever) {
   set_time(&config_hdr.util_return_mins, allow_forever); }

void set_exercise_period(bool parameter) {  //********* change the exercise period
   char string[25];
   int delta;
   TimeElements timeparts;
   byte field = 0; // start with first field
   while (true) {
      timeparts.Hour = config_hdr.exer_hour; //convert from 24-hour to 12-hour clock
      bool am = get_am(&timeparts);
      sprintf(string, "%2d min %s %2d%s %1d wk", // "20 min Thu 11am 3 wk"
              config_hdr.exer_duration_mins,
              weekdays[config_hdr.exer_wday],
              timeparts.Hour, am ? "am" : "pm",
              config_hdr.exer_weeks);
      center_message(CONFIG_ROW, string); // current values
      delta = get_config_changes(config_exercise_columns, &field);
      if (delta == 0) break;
      switch (field) {
         case 0: // exercise duration in minutes, 0 for "don't exercise"
            config_hdr.exer_duration_mins = bound (config_hdr.exer_duration_mins, delta, 0, 99);
            break;
         case 1: // day of the week
            config_hdr.exer_wday = bound (config_hdr.exer_wday, delta, 1, 7);
            break;
         case 2:  // hour
            timeparts.Hour = bound (timeparts.Hour, delta, 1, 12);
            break;
         case 3: // am/pm switch
            am = !am;
            break;
         case 4: // number of weeks
            config_hdr.exer_weeks = bound (config_hdr.exer_weeks, delta, 1, 9);
            break; }
      use_am (&timeparts, am); // convert from 12-hour to 24-hour clock
      config_hdr.exer_hour = timeparts.Hour;  } }

void do_configuration (void) { // set configuration parameters
   const static struct {
      const char *title;
      void (*fct)(bool);
      bool parameter; } // for set_time: allow "forever"?
   parm_cmds [] = {
      {"set date and time", set_date, false },
      {"set gen delay", set_gen_delay, false },
      {"set gen run time", set_gen_runtime, true },
      {"set gen rest time", set_gen_resttime, false },
      {"set gen cool time", set_gen_cooltime, false },
      {"set util return time", set_util_returntime, false },
      {"set exercise periods", set_exercise_period, false },
      {NULL, NULL } } ;
   config_changed = false;
   byte cmd = 0; do { // do all config settings
      lcdclear(); center_message(0, parm_cmds[cmd].title); // show instruction on top line
      center_message(2, "arrows: change");
      center_message(3, "MENU: done");
      (parm_cmds[cmd].fct)(parm_cmds[cmd].parameter); } // execute the parm configuration routine
   while (parm_cmds[++cmd].title);
   lcdclear();
   if (config_changed) {
      update_config();  // write configuration into EPROM
      log_event(EV_CONFIG_UPDATED);
      center_message(0, "changes recorded"); }
   else center_message(0, "no changes made");
   delay_looksee(); }

//-----------------------------------------------------------------------
//  Generator routines
//-----------------------------------------------------------------------
/*
   The Cummins RA100A ATS (Automatic Transfer Switch) state machine

   state connected  "connect   util    gen    action   next
   to         to gen"   power  power           state
   1     util        on       on     on     switch    9
   2     util        on       on     off      --
   3     util        on       off    on     switch    11
   4     util        on       off    off      --
   5     util        off      on     on       --
   6     util        off      on     off      --
   7     util        off      off    on       --
   8     util        off      off    off      --
   9     gen         on       on     on       --
   10    gen         on       on     off      --
   11    gen         on       off    on       --
   12    gen         on       off    off      --
   13    gen         off      on     on     switch    5
   14    gen         off      on     off    switch    6
   15    gen         off      off    on       --
   16    gen         off      off    off      --

   Conclusions:  It is stable in all states
   "connect to generator" will switch if generator power is on
   "not connect to generator" (ie, "connect to utility") will switch if utility power is on, even if the generator is not running
*/

#ifdef IFTTT_EVENT
void ifttt_trigger(const char *msg) {
   ifttt_data = msg;
   ifttt_do_trigger = true;
   log_event(EV_IFTTT_QUEUED, ifttt_data);
   if (DEBUG) {
      Serial.print("IFTTT trigger queued: \""); Serial.print(ifttt_data); Serial.println('\"');
      showing_screen = false; } }
#endif

void menu_pushed(void);

bool gen_button(void) {
   byte button = check_for_button();
   if (button == MENU_BUTTON) menu_pushed();
   return button == GEN_BUTTON; }

bool start_gen_now_button(void) {
   return gen_button() && yesno(2, false, "start generator now?"); }

bool stop_gen_now_button(void) {
   return gen_button() && yesno(2, false, "stop generator now?"); }

bool start_generator(void) {
   idle();
   digitalWrite(RUN_GEN_RELAY, RELAY_ON);
   rungenrelay = true;
   log_event(EV_GEN_ON);
   unsigned long waitstart = millis();
   while (!gen_on.val) {
      timeleft_message("starting generator", (millis() - waitstart) / 1000);
      if (millis() - waitstart > TIMEOUT_GEN_START_SECS * 1000) {
         show_error(EV_GEN_ON_FAIL);
         digitalWrite(RUN_GEN_RELAY, RELAY_OFF);
         rungenrelay = false;
         return false; } }
   return true; }

bool stop_generator(void) {
   idle();
   digitalWrite(RUN_GEN_RELAY, RELAY_OFF);
   rungenrelay = false;
   log_event(EV_GEN_OFF);
   exercising = false;
   unsigned long waitstart = millis();
   while (gen_on.val) {
      timeleft_message("stopping generator", (millis() - waitstart) / 1000);
      if (millis() - waitstart > TIMEOUT_GEN_STOP_SECS * 1000) {
         show_error(EV_GEN_OFF_FAIL);
         return false; } }
   center_message(0, "generator stopped");
   return true; }

bool connect_to_generator(void) {
   idle();
   log_event(EV_GEN_CONNECT);
   if (!gen_connected.val) {
      if (gen_on.val) {
         digitalWrite(CONNECT_GEN_RELAY, RELAY_ON);
         connectgenrelay = true;
         unsigned long waitstart = millis();
         while (!gen_connected.val) {
            timeleft_message("connecting generator", (millis() - waitstart) / 1000);
            if (millis() - waitstart > TIMEOUT_GEN_CONNECT_SECS * 1000) {
               show_error(EV_GEN_CONNECT_FAIL);
               return false; } } }
      else { // generator not on -- logic error?
         show_error(EV_GEN_CONNECT_BADSTATE);
         return false; } }
   return true; }

bool connect_to_utility(void) {
   idle();
   log_event(EV_UTIL_CONNECT);
   if (!util_connected.val) {
      if (util_on.val) {
         digitalWrite(CONNECT_GEN_RELAY, RELAY_OFF);  // reconnect to utility power
         connectgenrelay = false;
         unsigned long waitstart = millis();
         while (!util_connected.val) {
            timeleft_message("connecting utility", (millis() - waitstart) / 1000);
            if (millis() - waitstart > TIMEOUT_UTIL_CONNECT_SECS * 1000) {
               show_error(EV_UTIL_CONNECT_FAIL);
               return false; } } }
      else { // utility not on -- logic error?
         show_error(EV_UTIL_CONNECT_BADSTATE);
         return false; } }
   return true; }

bool utility_back(bool do_delay) {
   idle();
   if (!util_on.val) return false;  // utility is stlll off

   // utility power is back
   log_event(EV_POWER_BACK);
   // delay for some minutes until we know it is really stable
   time_t util_connect_datetime = now() + MINS_TO_SECS((time_t) config_hdr.util_return_mins);
   while (do_delay && !util_connected.val && now() < util_connect_datetime) {
      center_message(0, "Await stable power");
      show_voltage_current(1);
      timeleft_message("utility connect in", util_connect_datetime - now());
      if (gen_button() && yesno(2, false, "connect utility now?")) break;
      if (!util_on.val) {  // utility failed again
         show_error(EV_UTIL_FAIL);
         return false; } }
   if (!connect_to_utility()) return false;

   // we're on utility power; let the generator cool down with no load
   if (config_hdr.gen_cooldown_mins != 0) {
      log_event(EV_GEN_COOLDOWN);
      time_t gen_stop_datetime = now() + MINS_TO_SECS((time_t) config_hdr.gen_cooldown_mins);
      while (do_delay && gen_on.val && now() < gen_stop_datetime) {
         center_message(0, "Generator cooldown");
         show_voltage_current(1);
         timeleft_message("generator off in", gen_stop_datetime - now());
         if (gen_button() && yesno(2, false, "stop generator now?")) break;
         if (!util_on.val) {  // utility failed again
            show_error(EV_UTIL_FAIL);
            return false; } } }
   stop_generator();

   #ifdef IFTTT_EVENT
   ifttt_trigger("restored");
   #endif

   return true; }

void gen_pushed(void) {
   lcdclear();
   if (yesno(1, false, "switch to generator?")
         && yesno(1, false, "Are you sure?")) {
      exercising = false;
      center_message(1, "");
      if (!start_generator()) return;
      time_t gen_start_datetime = now();
      if (connect_to_generator()) {
         while (!stop_gen_now_button()) {
            center_message(0, "generator running");
            timeleft_message("generator on for ", now() - gen_start_datetime); }
         utility_back(false); } }
   lcdclear(); }

#if 0 // unused
void run_generator(void) {
   lcdclear();
   if (yesno(1, false, "run generator?")) {
      center_message(1, "");
      if (!start_generator()) return;
      time_t gen_start_datetime = now();
      while (!stop_gen_now_button()) timeleft_message("generator on for ", now() - gen_start_datetime);
      stop_generator(); } }
#endif

#define SECONDS_PER_WEEK (60 * 60 * 24 * 7)
#define SECONDS_PER_HOUR (60 * 60)

void check_exercise_startstop(bool doit) {  // see if we should start or stop an exercise period
   if (exercising) {
      if ((millis() - exercise_start_millis) / 1000 > MINS_TO_SECS(config_hdr.exer_duration_mins)) {
         center_message(0, "finishing exercise");
         stop_generator(); // sets exercising = false;
         log_event(EV_EXERCISE_END);
         lcdclear(); } }
   else if (config_hdr.exer_duration_mins > 0) {
      TimeElements timeparts_now;
      time_t timenow = now();
      breakTime(timenow, timeparts_now);
      if (doit ||
            (timeparts_now.Wday == config_hdr.exer_wday
             && timeparts_now.Hour == config_hdr.exer_hour
             && timenow - config_hdr.exer_last >= config_hdr.exer_weeks * SECONDS_PER_WEEK - SECONDS_PER_HOUR)) {
         exercising = true;
         log_event(EV_EXERCISE_START);
         exercise_start_millis = millis();
         config_hdr.exer_last = timenow;
         update_config(); // update the "last exercised" time in the EEPROM
         center_message(0, "doing exercise");
         if (!start_generator()) {
            exercising = false;
            log_event(EV_EXERCISE_END); }
         lcdclear(); } } }

void show_exercise_info (void) {
   lcdclear();
   if (config_hdr.exer_last == 0)
      center_message(0, "no previous exercise");
   else {
      center_message(0, "last exercise:");
      show_datetime(1, config_hdr.exer_last); }
   if (config_hdr.exer_duration_mins == 0)
      center_message(2, "no exercise coming");
   else {
      center_message(2, "next exercise:");
      TimeElements timeparts;
      time_t lasttime = config_hdr.exer_last;
      if (lasttime == 0) lasttime = now() - config_hdr.exer_weeks * SECONDS_PER_WEEK;
      time_t nexttime = lasttime; // start with the last time
      breakTime(nexttime, timeparts); // adjust to the right weekday and hour, if necessary
      timeparts.Wday = config_hdr.exer_wday;
      timeparts.Hour = config_hdr.exer_hour;
      timeparts.Minute = timeparts.Second = 0; // This is why -SECONDS_PER_HOUR below. Think about it!
      nexttime = makeTime(timeparts);
      while (nexttime - lasttime < config_hdr.exer_weeks * SECONDS_PER_WEEK - SECONDS_PER_HOUR) {
         nexttime += SECONDS_PER_WEEK; } // move by weeks until it's enough beyond the last time
      breakTime(nexttime, timeparts);
      bool am = get_am(&timeparts);
      char string[40];
      sprintf(string, "%2d %3s 20%02d %2d:%02d %cM",
              timeparts.Day, months[timeparts.Month], timeparts.Year - 30,
              timeparts.Hour, timeparts.Minute, am ? 'A' : 'P');
      lcdprint(3, string); }
   delay_long(LOOKSEE * 2); }

void genswitch_control(void) {
   const static struct  {  // manual control routines
      const char *title;
      bool (*fct)(void); }
   genswitch_controls [] = {
      {"turn on generator?", start_generator },
      {"turn off generator?", stop_generator },
      {"connect to gen?", connect_to_generator },
      {"connect to util?", connect_to_utility },
      {NULL, NULL } };
   for (byte cmd = 0; ; ) { //cycle through menu items
      lcdclear();
      center_message(3, "MENU exits");
      bool doit = yesno(0, true, genswitch_controls[cmd].title);
      if (menu_button_pushed) break;
      if (doit) (genswitch_controls[cmd].fct)(); // execute the control routine
      if (genswitch_controls[++cmd].title == NULL) cmd = 0; } // go to the next operation
   lcdclear(); }

//-----------------------------------------------------------------------
//***** the main control logic, when utility power fails,
//***** for starting, stopping, and connecting to the generator
//-----------------------------------------------------------------------

void util_failed(void) {
   bool gen_stay_on = (config_hdr.gen_run_mins == FOREVER);
   time_t util_off_datetime = now();
   time_t gen_start_datetime;
   lcdclear();

   #ifdef IFTTT_EVENT     // queue sending a text and/or an email
   ifttt_trigger("failed");
   #endif
   idle();

   if (!gen_connected.val && !gen_on.val) { // the power must have just failed
      log_event(EV_UTIL_FAIL);
      // checking battery voltage too soon gives bad results, maybe because the
      // battery charger is powering down?
      delay_long(BATTERY_CHECK_DELAY_MSEC);
      check_battery_voltage();
      gen_start_datetime = util_off_datetime  + MINS_TO_SECS((time_t) config_hdr.gen_delay_mins);
      while (!athome && now() < gen_start_datetime) {  // wait until we should start the generator
         idle();
         if (util_on.val) {
            center_message(2, "power back before generator started");
            log_event(EV_POWER_BACK);
            delay_looksee();
            return; }
         check_battery_voltage();
         if (start_gen_now_button())  // the gen start/stop button forces it
            break;
         center_message(0, "power went off at");
         show_datetime(1, util_off_datetime);
         timeleft_message("generator on in", gen_start_datetime - now()); } }

   else  { // we started up with the generator running and/or connected
      center_message(0, "generator already on / connected at");
      show_datetime(2, util_off_datetime);
      delay_looksee(); }

   // the loop we repeat until power returns

   exercising = false; // cancel an exercise period in progress
   while (1) { // alternate running and (perhaps) resting the generator
      if (!start_generator()) return;
      gen_start_datetime = now();
      if (!connect_to_generator()) return;
      time_t gen_stop_datetime = // when we should stop it
         (gen_stay_on || athome) ? NEVER : now() + MINS_TO_SECS(config_hdr.gen_run_mins);

      while (now() < gen_stop_datetime) { // wait until we should turn off generator
         center_message(0, "generator running");
         show_voltage_current(1);
         idle();
         if (utility_back(true)) return;
         if (gen_button()) {
            if (yesno(2, false, "keep generator on?")) {
               gen_stay_on = true; gen_stop_datetime = NEVER; continue; }
            if (yesno(2, false, "rest generator?")) {
               gen_stay_on = false; break; } }
         if (!gen_stay_on && !athome && gen_stop_datetime == NEVER) // one of them must have changed
            gen_stop_datetime =
               (gen_stay_on || athome) ? NEVER : now() + MINS_TO_SECS(config_hdr.gen_run_mins);
         if (gen_stay_on || athome) timeleft_message("duration", now() - gen_start_datetime);
         else timeleft_message("generator off in", gen_stop_datetime - now()); }

      // time for a generator rest time period, maybe
      if (last_max_current > GEN_REST_CURRENT_LIMIT) {
         center_message(2, "current too high; ");
         center_message(3, "rest cancelled!");
         delay_looksee(); }
      else {
         stop_generator(); // start resting
         gen_start_datetime = now() + MINS_TO_SECS((time_t) config_hdr.gen_rest_mins);
         while (!athome && now() < gen_start_datetime) { // wait for the rest period
            idle();
            if (utility_back(true)) return;
            if (start_gen_now_button()) break;
            center_message(0, "generator resting");
            center_message(1, "");
            timeleft_message("will go on in", gen_start_datetime - now()); } } } }

//--------------------------------------------------------------------------
//  special operations submenu
//--------------------------------------------------------------------------

void show_version(void) {
   lcdclear();
   char string[40];
   sprintf(string, "version %s", VERSION);
   center_message(0, string);
   center_message(1, "compiled");
   center_message(2, __DATE__);
   center_message(3, __TIME__); }

#ifdef IFTTT_EVENT
void ifttt_test(void) {
   ifttt_trigger("test");
   center_message(3, "IFTTT trigger queued");
   delay_looksee(); }
#endif

void show_battery_volts(void) {
   lcdclear();
   for (int i = 0; i < LOOKSEE / SMIDGE; ++i) { // keep updating it for a while
      show_battery_voltage(0);
      if (do_battery_warning) show_battery_warning(2);
      delay(SMIDGE);
      idle(); } }

void do_exercise(void) {
   check_exercise_startstop(true); }

void special_operation(void) {
   const static struct  {  // special test routines
      const char *title;
      void (*fct)(void); }
   special_operations [] = {
      {"show version info?", show_version_info },
      {"gen/switch control?", genswitch_control },
      {"clear log?", clear_log },
      #ifdef IFTTT_EVENT
      {"test IFTTT?", ifttt_test },
      #endif
      {"do exercise?", do_exercise },
      {"show battery volts?", show_battery_volts },
      {NULL, NULL } };
   for (byte cmd = 0; ; ) { //cycle through menu items
      lcdclear();
      center_message(3, "MENU exits");
      bool doit = yesno(0, true, special_operations[cmd].title);
      if (menu_button_pushed) break;
      if (doit) {
         (special_operations[cmd].fct)(); // execute the special routine
         break; } // and exit
      if (special_operations[++cmd].title == NULL) cmd = 0; } // go to the next test
   lcdclear(); }

//--------------------------------------------------------------------------
//  top-level menu selection
//--------------------------------------------------------------------------

void show_version_info(void) {
   show_version();
   delay_looksee(); }

void show_wifi_info(void) {
   show_wifi_mac_info();
   show_wifi_network_info();
   show_wifi_stats(); }

void menu_pushed (void) {
   const static struct  {  // menu action routines
      const char *title;
      void (*fct)(void); }
   menu_cmds [] = {
      {"show event log?", show_event_log },
      #if WIFI
      {"show WiFi info?", show_wifi_info },
      #endif
      //// {"run generator?", run_generator },
      {"configure?", do_configuration },
      {"show exercise info?", show_exercise_info },
      {"clear batt warning?", clear_battery_warning },
      {"special operations?", special_operation },
      {NULL, NULL } };
   for (byte cmd = 0; ; ) { //cycle through menu items
      lcdclear();
      center_message(3, "MENU exits");
      bool doit = yesno(0, true, menu_cmds[cmd].title);
      if (menu_button_pushed) break;
      if (doit) {
         (menu_cmds[cmd].fct)(); // execute the command routine
         break; } // and exit
      if (menu_cmds[++cmd].title == NULL) cmd = 0; } // go to the next operation
   lcdclear(); }

//-----------------------------------------------------------------------
//    various hardware tests
//-----------------------------------------------------------------------

bool try_relay(int button_pin, int relay_pin) {
   if (digitalRead(button_pin) == 0) {
      delay(DEBOUNCE);
      digitalWrite(relay_pin, RELAY_ON);
      while (digitalRead(button_pin) == 0) watchdog_poke(); ;
      delay(DEBOUNCE);
      digitalWrite(relay_pin, RELAY_OFF);
      return true; }
   else return false; }

void hardware_tests(void) {

   if (0) { // test buttons
      lcdclear();
      lcdprint("Buttons : ");
      unsigned long start_time = millis();
      do // button pushing until 5 seconds of no activity
         for (byte i = 0; i < NUM_BUTTONS; ++i) {
            lcdsetCursor(8 + i, 0);
            if (digitalRead(button_pins[i]) == 1) lcdprint(' ');
            else {
               lcdprint('0' + i);
               start_time = millis();
               watchdog_poke(); } }
      while (millis() - start_time < 5 * 1000); }

   if (0) { // test relays
      lcdclear(); lcdprint("top buttons : relays");
      unsigned long start_time = millis();
      do { // do relays until 5 seconds of no activity
         if (try_relay(GEN_BUTTON_PIN, CONNECT_GEN_RELAY)
               || try_relay(MENU_BUTTON_PIN, RUN_GEN_RELAY))
            start_time = millis(); }
      while (millis() - start_time < 5 * 1000); }

   if (0) { // test max power usage
      lcdclear(); lcdprint("menu : max power");
      unsigned long start_time = millis();
      do { // turn on everything until 5 seconds of no activity
         if (digitalRead(MENU_BUTTON_PIN) == 0) {
            delay(DEBOUNCE);
            digitalWrite(CONNECT_GEN_RELAY, RELAY_ON);
            digitalWrite(RUN_GEN_RELAY, RELAY_ON);
            digitalWrite(WIFI_LED, WIFI_LED_ON);
            while (digitalRead(MENU_BUTTON_PIN) == 0) watchdog_poke();;
            delay(DEBOUNCE);
            digitalWrite(CONNECT_GEN_RELAY, RELAY_OFF);
            digitalWrite(RUN_GEN_RELAY, RELAY_OFF);
            digitalWrite(WIFI_LED, WIFI_LED_OFF);
            start_time = millis(); } }
      while (millis() - start_time < 5 * 1000); }

   if (0) { // test display/WiFi power switching
      lcdclear(); lcdprint("Menu : display off");
      unsigned long start_time = millis();
      do {
         if (digitalRead(MENU_BUTTON_PIN) == 0) {
            lcdWiFi_poweroff();
            delay(DEBOUNCE);
            while (digitalRead(MENU_BUTTON_PIN) == 0) watchdog_poke();
            lcdWiFi_poweron();
            delay(SMIDGE); // wait for cap to charge up
            // do we need to reinitialize the display?
            lcdclear(); lcdprint("Menu : display off");
            start_time = millis(); } }
      while (millis() - start_time < 5 * 1000); }

   if (0) { // test watchdog timer reset
      lcdclear(); lcdprint("Menu : stop watchdog");
      while (1) if (digitalRead(MENU_BUTTON_PIN) == 0) break; // wait for reset
      delay(DEBOUNCE);
      while (digitalRead(MENU_BUTTON_PIN) == 0) watchdog_poke(); // wait for button release
      delay(DEBOUNCE); } }

#if 0
void show_analog(void) {  // show analog inputs every half second
   static unsigned long last_analog = 0;
   if (millis() - last_analog > 500) {
      char string[40];
      last_analog = millis();
      float genV = analog(GEN_VOLTAGE, VOLTAGE_EXAMPLE, VOLTAGE_ANALOG);
      float utilV = analog(UTIL_VOLTAGE, VOLTAGE_EXAMPLE, VOLTAGE_ANALOG);
      sprintf(string, "Gen %dV, Util %dV", (int)genV, (int)utilV);
      center_message(1, string);
      float Ph1A = analog(LOAD_CURRENT1, CURRENT_EXAMPLE, CURRENT_ANALOG);
      float Ph2A = analog(LOAD_CURRENT2, CURRENT_EXAMPLE, CURRENT_ANALOG);
      sprintf(string, "Ph1 %dA, Ph2 %dA", (int)Ph1A, (int)Ph2A);
      center_message(2, string);
      float battV = analog(BATT_VOLTAGE, BATT_EXAMPLE, BATT_ANALOG) + BATT_VOLTAGE_ADJ;
      sprintf(string, "Gen batt %.1fV", battV);
      center_message(3, string); } }
#endif

//-----------------------------------------------------------------------
//    Initialization
//-----------------------------------------------------------------------

#if WIFI
void wifi_reset(void) {
   digitalWrite(WIFI_RST, 0); // reset the WiFI module
   delay(250);
   digitalWrite(WIFI_RST, 1);
   delay(250); }
#endif


void setup(void) {

   // configure the I/O pins

   for (byte i = 0; i < NUM_BUTTONS; ++i) // button inputs
      pinMode(button_pins[i], INPUT_PULLUP);
   pinMode(GEN_CONNECTED_PIN, INPUT_PULLUP);
   pinMode(GEN_ON_PIN, INPUT_PULLUP);
   pinMode(UTIL_CONNECTED_PIN, INPUT_PULLUP);
   pinMode(UTIL_ON_PIN, INPUT_PULLUP);
   digitalWrite(CONNECT_GEN_RELAY, RELAY_OFF); pinMode(CONNECT_GEN_RELAY, OUTPUT); digitalWrite(CONNECT_GEN_RELAY, RELAY_OFF);
   digitalWrite(RUN_GEN_RELAY, RELAY_OFF); pinMode(RUN_GEN_RELAY, OUTPUT); digitalWrite(RUN_GEN_RELAY, RELAY_OFF);
   pinMode(ATHOME_LED, OUTPUT); digitalWrite(ATHOME_LED, ATHOME_LED_OFF);
   #if WIFI
   pinMode(WIFI_CS, OUTPUT);
   pinMode(WIFI_RST, OUTPUT);
   pinMode(WIFI_BUSY, OUTPUT);
   pinMode(WIFI_LED, OUTPUT);
   pinMode(WIFI_GPI00, OUTPUT);
   WiFi.setPins(WIFI_CS, WIFI_BUSY, WIFI_RST, WIFI_GPI00, &SPI);
   digitalWrite(WIFI_LED, WIFI_LED_OFF); // turn off the "WiFi connected" light
   #endif
   analogReference(0);  // use 3.3V supply as reference

   setSyncProvider(getTeensy3Time);
   if (now() < (time_t)30 * 365 * 24 * 60 * 60) // approx Jan 1, 2000
      set_datetime_compile(); // clock has never been initialized

   lcdWiFi_poweron();
   delay(200);

   update_bools(); // start global boolean updates

   // start up the various modules

   #if DEBUG
   Serial.begin(115200);
   if (!LCD_HW) while (!Serial) ; // wait for puTTY
   Serial.print("Controller started at "); Serial.println(format_datetime(now()));
   #endif

   #if DEBUGSER
   DEBUGPORT.setTX(DEBUGPIN);
   DEBUGPORT.begin(230400);
   DEBUGPORT.println("Debug port");
   #endif

   #if LCD_HW
   lcd_start();
   #endif
   show_version();
   delay(LOOKSEE);
   lcdclear();

   #if WATCHDOG
   watchdog_setup(WATCHDOG_SECS * 1000); // setup watchdog timer
   watchdog_poke();
   #endif

   update_bools();
   hardware_tests();
   read_config();
   int num_resets;
   if ((num_resets = watchdog_counter()) != 0) { // if we experienced a watchdog reset last time
      log_event(EV_WATCHDOG_RESET, num_resets);
      center_message(1, "Watchdog reset!");
      delay(LOOKSEE); }
   log_event(EV_STARTUP);
   lcdclear();

   #if WIFI
   wifi_reset();
   if (WiFi.status() != WL_NO_SHIELD)
      have_wifi_module = true;
   else if (DEBUG) Serial.println("No WiFi module");
   //if (DEBUG) show_wifi_mac_info();
   #endif // WIFI

   update_bools();
   if (util_on.val) {  // if power is on
      if (gen_connected.val     // but we are connected to the generator,
            || gen_on.val) {    // or the generator is running,
         connect_to_utility();  // then try to get everything back to normal
         stop_generator(); }
      last_poweron_time = 0; }
   else {             // if power is off
      if (gen_connected.val) {  // and we are connected to the generator
         digitalWrite(CONNECT_GEN_RELAY, RELAY_ON); // then make that consistent
         connectgenrelay = true;
         log_event(EV_GEN_CONNECT); } }
   lcdclear(); }

#if DEBUG
uint32_t FreeMem() {
   uint32_t stackTop;
   uint32_t heapTop;
   stackTop = (uint32_t) &stackTop;  // current position of the stack.
   void* hTop = malloc(1);   // current position of heap.
   heapTop = (uint32_t) hTop;
   free(hTop);
   return stackTop - heapTop;     // The difference is (approximately) the free, available ram.
}
#endif

//-------------------------------------------------------------------
//    main execution loop
//-------------------------------------------------------------------

void loop(void) {

   // show the various headline messages

#define HEADLINE_UPDATE_MSEC 400  // update them this often
#define HEADLINE_CHANGE_TIMES 5  // and change every this many times
   // the message types
   enum headline_types {PLACENAME, DATETIME, BATTERYWARN, EXERCISE, WRAPAROUND };
   // pointers to the booleans that say whether to show a message type
   static bool alwaystrue = true;
   static bool *headline_doit[] = {&alwaystrue, &alwaystrue, &do_battery_warning, &exercising };
   static int headline = PLACENAME, headline_changecount = 0;
   static unsigned long headline_time = 0;

   if (millis() - headline_time > HEADLINE_UPDATE_MSEC) {  // time to update
      if (++headline_changecount >= HEADLINE_CHANGE_TIMES) { // time to change
         lcddumpscreen();
         headline_changecount = 0;
         if (headline == BATTERYWARN || headline == EXERCISE) lcdclear();
         do { // find the next one we should do
            if (++headline >= WRAPAROUND) headline = PLACENAME; }
         while (!*headline_doit[headline]);
         check_exercise_startstop(false); }
      headline_time = millis();
      switch (headline) { // update the display
         case PLACENAME: center_message(0, TITLE); show_voltage_current(2);
            break;
         case DATETIME: show_datetime(0, now()); show_voltage_current(2);
            break;
         case BATTERYWARN: show_battery_warning(1);
            break;
         case EXERCISE: center_message(1, "Exercising generator");
            timeleft_message("time left",
                             MINS_TO_SECS(config_hdr.exer_duration_mins) - (millis() - exercise_start_millis) / 1000);
            break; } }

   idle();

   if (!util_on.val) {  // if power is out,
      util_failed();    // then spring into action
      lcdclear(); }

   byte button = check_for_button();
   if (button == MENU_BUTTON) menu_pushed();
   if (button == GEN_BUTTON) gen_pushed();

   delay(SMIDGE); }

//*
