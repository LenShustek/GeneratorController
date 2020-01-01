// file:genwifi.c
/* ----------------------------------------------------------------------------------------
   web routines

   This runs almost like a separate process by calling process_web() periodically.
   (Unfortunately some WiFiNINA calls block for many seconds!)

   As a web server we provide the current status page as the home page. There are also these subpages:
     /log         show the event log
     /visitors    show the list of IP addesses who visited
     /pushbutton  push the button in the POST request "button=x"
                  and return the status page after a delay
                  that allows the button action to happen

   Only one client is supported at a time, and the WiFiNINA library has several bugs:
     - long transfers (ie images) have to be broken up and separated in time
     - it won't accept two simultaneous connections from the client, which the Chrome
       brower does for images

   We also can act as a web client (browser) to issue triggers to IFTTT that cause emails
   and/or text messages to be sent.

   See the main module for other details and the change log.
   ----------------------------------------------------------------------------------------
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

/* WiFi.status() return codes
        WL_NO_SHIELD = 255,
        WL_NO_MODULE = WL_NO_SHIELD, // 255
        WL_IDLE_STATUS = 0, // 0
        WL_NO_SSID_AVAIL, // 1
        WL_SCAN_COMPLETED, // 2
        WL_CONNECTED, // 3
        WL_CONNECT_FAILED, // 4
        WL_CONNECTION_LOST, // 5
        WL_DISCONNECTED, // 6
        WL_AP_LISTENING, // 7
        WL_AP_CONNECTED,  // 8
        WL_AP_FAILED //  9
*/

#include "generator.h"

#if WIFI

#define MAX_IP_ADDRESSES 25
#define CONNECT_DELAY_SECS 10   // how long to wait between network connect attempts
#define MAX_CONNECT_ATTEMPTS 3  // (after that we reset the WiFi module)
#define DELAYED_RSP_MSEC 1000   // how long to delay an HTTP response for a button push to be processed

WiFiServer server(WIFI_PORT); // 80 is standard; others are more secure
WiFiClient client; // static client allows only one at a time when we're a server

IPAddress remote_IP;
enum {WEB_NOT_CONNECTED, WEB_AWAITING_CONNECTION,   // WiFi network connection states
      WEB_AWAITING_CLIENT, WEB_PROCESSING_REQUEST   // web server states
     } web_status = WEB_NOT_CONNECTED;

enum  request_type_t {REQ_UNKNOWN, REQ_ROOT, REQ_VISITORS, REQ_LOG, REQ_PUSHBUTTON, REQ_SETPASS, REQ_FAVICON, REQ_BUTTONIMAGE };
const char *request_type_names[] = {"unknown", "root", "visitors", "log", "pushbutton", "setpass", "favicon", "buttonimage", "???" };

enum response_type_t {RSP_UNKNOWN, RSP_STATUS, RSP_VISITORS, RSP_LOG, RSP_FAVICON, RSP_BUTTONIMAGE, RSP_ASKPASS, RSP_NONE };
const char *response_type_names[] = {"unknown", "status", "visitors", "log", "favicon", "buttonimage", "askpass", "no_response", "???" };

bool delayed_response = false;
unsigned long delayed_sendtime;

time_t next_connect_time = 0;  // when to next try to connect to the WiFi network

struct client_t { // history of the clients whose web browsers made requests
   IPAddress ip_address;
   long count;
   bool gave_password; }
clients[MAX_IP_ADDRESSES],
        *current_client;
long requests_processed = 0;
long wifi_connects = 0, wifi_connectfails = 0, wifi_disconnects = 0, wifi_resets = 0;

char linebuf[MAXLINE];

bool client_write(WiFiClient *pclient, const char *buf, int length, bool show) {
#define CHUNKSIZE 500 // to get around WiFiNINA bugs...
   while (length > 0) {
      //Serial.print(length); Serial.print(" bytes to write; ");
      if (!pclient->connected()) {
         //Serial.print("at time "); Serial.print((float)millis() / 1000);
         //Serial.println(" client no longer connected");
         return false; }
      int bytes_done = pclient->write(buf, length > CHUNKSIZE ? CHUNKSIZE : length); //TEMP server.write???
      if (HTML_SHOW_RSP) {
         showing_screen = false;
         Serial.print("at time "); Serial.print((float)millis() / 1000); Serial.print(" wrote ");
         if (show) {
            Serial.print(bytes_done); Serial.print(" bytes: ");
            if (bytes_done < MAXLINE) Serial.write(buf, bytes_done);
            else {
               Serial.write(buf, MAXLINE);
               Serial.write("...\n"); } }
         else {
            Serial.print(bytes_done); Serial.print(" bytes of binary data\n"); } }
      length -= bytes_done;
      buf += bytes_done;
      delay(10); // time for something to happen on the co-processor??
   }
   return true; }

void client_printf(WiFiClient *pclient, const char *format, ...) {
   char buf[MAXLINE];
   va_list argptr;
   va_start(argptr, format);
   vsnprintf(buf, MAXLINE, format, argptr);
   client_write(pclient, buf, strlen(buf), true);
   va_end(argptr); }

char *expand_arrows_and_blanks(char *msg) { // expand our arrow symbols into HTML arrows, blanks into &nbsp
   // WARNING: returns pointer to a static string!
   static char outmsg[MAXLINE];
   char *dst = outmsg;
   for (char *src = msg; *src; ++src) {
      if (*src == LEFTARROW[0]) {
         strcpy(dst, "&#8592;"); dst += 6; }
      else if (*src == UPARROW[0]) {
         strcpy(dst, "&#8593;"); dst += 6; }
      else if (*src == RIGHTARROW[0]) {
         strcpy(dst, "&#8594;"); dst += 6; }
      else if (*src == DOWNARROW[0]) {
         strcpy(dst, "&#8595;"); dst += 6; }
      else if (*src == ' ') {
         strcpy(dst, "&nbsp;"); dst += 5; }
      else *dst++ = *src; }
   *dst = 0;
   return outmsg; }

struct client_t * add_IP_address(WiFiClient *pclient) { // record this IP address in our table
   IPAddress addr = pclient->remoteIP();
   int empty_ndx = -1, min_ndx = -1, min_count = INT_MAX;
   for (int ndx = 0; ndx < MAX_IP_ADDRESSES; ++ndx) {
      if (clients[ndx].ip_address == addr) {// IP address is already in the table
         return &clients[ndx]; }
      if (clients[ndx].count == 0) empty_ndx = ndx; // remember empty slot
      else if (clients[ndx].count < min_count) { // remember min count slot
         min_ndx = ndx; min_count = clients[ndx].count; } }
   if (empty_ndx >= 0) min_ndx = empty_ndx;
   clients[min_ndx].ip_address = addr; // create a new entry for it
   clients[min_ndx].count = 1;
   clients[min_ndx].gave_password = false;
   return &clients[min_ndx]; }

char *format_ip_address(IPAddress addr) {
   // WARNING: returns pointer to a static string!
   static char str[30];
   sprintf(str, "%d.%d.%d.%d:%d", addr[0], addr[1], addr[2], addr[3], WIFI_PORT);
   return str; }

char *format_mac_address(byte *mac) {
   // WARNING: returns pointer to a static string!
   static char str[30];
   sprintf(str, "%02X-%02X-%02X-%02X-%02X-%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]); // why reversed???
   return str; }

void show_wifi_mac_info(void) {
   lcdclear();
   if (WiFi.status() == WL_NO_SHIELD)
      lcdprint("No WiFi module");
   else {
      lcdprint("WiFi firmware:");
      lcdprint(1, WiFi.firmwareVersion());
      lcdprint(2, "MAC address:");
      byte mac[6];
      WiFi.macAddress(mac);
      lcdprint(3, format_mac_address(mac)); }
   delay_looksee();
   lcdclear(); }

void show_wifi_network_info(void) {
   lcdclear();
   if (WiFi.status() == WL_NO_SHIELD)
      lcdprint("No WiFi module");
   else if (WiFi.status() != WL_CONNECTED)
      lcdprint("WiFi not connected");
   else {
      lcdprint("WiFi connected");
      lcdprint(1, WiFi.SSID());
      lcdprint(2, format_ip_address(WiFi.localIP()));
      lcdprintf(3, "strength %ld dBm", WiFi.RSSI()); }
   delay_looksee();
   lcdclear(); }

void show_wifi_stats(void) {
   lcdclear();
   lcdprintf(0, "connects: %ld", wifi_connects);
   lcdprintf(1, "connect fails: %ld", wifi_connectfails);
   lcdprintf(2, "disconnects: %ld", wifi_disconnects);
   lcdprintf(3, "resets: %ld", wifi_resets);
   delay_looksee();
   lcdclear(); }

#if DEBUG
void printWiFiStatus() {
   Serial.print("connected to SSID ");
   Serial.print(WiFi.SSID());
   IPAddress ip = WiFi.localIP();
   Serial.print(" with IP ");
   Serial.print(ip);
   long rssi = WiFi.RSSI();
   Serial.print(", RSSI ");
   Serial.print(rssi);
   Serial.println(" dBm");
   showing_screen = false; }

void print_client_info(const char *msg, WiFiClient *pclient, byte status) {
   Serial.print(msg);
   Serial.print(" port "); Serial.print(pclient->remotePort());
   Serial.print(" status "); Serial.print(status, HEX);
   Serial.print(" on client at "); Serial.print((uint32_t)pclient, HEX); Serial.print(": ");
   for (int i = 0; i < 16; ++i) {
      Serial.print(*((byte *)pclient + i), HEX); Serial.print(' '); }
   Serial.println();
   showing_screen = false; }
#endif

void queue_button_push(int button) {  // queue a button push
   button_webpushed[button] = true;
   delayed_response = true; // queue a delayed response after the button push happens
   delayed_sendtime = millis() + DELAYED_RSP_MSEC;
   if (DEBUG) {
      Serial.print("queuing a delayed response after pushing button ");
      Serial.println(button);
      showing_screen = false; } }

bool check_password (char *ptr) {
   return strcmp(ptr, ACTION_PASSWORD) == 0; }

bool get_request_line(WiFiClient *pclient, char *ptr) {
   int nbytes = pclient->readBytesUntil('\n', ptr, MAXLINE - 1);
   ptr[nbytes] = 0;
   //if (ptr[0] != 0 && ptr[0] != '\n') Serial.println(ptr);
   return nbytes > 1; }

bool check_for_client(const char *msg) {
   byte status;
   client = server.available(&status);
   if (client) {
      #if DEBUG
      print_client_info(msg, &client, status);
      #endif
      return true; }
   return false; }

void generate_response(WiFiClient *pclient, enum response_type_t response_type) {
   #if HTML_SHOW_RSP
   Serial.print("---generating response type "); Serial.print(response_type);
   Serial.print(" \""); Serial.print(response_type_names[response_type]);
   Serial.print("\" at time "); Serial.println(float(millis()) / 1000);
   showing_screen = false;
   #endif
   if (response_type == RSP_FAVICON) {
      extern char iconimagejpg[];    // binary jpg encoding of the image
      extern int iconimagesize;   // its length
      client_printf(pclient, "HTTP/1.1 200 OK\r\n");
      client_printf(pclient, "Content-Length:%d\r\n", iconimagesize);
      client_printf(pclient, "Content-Type: image/jpg\r\n\r\n");
      client_write(pclient, iconimagejpg, iconimagesize, false); }

   else if (response_type == RSP_BUTTONIMAGE) { // respond to the request for the button image
      /* to create the image: draw in powerpoint, save the slide as a jpeg, edit to crop and resize 50%,
         from cmd: binary_to_c buttons.jpg >buttonimage.c
         May also have to adjust width= in RSP_STATUS section. */
      extern char buttonimagejpg[];    // binary jpg encoding of the image
      extern int buttonimagesize;   // its length
      client_printf(pclient, "HTTP/1.1 200 OK\r\n");
      client_printf(pclient, "Content-Length:%d\r\n", buttonimagesize);
      client_printf(pclient, "Content-Type: image/jpg\r\n\r\n");
      client_write(pclient, buttonimagejpg, buttonimagesize, false); }

   else  { // for everything else
      // start by sending our standard http response header, which includes the title and date/time
      const char *response_header[] = { // our standard response header for HTML requests
         "HTTP/1.1 200 OK\r\n",
         "Content-Type: text/html\r\n",
         "Connection: close\r\n",
         //TEMP   "Refresh: 5\r\n", // refresh every 5 seconds
         "\r\n",
         "<!DOCTYPE HTML>\r\n",
         "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1252\"><style>\r\n",
         // define our CSS styles...
         ".lcd {font-family: monospace; font-size:x-large; width:23ch; border:3px; border-style:solid; border-color:blue; border-radius:10px; padding:1em}\r\n",
         ".led{height:20px; width:20px; border-radius:50%; background-color:blue; display:inline-block; position:absolute}\r\n",
         ".button {height:25px; width:25px; border:2px solid red; border-radius:50%; background-color:gray; color:white; display: inline-block; position:absolute;\r\n",
         "  -webkit-transition-duration: 0.2s; /* Safari */ transition-duration: 0.2s; cursor: pointer;}\r\n",
         ".button:hover{background-color:red;}\r\n",
         ".container {position: relative; text-align: left; color: white;}\r\n",
         "</style></head><body>\r\n",
         "<h1>" TITLE " generator</h1>\r\n",
         0 };
      for (const char **ptr = response_header; *ptr; ++ptr)
         client_write(pclient, *ptr, strlen(*ptr), true);
      client_printf(pclient, "<p style=\"font-size:large;\">&nbsp;&nbsp;&nbsp;&nbsp;%s</p><br>\r\n", format_datetime(now()));

      if (response_type == RSP_STATUS) {
         if (fatal_error) {
            client_printf(pclient, "FATAL ERROR: %s<br>\r\n ", fatal_msg); }
         else {
            client_printf(pclient, "<p class=\"lcd\">\r\n"); // boxed fixed-width font for LCD
            for (int row = 0; row < 4; ++row) { // show contents of the LCD display
               client_printf(pclient, expand_arrows_and_blanks(lcdbuf[row]));
               client_printf(pclient, "<br>\r\n"); }
            client_printf(pclient, "</p><div class=\"container\">\r\n");
            client_printf(pclient, "<img src=\"/buttonimage.jpg\" width=\"350\">\r\n");
            update_bools();
#define OFF_COLOR "LightGray"
#define ON_COLOR "Gold"
            client_printf(pclient, "<span class=\"led\" style=\"background-color:%s; left:85px; top:30px\"> </span>\r\n",
                          gen_connected.val ? ON_COLOR : OFF_COLOR);
            client_printf(pclient, "<span class=\"led\" style=\"background-color:%s; left:175px; top:30px\"> </span>\r\n",
                          util_connected.val ? ON_COLOR : OFF_COLOR);
            client_printf(pclient, "<span class=\"led\" style=\"background-color:%s; left:35px; top:45px\"> </span>\r\n",
                          gen_on.val ? ON_COLOR : OFF_COLOR);
            client_printf(pclient, "<span class=\"led\" style=\"background-color:%s; left:225px; top:45px\"> </span>\r\n",
                          util_on.val ? ON_COLOR : OFF_COLOR);
            client_printf(pclient, "<span class=\"led\" style=\"background-color:%s; left:305px; top:111px\"> </span>\r\n",
                          athome ? ON_COLOR : OFF_COLOR);
            client_printf(pclient, "<form action=\"pushbutton.html\" method=\"post\">\r\n");
            client_printf(pclient, "<button class=\"button\" style=\"left:105px; top:85px\" type=\"submit\" name=\"button\" value=\"0\"> </button>\r\n");
            client_printf(pclient, "<button class=\"button\" style=\"left:155px; top:85px\" type=\"submit\" name=\"button\" value=\"1\"> </button>\r\n");
            client_printf(pclient, "<button class=\"button\" style=\"left:32px; top:150px\" type=\"submit\" name=\"button\" value=\"2\"> </button>\r\n");
            client_printf(pclient, "<button class=\"button\" style=\"left:82px; top:150px\" type=\"submit\" name=\"button\" value=\"3\"> </button>\r\n");
            client_printf(pclient, "<button class=\"button\" style=\"left:168px; top:150px\" type=\"submit\" name=\"button\" value=\"4\"> </button>\r\n");
            client_printf(pclient, "<button class=\"button\" style=\"left:222px; top:150px\" type=\"submit\" name=\"button\" value=\"5\"> </button>\r\n");
            client_printf(pclient, "<button class=\"button\" style=\"left:301px; top:85px\" type=\"submit\" name=\"button\" value=\"6\"> </button>\r\n");
            client_printf(pclient, "</form> </div>\r\n"); } }

      else if (response_type == RSP_LOG) {
         client_printf(pclient, "<p style=\"font-size:medium;\">%d log file entries<br>\r\n", logfile_hdr.num_entries);
         if (logfile_hdr.num_entries > 0)
            for (int ndx = logfile_hdr.newest; ;) {
               client_printf(pclient, "%s  %s<br>\r\n",
                             format_datetime(logfile[ndx].datetime),
                             event_names[logfile[ndx].event_type]);
               if (ndx == logfile_hdr.oldest) break;
               if (--ndx < 0) ndx = log_max_entries - 1; }
         client_printf(pclient, "</p>\r\n"); }

      else if (response_type == RSP_VISITORS) {
         client_printf(pclient, "<p style=\"font-size:medium;\">%d total requests processed<br><br>\r\n",
                       requests_processed);
         for (int ndx = 0; ndx < MAX_IP_ADDRESSES; ++ndx)
            if (clients[ndx].count > 0) {
               client_printf(pclient, "IP %s visited %d times%s<br>\r\n",
                             format_ip_address(clients[ndx].ip_address),
                             clients[ndx].count,
                             clients[ndx].gave_password ? "; password was given" : ""); } }

      else if (response_type == RSP_ASKPASS) {
         client_printf(pclient, "<form action=\"setpass.html\" method=\"post\">\r\n");
         client_printf(pclient, "password: <input type=\"password\" name=\"pwd\" minlength=\"3\"><br>\r\n");
         client_printf(pclient, "</form>\r\n"); }

      else { // something else weird
         client_printf(pclient, "<br>**** UNKNOWN HTTP REQUEST %d: %s<br>\r\n",
                       response_type, response_type_names[response_type]); }

      client_printf(pclient, "</body></html>\r\n"); }

   delay(10);
   #if HTML_SHOW_RSP
   Serial.println("closing client connection from generate_response()...");
   showing_screen = false;
   #endif
   while (pclient->connected() && pclient->available() > 0) pclient->read(); // make sure input is empty
   delay(10);
   if (pclient->connected()) pclient->stop(); // stop the TCP connection
   //delete pclient;
   web_status = WEB_AWAITING_CLIENT; }

void process_client_request(WiFiClient *pclient) {
   enum request_type_t request_type = REQ_UNKNOWN;
   enum response_type_t response_type = RSP_UNKNOWN;
   #if HTML_SHOW_REQ
   Serial.print("\nnew request from ");
   Serial.print(pclient->remoteIP());
   Serial.print(": ");
   Serial.print(pclient->remotePort());
   Serial.print(" at time ");
   Serial.println((float)millis() / 1000);
   showing_screen = false;
   #endif
   current_client = add_IP_address(pclient);

   while (get_request_line(pclient, linebuf)) {
      if (HTML_SHOW_REQ) {
         // Serial.print() ignores embedded \r\n character sequences!
         Serial.print("  "); Serial.println(linebuf); }
      char *ptr = linebuf;
      if (scan_key(&ptr, "GET")) {
         if (scan_key(&ptr, "/ ")) request_type = REQ_ROOT;
         else if (scan_key(&ptr, "/VISITORS ")) request_type = REQ_VISITORS;
         else if (scan_key(&ptr, "/LOG ")) request_type = REQ_LOG;
         else if (scan_key(&ptr, "/FAVICON.ICO ")) request_type = REQ_FAVICON;
         else if (scan_key(&ptr, "/BUTTONIMAGE.JPG ")) request_type = REQ_BUTTONIMAGE; }
      else if (scan_key(&ptr, "POST")) {
         if (scan_key(&ptr, "/PUSHBUTTON.HTML")) request_type = REQ_PUSHBUTTON;
         else if (scan_key(&ptr, "/SETPASS.HTML")) request_type = REQ_SETPASS; }    }
   #if HTML_SHOW_RSP
   Serial.print("---received request type "); Serial.print(request_type);
   Serial.print(" \""); Serial.print(request_type_names[request_type]);
   Serial.print("\" at time "); Serial.println(float(millis()) / 1000);
   showing_screen = false;
   #endif

   if (request_type != REQ_FAVICON) {
      ++current_client->count;  ++requests_processed; }

   // done with HTTP request header; figure out what kind of response to generate
   if (request_type == REQ_ROOT)  // normal request for the status page
      response_type = RSP_STATUS;
   else if (request_type == REQ_BUTTONIMAGE)
      response_type = RSP_BUTTONIMAGE;
   else if (request_type == REQ_LOG)
      response_type = RSP_LOG;
   else if (request_type == REQ_VISITORS)
      response_type = RSP_VISITORS;
   else if (request_type == REQ_FAVICON)
      response_type = RSP_FAVICON;

   else if (request_type == REQ_PUSHBUTTON) {
      if (pclient->available() > 2) { // else what???
         while (get_request_line(pclient, linebuf)) {
            //Serial.print("post body line for pushbutton:"); Serial.println(linebuf); //TEMP
            char *ptr = linebuf; int button;
            if (scan_key(&ptr, "BUTTON=") && scan_int(&ptr, &button, 0, NUM_BUTTONS - 1)) {
               if (current_client->gave_password) { // already provided the password
                  queue_button_push(button); // push the button
                  response_type = RSP_NONE; // but delay response until acted on
               }
               else response_type = RSP_ASKPASS; // need to ask for password first
            } } } }

   else if (request_type == REQ_SETPASS) {
      while (get_request_line(pclient, linebuf)) {
         //Serial.print("post body line for password:"); Serial.println(linebuf); //TEMP
         char *ptr = linebuf;
         if (scan_key(&ptr, "PWD=") && check_password(ptr)) {
            current_client->gave_password = true;
            response_type = RSP_STATUS; } //  could also have saved pushbutton and act on it here
         else response_type = RSP_ASKPASS; } }

   if (response_type != RSP_NONE) generate_response(pclient, response_type); }

#ifdef IFTTT_EVENT
void ifttt_send_trigger(WiFiClient *pclient) {
   static char ifttt_server[] = "maker.ifttt.com";
   static char ifttt_path[]   = "/trigger/" IFTTT_EVENT "/with/key/" IFTTT_KEY;
   char json_string[MAXLINE];
   if (DEBUG) {
      Serial.print("sending IFTTT trigger with value1 data \""); Serial.print(ifttt_data); Serial.println('"');
      showing_screen = false; }
   pclient->stop();
   log_event(EV_IFTTT_SENDING, ifttt_data);
   if (pclient->connect(ifttt_server, 80)) { // send the trigger
      sprintf(json_string, "{\"value1\" : \"%s\"}", ifttt_data);
      client_printf(pclient, "POST %s HTTP/1.1\r\n", ifttt_path);
      client_printf(pclient, "Host: %s\r\n", ifttt_server);
      client_printf(pclient, "Content-Length: %d\r\n", strlen(json_string));
      client_printf(pclient, "Content-type: application/json; charset=\"UTF-8\"\r\n");
      client_printf(pclient, "Connection: close\r\n");
      client_printf(pclient, "\r\n");
      client_printf(pclient, "%s\r\n", json_string);
      while (pclient->connected()) {  // read the server's entire response
         while (pclient->available()) {
            char c = pclient->read();
            if (DEBUG) Serial.print(c); } }
      if (DEBUG) {
         Serial.println();
         showing_screen = false; }
      pclient->stop();
      log_event(EV_IFTTT_SENT); }
   else { // failed
      if (DEBUG) {
         Serial.println("failed to connect to IFTTT server");
         Serial.print("WiFi.status="); Serial.print(WiFi.status());
         Serial.print(", client.status="); Serial.println(pclient->status());
         showing_screen = false; }
      log_event(EV_IFTTT_FAILED); } }
#endif

void process_web(void) {
   static bool processing_web = false; // anti-recursion flag
   static int connect_attempts = 0;
   SEROUT("pw");
   update_bools();
   if (HAVE_POWER && !processing_web && now() - last_poweron_time > POWER_ON_WEB_DELAY_SECS) {
      // It's not worth trying web stuff if there is no power, or if the power was recently turned on,
      // because the house's WiFi access points and network routers will be down.
      // It just slows things down because some of the WiFiNINA calls are blocking.
      // Also, the IFTTT trigger message will fail, and we don't currently do retries for that.
      processing_web = true;
      switch (web_status) {

         case WEB_NOT_CONNECTED:
            digitalWrite(WIFI_LED, WIFI_LED_OFF);
            if (now() >= next_connect_time) {
               if (DEBUG) {
                  Serial.print("attempting connection to WiFi network ");
                  Serial.println(WIFI_SSID);
                  showing_screen = false; }
               #ifdef WIFI_IPADDR
               static IPAddress ip(WIFI_IPADDR);
               static IPAddress dns(WIFI_DNSADDR);
               static IPAddress gateway(WIFI_GATEWAYADDR);
               static IPAddress subnet(255, 255, 255, 0);
               WiFi.config(ip, dns, gateway, subnet);
               if (DEBUG) {
                  Serial.print("config with static IP address ");
                  Serial.println(ip); }
               #endif
               SEROUT("begin");
               digitalWrite(WIFI_LED, WIFI_LED_ON); // turn LED on to show the attempt
               // This can block for as long as 50 seconds! So our watchdog timeout should be longer.
               int connectstatus = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
               SEROUT("began");
               if (DEBUG) {
                  Serial.print("WiFi.begin status = ");
                  Serial.println(connectstatus); }
               delay(250); // Necessary to avoid reboot, and 100 msec is not enough! But why??
               ++connect_attempts;
               web_status = WEB_AWAITING_CONNECTION;
               if (DEBUG) {
                  Serial.print("awaiting connection, try "); Serial.println(connect_attempts);
                  showing_screen = false; } }
            break;

         case WEB_AWAITING_CONNECTION:
            switch (WiFi.status()) {
               case WL_CONNECTED:
                  ++wifi_connects;
                  //log_event(EV_WIFI_CONNECTED);
                  if (DEBUG) {
                     Serial.print("starting server on port "); Serial.println(WIFI_PORT);
                     showing_screen = false; }
                  server.begin();
                  #if DEBUG
                  printWiFiStatus();
                  #endif
                  connect_attempts = 0;
                  web_status = WEB_AWAITING_CLIENT;
                  digitalWrite(WIFI_LED, WIFI_LED_ON);;
                  show_wifi_mac_info();
                  show_wifi_network_info();
                  break;
               case WL_IDLE_STATUS:
                  SEROUT("wait");
                  break;
               default: // connection failed
                  digitalWrite(WIFI_LED, WIFI_LED_OFF);
                  ++wifi_connectfails;
                  //log_event(EV_WIFI_NOCONNECT);
                  if (DEBUG) {
                     Serial.println("Failed to connect");
                     showing_screen = false; }
                  if (connect_attempts >= MAX_CONNECT_ATTEMPTS) {
                     if (DEBUG) {
                        Serial.println("Too many connection attempts; resetting WiFi module");
                        showing_screen = false; }
                     ++wifi_resets;
                     //log_event(EV_WIFI_RESET);
                     wifi_reset();
                     connect_attempts = 0; }
                  next_connect_time = now() + CONNECT_DELAY_SECS; // when to try next
                  web_status = WEB_NOT_CONNECTED; }
            break;

         case WEB_AWAITING_CLIENT:
            if (WiFi.status() != WL_CONNECTED) { // we were dumped from the network
               ++wifi_disconnects;
               //log_event(EV_WIFI_DISCONNECTED);
               // try resetting, since otherwise we can't reconnect to the Google Wifi router
               if (DEBUG) {
                  Serial.println("Dumped from network; resetting WiFi module");
                  showing_screen = false; }
               ++wifi_resets;
               //log_event(EV_WIFI_RESET);
               wifi_reset();
               connect_attempts = 0;
               next_connect_time = now() + CONNECT_DELAY_SECS; // when to try next
               web_status = WEB_NOT_CONNECTED; }
            else {
               if (check_for_client("got client")) {
                  //https://arduino.stackexchange.com/questions/31256/multiple-client-server-over-wifi/31263
                  web_status = WEB_PROCESSING_REQUEST;
                  process_client_request(&client); }
               #ifdef IFTTT_EVENT
               else if (ifttt_do_trigger) {  // we're idle and can process an outgoing trigger
                  ifttt_send_trigger(&client);
                  ifttt_do_trigger = false; }
               #endif
            }
            break;

         case WEB_PROCESSING_REQUEST:
            assert(delayed_response, "not delayed rsp?");
            if (millis() > delayed_sendtime) {
               delayed_response = false;
               if (DEBUG) {
                  Serial.println("generating delayed response from button push");
                  showing_screen = false; }
               generate_response(&client, RSP_STATUS); } } }
   SEROUT(".");
   processing_web = false;
   return; }
#else
void process_web(void) {
   return; }
#endif //WIFI
//*
