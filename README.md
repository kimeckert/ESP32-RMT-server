# ESP32-RMT-server
WiFi server that drives low-level commands to the ESP32 RMT peripheral

__NOTE: Construction in progress__

The ESP32 receives an HTTP POST request and decodes commands contained in the request body.
These commands control the RMT peripheral and drive an infrared LED.
The commands are low-level mark and space commands.
There is no protocol decode implemented in this application.
This allows the RMT to transmit virtually any infrared protocol.
Compiles with [ESP-IDF](https://github.com/espressif/esp-idf)

* The HTTP POST request body consists of one or more text lines. Lines are separated by newline (\n) characters. Returns (\r) are allowed, but are ignored.
* Each line consists of comma-separated values. There is one command on each line. Each line starts with one character that identifies the type of data contained on the remainder of the line.

The intent is that a JavaScript program contained in an HTML file, running on a web browser and served by a host machine (not the ESP32) will create these low-level RMT commands and transmit them to the ESP32. An example HTML file will be part of this repository. A real application will have its own repository.

# Commands

The commands closely follow the RMT register/RAM definitions. Refer to the ESP32 documentation at http://esp32.net/ .
* __c,[div],[high],[low]__
  * Sets the channel clock frequency and the carrier clock frequency and duty cycle.
  * __[div]__: RMT channel clock divider (RMT_DIV_CNT_CHn). Text representation of an integer between 1 and 255, inclusive. Defines the RMT channel clock division ratio.  The channel clock is divided from the source clock.
  * __[high]__ and __[low]__: High (RMT_CARRIER_HIGH_CHn) and low (RMT_CARRIER_LOW_CHn) duration of the carrier waveform. Text representation of integers between 1 and 65,535, inclusive. The unit of carrier_high/low is one channel clock period.
* __t,[non-zero duration], ... ,0__
  * Defines a sequence of durations of IR transmission bits, both modulated at the carrier frequency (mark) and idle (space).
  * __[non-zero duration]__: A text representation of a non-zero integer between -32,767 and 32,767, inclusive. Duration of the transmit burst. A zero value denotes the end of the transmission sequence.
    * Positive integers create a burst of IR output (mark) at the carrier frequency, with duration of [non-zero-duration] channel clock periods.
    * Negative integers create a duration on the IR output with no carrier (space).  The duration is in channel clock periods.
    * Positive and negative values do not need to be alternated. Either positive or negative values can follow a positive or negative value.
  * A zero value tells the RMT to stop the RMT transmission sequence.  The last value on the line must be a zero. A zero anywhere else on the line will terminate the transmission sequence at that point. When terminated, the RMT transmissions can be started again with a command on a new line.
  * The RMT RAM can store a maximum of 128 duration values. If more than 128 values are entered on a line, the driver implements the RMT wrap-around mode to transmit the longer sequence. The terminating zero duration counts as one of the 128 duration values.
* __d,[milliseconds delay]__
  * __[milliseconds delay]__: Create a delay before decoding the next line. Used to create a realtively long gap between IR tramsmissions. Text representation of a positive non-zero integer of the number of milliseconds for the delay.  Implemented with the FreeRTOS function:
    * vTaskDelay( milliseconds delay / portTICK_PERIOD_MS ); and has the limitations of that function. See the documentation for this function for the maximum possible delay, which depends on the tick period of your system and the data type used for this function. If your tick period is 1mS and the data type uses a 16-bit unsigned int, the maximum duration is about 65 seconds.

# Definitions
* __HTTP POST Request__: A combination of request header and request body, sent to the ESP32. The header and body are separated by a blank line.
* __Request Header__: Your browser creates most of this.  Important options for this application are:
  * __Request Type__: POST. Needed to avoid CORS restrictions and to contain enough information for the ESP32.
  * __Content-Type__: text/plain. Needed to avoid CORS restrictions.
* __Request Body__: One or more lines of text, following the format described above.
* __Lines__: In this application, each line in the request body consists of multiple text values, separated by commas. All of the numbers transmitted to the ESP32 are text representations of decimal numbers. Each line is terminated by either:
  * a newline (\n), or
  * a return (\r) and a newline (\n), or
  * the end of the request body
* __CORS__: Cross-Orgin-Resource-Sharing. Keeps your compliant browser from fetching information from a different server than the original web page was served from. This is an important security feature.
