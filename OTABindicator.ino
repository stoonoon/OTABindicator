// For wifi connectivity and OTA sketch updates
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

#ifndef STASSID
#define STASSID "TurkWheela"
#define STAPSK  "getmetothepubontime"
#endif

// For LCD panel
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// For NIST NTP time updates
#include <time.h>
#include <sys/time.h>

// For scheduling functions
#include <timer.h>

// For button debouncing
#include <Bounce2.h>
//#define BUTTON_PIN 2 // D4 / GPIO2
#define BUTTON_PIN 0 // D3 / GPIO0


// For debugging
// make true to debug, false to not
#define DEBUG true

// conditional debugging
#if DEBUG 

  #define beginDebug()  do { Serial.begin(115200); Serial.println(F("Debugging enabled")); } while (0)
  #define Trace(x)      do { Serial.print (x);} while (0)
  #define Tracef(x, y)  do { Serial.print(__func__); Serial.print(F(": ")); Serial.printf  (x, y); Serial.println();} while (0)
  #define Trace2(x,y)   do { Serial.print(__func__); Serial.print(F(": ")); Serial.print   (x,y);} while (0)
  #define Traceln(x)    do { Serial.print(__func__); Serial.print(F(": ")); Serial.println (x);} while (0)
  #define Traceln2(x,y) do { Serial.print(__func__); Serial.print(F(": ")); Serial.println (x,y);} while (0)
  #define TraceFunc()   do { Serial.print (F("\nIn function: ")); Serial.println (__PRETTY_FUNCTION__); Serial.println();} while (0)
  
#else
  #define beginDebug()  ((void) 0)
  #define Trace(x)      ((void) 0)
  #define Trace2(x,y)   ((void) 0)
  #define Traceln(x)    ((void) 0)
  #define Traceln2(x,y) ((void) 0)
  #define TraceFunc()   ((void) 0)
#endif // DEBUG

#define MIN(a,b) ((a < b) ? a : b)
    

// dummy json string to test deserialization and parsing
const PROGMEM char test_json_str[] = "[{\"date\": \"16/01/2020\", \"bins\": [\"Household waste\"]}, {\"date\": \"22/01/2020\", \"bins\": [\"Chargeable garden waste\"]}, {\"date\": \"23/01/2020\", \"bins\": [\"Black box or basket recycling\", \"Mixed dry recycling (blue lidded bin)\"]}, {\"date\": \"30/01/2020\", \"bins\": [\"Household waste\"]}]";
const bool use_test_string = false;

const char* ssid = STASSID;
const char* password = STAPSK;

const IPAddress json_server(192,168,1,206);
const int json_port = 1880;
const PROGMEM char json_path[] = "/bin_info";
char json_buffer[600];

const int max_collection_dates = 10; // max number of dates which we expect to store data for
const int max_collection_strings_per_date = 4;
const int max_characters_per_collection_string = 9;
const int max_date_string_length = 9;


typedef struct collection_day_data {
  char date_string[max_date_string_length+1] = ""; // date string should only be 9 characters long
  time_t collection_time_t;
  int collection_count = 0;
  char collection[max_collection_strings_per_date][max_characters_per_collection_string+1];
} ;
  
collection_day_data local_bin_data_array[max_collection_dates];
int collection_day_count = 0; // how many dates we have info for


// SCL on screen connected to D1 on Wemos
// SDA on screen connected to D2 on Wemos
// VCC on screen connected to 5V on Wemos
//nb - my 2004 I2C is 0x27
const int lcd_cols=20;
const int lcd_rows=4;
const int lcd_addr=0x27;
LiquidCrystal_I2C lcd(lcd_addr,lcd_cols,lcd_rows);  // set the LCD address to 0x27 for a 16 chars and 2 line display
char lcd_buffer[lcd_rows][lcd_cols];
char lcd_buffer_prev[lcd_rows][lcd_cols];

// For alarm function
Bounce debouncer = Bounce();
time_t next_collection_time = -1;
bool backlight_is_on = true;
bool text_blink_is_on = true;
bool alarm_active = true; // prob want to default this to false once tested



auto timer = timer_create_default();

// end of globals


void timezoneSetup() {  // uncomment this at some point. 
  TraceFunc();
  Traceln(F("REMINDER : TIMEZONE SETUP STUFF NOT IMPLEMENTED YET"));
  // Makes no difference until BST kicks in, and it's throwing
  // errors in VScode as it doesn't seem to understand that the code will be running on linux,
  // and so setenv and tzset are valid. 

  // time_t rtc_time_t = 1541267183; // fake RTC time for now
  // timezone tz = { 0, 0}; // clear offsets as per https://github.com/esp8266/Arduino/issues/4637#issuecomment-435611842
  // timeval tv = { rtc_time_t, 0};
  // settimeofday(&tv, &tz);
  // setenv(F("TZ"), F("GMT0BST,M3.5.0/1:00:00,M10.5.0/2:00:002"), 1);
  // tzset(); // save the TZ variable  
}

void wifiSetup() {
  //TraceFunc();
  lcd.print(F("Booting WiFi: "));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Traceln(F("Connection Failed! Rebooting..."));
    lcd.println(F("Connection Failed!\n\n"));
    delay(5000);
    ESP.restart();
  }//while
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = F("sketch");
    } else { // U_FS
      type = F("filesystem");
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Traceln(F("Start updating: "));
    Traceln(type);
    lcd.clear();
    lcd.print(F("Start updating:"));
    lcd.setCursor(0,1);
    lcd.print(type);
    
  });
  ArduinoOTA.onEnd([]() {
    Traceln(F("\nEnd"));
    lcd.setCursor(0,3);
    lcd.print(F("End"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Trace(F("Progress: "));
    Tracef("%u%%\r", (progress / (total / 100)));
    lcd.setCursor(0,2);
    lcd.print(F("Progress: "));
    lcd.printf("%u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Trace(F("Error"));
    Tracef("[%u]: ", error);
    lcd.clear();
    lcd.print(F("Error"));
    lcd.printf("[%u]: ", error);
    lcd.setCursor(0,1);
    if (error == OTA_AUTH_ERROR) {
      Traceln(F("Auth Failed"));
      lcd.print(F("Auth Failed"));
    } else if (error == OTA_BEGIN_ERROR) {
      Traceln(F("Begin Failed"));
      lcd.print(F("Begin Failed"));
    } else if (error == OTA_CONNECT_ERROR) {
      Traceln(F("Connect Failed"));
      lcd.print(F("Connect Failed"));
    } else if (error == OTA_RECEIVE_ERROR) {
      Traceln(F("Receive Failed"));
      lcd.print(F("Receive Failed"));
    } else if (error == OTA_END_ERROR) {
      Traceln(F("End Failed"));
      lcd.print(F("End Failed"));
    }
  });
  ArduinoOTA.begin();
  Traceln(F("IP address: "));
  Traceln(WiFi.localIP());
  lcd.clear();
  lcd.print(F("Wifi: Ready. "));
  lcd.setCursor(0,1);
  lcd.print(F("IP address: "));
  lcd.setCursor(0,2);
  lcd.print(WiFi.localIP());
  //Traceln(F("Returning from wifiSetup()"));
}//wifiSetup()

bool getNTPtime(void *) {
  //TraceFunc();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");// will need to sort out timezone & DST settings here!
  return true; // keep timer alive?
}

bool getBinDataJsonString() {
  if (use_test_string) {
    Traceln(F("Using test JSON string"));
    strcpy(json_buffer, test_json_str);
    return true;
  } //if (use_test_string)
  else { // !use_test_string
    Traceln(F("Attempting to load JSON from http"));
    if (getBinDataFromHTTP()) {
      return true;
    }
    else {
      return false;
    }
  } // else
} //getBinDataJsonString()

bool getBinDataFromHTTP() {
  TraceFunc();
  WiFiClient client;
  
  Traceln(F("Starting connection..."));
  if (client.connect(json_server, json_port)) {
    Traceln(F("Connected"));
  }
  else {
    Traceln(F("Connection Failed"));
    return false;
  }
  
  // Make an HTTP request
  client.println(F("GET /bin_info HTTP/1.0"));
  client.println();
  if (client.println() == 0) {
    Traceln(F("Failed to send request"));
    return false;
  }

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Trace(F("Unexpected response: "));
    Traceln(status);
    return false;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Traceln(F("Invalid response"));
    return false;
  }

  int i = 0;
  //trim leading quote and escape characters
  while (client.available()) {
    json_buffer[i] = client.read();
    if ( (i==0) &&  (json_buffer[i]=='"') ) {
      //Traceln(F("Trimming leading quote"));
      // by doing nothing
    }
    else if (json_buffer[i] == '\\') {
      //Traceln(F("Trimming backslash"));
      //by doing nothing
    }
    else {
      i++;
    }
  }
  if (json_buffer[i-1] == '"') {
    //Traceln(F("Trimming trailing quote"));
    json_buffer[i-1] = ' ';
  }
  if (strlen(json_buffer) > 0) {
    return true;
  }
  else {
    Traceln(F("No data recieved from http"));
    return false;
  }
}//getBinDataFromHTTP()

time_t parse_iso (char * iso) {
  // parses an ISO-like date, of a format like "YYYY-MM-DDTHH:MM:SS"
  // and converts it to time_t
  //TraceFunc();
  #define NUM(tens, ones) (((iso[(tens)]-'0')*10) + (iso[(ones)]-'0'))
  if (strlen(iso) == 0) {
    Traceln("parse_iso got empty string");
    return time(NULL);
  }
  int year = ((100 * NUM(0, 1)) + NUM(2, 3));
  int month = NUM(5, 6);
  int day = NUM(8, 9);
  int hour = NUM(11, 12);
  int minute = NUM(14, 15);
  int second = NUM(17, 18);

  struct tm the_tm;
  the_tm.tm_year = year-1900;
  the_tm.tm_mon = month -1;
  the_tm.tm_mday = day;
  the_tm.tm_hour = hour;
  the_tm.tm_min = minute;
  the_tm.tm_sec = second;
  // assume DST is same... and time isn't critical for this application
  return mktime(&the_tm); 
} // parse_iso

void dump_local_array_day(int i) {
  TraceFunc();
  Trace(F("date: "));
  Trace((i+1));
  Trace(F(" of "));
  Trace((collection_day_count));
  Trace(F(":\ndate_string: "));
  collection_day_data this_day = local_bin_data_array[i];
  Trace(this_day.date_string);
  Trace(F("\ncollection_time_tm: "));
  Trace(ctime(&this_day.collection_time_t));
  Trace(F("\ncollection_count: "));
  Trace(this_day.collection_count);
  for (int j=0; j<this_day.collection_count; j++) {
    Trace(F("\nCollection #: "));
    Trace(this_day.collection[j]);
    Trace(F("\n"));
  }
}

void dump_local_array() {
  TraceFunc();
  Trace(F("collection_day_count: "));
  Trace(collection_day_count);
  Trace(F("\n"));
  for (int i=0; i < collection_day_count; i++) {
    dump_local_array_day(i);
  }
  Traceln(F("----------------------------------------"));
  Traceln(F("End of test read of local_bin_data_array"));
  Traceln(F("----------------------------------------"));
}

bool parseJSONbuffer() {
  //TraceFunc();
  StaticJsonDocument<600> json_doc; 
  auto error = deserializeJson(json_doc, json_buffer); // parse message
  if (error) {
    Trace(F("deserializeJson() failed with code: "));
    Traceln(error.c_str());
    if (strlen(json_buffer) == 0 )
    Traceln(F("Input string was:"));
    Traceln(json_buffer);
    return false;
  }//if(error)
  
  
  //inspect json doc
  JsonObject json_doc_as_obj = json_doc.as<JsonObject>();
  JsonArray json_bin_days_array;
  for (JsonPair p : json_doc_as_obj) {
    // look for "bindates" key
    if (strcmp(p.key().c_str(), "bindates")==0) {
      json_bin_days_array = p.value().as<JsonArray>();
    }
  }

  // # number of dates we have collection info for
  collection_day_count = json_bin_days_array.size(); 

  // Iterate over each (date,bindates) element in json_bin_days_array
  for (int i=0; i< collection_day_count; i++) {
    // Get a reference to this specific date
    JsonObject json_single_day = json_bin_days_array[i];

    JsonObject json_single_day_date = json_single_day["date"];

    

    // copy date string from json object to our struct array
    strcpy(local_bin_data_array[i].date_string, json_single_day_date[F("date_string")]);
    
    // Get ISO datetime string from json object
    char isotime[20];
    strcpy(isotime, json_single_day_date[F("date_iso")]);
    //Trace(F("isotime: "));
    //Trace(isotime);
    //Trace(F("\n"));
    
    // Get iso time from json object, parse it and convert to something we can do comparisons with
    time_t coll_time = parse_iso(isotime);

    //Trace(F("parsed time: "));
    //Trace(asctime(some_tm));
    //Trace(F("\n"));
    
    //Traceln(F("copying to our struct array"));
    local_bin_data_array[i].collection_time_t = coll_time;
    updateNextCollectionTime(coll_time);
    //Traceln(F("test read from our struct array"));
    //Trace(asctime(local_bin_data_array[i].collection_time_tm));

    JsonArray single_day_binlist = json_single_day[F("bins")];
    for (int j=0; j<max_collection_strings_per_date; j++) {
      local_bin_data_array[i].collection_count = single_day_binlist.size();
      if (j<single_day_binlist.size()){
        // copy in collection info string if it exists
        strcpy(local_bin_data_array[i].collection[j], single_day_binlist[j]);
      }
      else {
        // initialise unused string as blank
        strcpy(local_bin_data_array[i].collection[j], "");
      }
    }
  }
  //Traceln(F("dumping local array from within parseJSONbuffer"));
  //dump_local_array();
  
  //Traceln(F("Returning from parseJSONbuffer"));
  return true;
}//parseJSONbuffer

void wipeLCDBuffer(int row, int start_col=0, int maxlen=lcd_cols) {
  //wipes chars from lcd_buffer, starting from specified
  //row and col, continues until end of row, or maxlen reached
  
  int wipe_chars = MIN(maxlen, (lcd_cols-start_col));
  for (int i = start_col; i < (start_col + wipe_chars); i++) {
    lcd_buffer[row][i]=' ';
  }
}//wipeLCDBuffer

void strToLCDBuffer(char* str, int start_row=0, int start_col=0,
                  int maxlen=lcd_cols) {
  // copies (upto) maxlen characters from str into lcd_buffer,
  // starting at [row][col] and continues until end of row, 
  // end of str, or maxlen is reached
  
  int in_len = MIN(strlen(str), maxlen);
  int out_len = lcd_cols - start_col;
  int copy_len = MIN(in_len, out_len);
  for (int i=0; i<copy_len; i++) {
    lcd_buffer[start_row][i+start_col] = str[i];
  }
}//strToLCDBuffer

void addCollectionToLCD(int &current_display_line, collection_day_data &this_day) {
  // Check we haven't already filled the screen
  if (current_display_line<lcd_rows) {
    // wipe row from buffer
    wipeLCDBuffer(current_display_line, 0, lcd_cols);
    // Check we actually have something to parse in the date string
    if (strcmp(this_day.date_string, "") != 0) {
      if (text_blink_is_on or (current_display_line > 1)) {
        // copy date string to LCD buffer
        strToLCDBuffer(this_day.date_string, current_display_line, 0);
        // copy 1st collection string to buffer
        strToLCDBuffer(this_day.collection[0], current_display_line, 10);
      }

      // this LCD line is now full
      current_display_line++;

      // check if more LCD rows are available
      if (current_display_line<lcd_rows) {

        // check if there is a 2nd collection on this day
        if (this_day.collection_count > 1) {
          strToLCDBuffer(this_day.collection[1], current_display_line, 0);
          
          // check if there is a 3rd collection on this day
          if (this_day.collection_count > 2) {
            strToLCDBuffer(this_day.collection[2], current_display_line, 10);
          }
          
          // no more data to go on this LCD row
          current_display_line++;
        } // if (collection_day.collection_count > 1)
      } // if (current_display_line<lcd_rows)
    } // if (strcmp(collection_day.date_string, "") != 0)
  } // if (current_display_line<lcd_rows)
}

void updateLCDclockBuffer() {
  // updates date/time display on top row of LCD screen
  // NB - runs every second... no debug code here!!
  time_t timenow = time(NULL);
  struct tm * timeinfo = localtime (&timenow);
  char clockStrBuffer[21];
  strftime(clockStrBuffer, sizeof(clockStrBuffer), "%a %b %d %H:%M:%S ", timeinfo);
  strToLCDBuffer(clockStrBuffer, 0, 0);
  //return true; // keep timer alive
}

void updateNextCollectionTime(time_t coll_time) {
  time_t now = time(NULL);
  // Check if next_collection_time is in the past or uninitialized
  if (next_collection_time < now) { 
    next_collection_time = coll_time;
  }
  //check if coll_time is sooner than current next_collection_time
  else if (coll_time < next_collection_time) {
    next_collection_time = coll_time;
  }
}

void toggleBacklight() {
  if (backlight_is_on) {
    setBacklight(false);
  }
  else {
    setBacklight(true);
  }  
}

void setBacklight(bool action) {
  if (action) {
    lcd.backlight();
    backlight_is_on = true;
  }
  else {
    lcd.noBacklight(); // turn backlight on
    backlight_is_on = false;
  }  
}

void alarm_update() {
  if (alarm_active) {
    time_t rawtime = time(NULL);
    struct tm * timeinfo;
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    int phase = timeinfo->tm_sec;
    phase = phase % 4;
    if (phase == 1) {
      text_blink_is_on = false;
    }
    else {
      text_blink_is_on = true;
    }
    if (phase == 3) {
      setBacklight(false);
    }
    else {
      setBacklight(true);
    }
  }
}

void pushBuffertoLCD() {
  //push buffer to LCD
  // for (int row=0; row<lcd_rows; row++) {
  //   lcd.setCursor(0, row);
  //   lcd.print(lcd_buffer[row]);
  // }
  
  // only push characters that have changed
  for (int row=0; row<lcd_rows; row++) {
    for (int col=0; col<lcd_cols; col++) {
      if (lcd_buffer[row][col] != lcd_buffer_prev[row][col]) {
        lcd.setCursor(col, row);
        lcd.print(lcd_buffer[row][col]);
        lcd_buffer_prev[row][col] = lcd_buffer[row][col];
      }
    }
  }
}

bool updateDisplay(void *) {
  TraceFunc();
  updateLCDclockBuffer();
  int current_display_line = 1; // row of LCD to print next
  bool first_collection = true; // so we can blink the imminent collection only

  // Iterate over all the collection dates we have information for
  for (int i=0; i < collection_day_count; i++) {
  //for (collection_day_data collection_day:local_bin_data_array) {
    collection_day_data this_day = local_bin_data_array[i];
    if (first_collection) {
      alarm_update();
    }
    // Check collection date isn't in the past
    time_t now = time(NULL);
    if (this_day.collection_time_t > now) {
      // Check this isn't a garden collection
      if (strcmp(this_day.collection[0], "Garden") !=0) {
        addCollectionToLCD(current_display_line, this_day);
        first_collection = false;
      }
    }
  } // for (collection_day_data collection_day:local_bin_data_array)
  
  pushBuffertoLCD();

  return true; // keep timer alive
} // updateDisplay

bool bindicatorUpdater(void *) {
  //TraceFunc();
  if (getBinDataJsonString()) {
    if (parseJSONbuffer()) {
      updateDisplay(NULL);
    }
    else {
      Traceln(F("parseJSONbuffer failed."));
    }    
  }
  else {
    Traceln(F("bindicatorUpdater failed"));
  }
  return true; // keep timer alive
} // bindicatorUpdater

void buttonHandler() {
  // check for button press and act accordingly
  debouncer.update();
  if (debouncer.fell()) {
    Traceln("Button press detected");
    //toggleBacklight();
    alarm_active = !alarm_active;
    setBacklight(!alarm_active);
  }
}

void setup() {
  beginDebug();
  TraceFunc();
  lcd.init();
  lcd.backlight();
  for (int i; i<lcd_rows; i++) {
    for (int j; j<lcd_cols; j++) {
      lcd_buffer[i][j] = ' ';
      lcd_buffer_prev[i][j] = ' ';
    }
  }
  timezoneSetup();
  wifiSetup();
  delay(2000);//pause to show wifi config
  
  // setup backlight / alarm snooze button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  debouncer.attach(BUTTON_PIN);
  debouncer.interval(25); // interval in ms

  const int SEC_IN_MILLIS = 1000;
  const unsigned long HOUR_IN_MILLIS = SEC_IN_MILLIS * 60 * 60;
  const unsigned long DAY_IN_MILLIS = HOUR_IN_MILLIS * 24;
  timer.in(1, getNTPtime); // initial sync with NTP
  timer.every(DAY_IN_MILLIS, getNTPtime); // then update NTP every day
  //timer.every(SEC_IN_MILLIS, updateLCDclockBuffer); // update clock and refresh screen every second
  timer.every(SEC_IN_MILLIS, updateDisplay); // refresh screen every second
  timer.in(1*SEC_IN_MILLIS, bindicatorUpdater); // refresh bin list every 15s
  timer.every(60*SEC_IN_MILLIS, bindicatorUpdater); // refresh bin list every 15s
  lcd.clear();
  lcd.print(F("Bindicator loading.."));
  Traceln(F("setup() is done"));
}//setup()

void loop() {
    ArduinoOTA.handle();
    
    buttonHandler();
    timer.tick();
}//loop()
