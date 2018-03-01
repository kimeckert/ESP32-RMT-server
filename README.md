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
  * __[div]__: div_cnt, RMT counter clock divider. Integer between 1 and 255, inclusive.
    * Defines the RMT clock division ratio.  The channel clock is divided from source clock.
  * __[high]__ and __[low]__: High and low duration of the carrier. Integer between 1 and 65,535, inclusive.
    * Set different values for carrier\_high and carrier\_low to set different frequency of carrier. The unit of carrier_high/low is the source clock tick, not the divided channel counter clock.
* __t,[non-zero duration], ... ,0__
  * __[non-zero duration]__: Non-zero integer between -32,767 and 32,767, inclusive. Duration of the transmit burst. A zero value denotes the end of the transmission sequence.
    * Positive integers create a burst of IR output at the carrier frequency, with duration of [non-zero-duration] channel clock periods.
    * Negative integers create a duration on the IR output with no carrier.
    * Positive and negative values do not need to be alternated. Either positive or negative can follow a positive or negative value.
  * A zero value is the end of the RMT sequence.
  * The RMT RAM can store a maximum of 128 duration values. If more than 128 values are entered, the driver implements wrap-around mode to transmit the longer sequence.
* __d,[milliseconds delay]__
  * __[milliseconds delay]__: Create a delay before sending the remaining IR tramsmissions.
