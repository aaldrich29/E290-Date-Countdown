# E290 Date Countdown

I wanted to countdown to upcoming Holidays, birthdays, and vacations for my kids. This works with the Heltec Vision Master E290, running the ESP32-S3 with an e-ink display.

### Features

- Displays 3 dates (one larger) for upcoming events
- Shows number of days remaining and date of event
- Can "pin" a date so that it'll show up even if it isn't in the nearest 3 dates
- Supports annual or one-time events
- Uses wifi to configure dates and for time sync
- Hold the boot button the back for a couple seconds to enter config mode
- Deep sleeps until just after midnight to refresh teh days
- Periodically connects to wifi for time sync
- It should theoriretically last a long, long time on battery

### Caveats
- Almost entirely coded with Gemini.. I haven't really cleaned it up and don't have the desire to
- Has been minimally tested so far
- I never could get battery voltage checks to work no matter how many examples I tried
- Not sure if that's my fault or I have a faulty board... please let me know if you figure it out
