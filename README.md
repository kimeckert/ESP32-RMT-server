# ESP32-RMT-server
WiFi server that drives low-level commands to the RMT peripheral

__NOTE: Construction in progress__

Receives an HTTP POST message and decodes commands contained in the message body.
These commands control the RMT peripheral and drive an infrared LED.
The commands are low-level mark-and-space commands.
There is no protocol decode implemented in this code.
This allows the RMT to transmit virtually any infrared protocol.
Compiles with ESP-IDF.

* The HTTP POST message consists of one or more text lines. Lines are separated by newline (\n) characters. Returns (\r) are allowed, but are ignored.
* Each line consists of comma-separated values. Each line starts with one character that identifies the type of data contained on the remainder of the line.

# Commands

The commands closely follow the RMT register/RAM definitions:
* __c,[div],[high,low]__
  * Sets the channel clock frequency and the carrier clock frequency and duty cycle.
  * __[div]__: div_cnt, RMT counter clock divider. Text representation of an integer between 1 and 255, inclusive.
    * Defines the RMT clock division ratio.  The channel clock is divided from source clock.
  * __[high]__ and __[low]__: High and low duration of the carrier. Text representation of an integer between 1 and 65,535, inclusive.
    * Set different values for carrier\_high and carrier\_low to set different frequency of carrier. The unit of carrier_high/low is the source clock tick, not the divided channel counter clock.
* __t,[non-zero duration], ... ,0__
  * Defines a sequence of durations of IR transmission bits, both modulated at the carrier frequency (mark) and idle (space).
  * __[non-zero duration]__: A text representation of a non-zero integer between -32,767 and 32,767, inclusive. Duration of the transmit burst. A zero value denotes the end of the transmission sequence.
    * Positive integers create a burst of IR output at the carrier frequency, with duration of [non-zero-duration] channel clock periods.
    * Negative integers create a duration on the IR output with no carrier.
    * Positive and negative values do not need to be alternated. Either positive or negative can follow a positive or negative value.
  * A zero value is the end of the RMT sequence.
  * The RMT RAM can store a maximum of 128 duration values. If more than 128 values are entered, the driver implements wrap-around mode to transmit the longer sequence.
* __d,[milliseconds delay]__
  * Creates an idle gap between consecutive transmission sequences.
  * __[milliseconds delay]__: Create a delay before sending the remaining IR tramsmissions. Text representation of a positive non-negative integer of the number of milliseconds for the delay.  Implemented with the FreeRTOS function:
    * vTaskDelay( milliseconds delay / portTICK_PERIOD_MS ); and has the limitations of that function.

# Definitions
* __HTTP POST Request__: A combination of request header and request body. These are separated by a blank line.
* __Request Header__: Your browser creates most of this.  Important options for this application are:
  * __Request Type__: POST. Needed to avoid CORS restrictions and to contain enough information for the ESP32.
  * __Content-Type__: text/plain. Needed to avoid CORS restrictions.
* __Request Body__: One or more lines of text.
* __Lines__: Zero or more non-return characters, terminated by a newline (\n), a return (\r) and a newline (\n) or the end of the body. In this application, each line consists of multiple values, separated by commas. All of the numbers transmitted to the ESP32 are text representations of decimal numbers.
* __CORS__: Cross-Orgin-Resource-Sharing. Keeps your compliant browser from fetching information from a different server than the original web page was served from. This is an important security feature.
