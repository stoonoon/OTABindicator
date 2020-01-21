# OTABindicator
Display unit for bin-scraper

Uses an ESP8266 Wemos D1 mini clone and a 2004 I2C LCD display to display:
 - current date/time as synced from NTP.
 - upcoming collection info, as webscraped and converted to json by bin-scraper.

Single momentary switch input for disabling/enabling the LCD backlight.

If a collection is due in the next 24 hours, the display goes into an alarm mode where it will alternately blink the next collection line and blink off the LCD backlight until the backlight on/off button is pressed.

Includes OTA update code so the Arduino sketch can be updated remotely if required.

That's about it....
