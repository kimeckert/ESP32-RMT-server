/*
	RMT transmit control over http server.
	* 
	* HTTP server code modified from
	* https://github.com/feelfreelinux/myesp32tests/blob/master/examples/http_server.c
	* 
	* RMT code modified from example: rmt_nec_tx_rx
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/rmt.h"

#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"

// FreeRTOS function
#define INCLUDE_vTaskDelay 1

// HTTP responses
const static char http_html_Accepted[] = "HTTP/1.1 202 Accepted\r\nConnection: close\r\n\r\n";
const static char http_html_Not_Allowed[] = "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n";

// LED on Huzzah32 board
const int LED_BUILTIN = 13;

// used during debug
const static char http_debug1[] = "HTTP/1.1 202 Accepted\r\nContent-type: text/html\r\n\r\n";
const static char http_debug2[] = "<html><head><title>ESP32</title></head><body><pre>";
const static char http_debug3[] = "</pre></body></html>";

// how to connect to my local WiFi
#define WIFI_SSID "ATTxey7ywa"
#define WIFI_PASS "ey7azf7x76vv"

// RMT values
#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define RMT_TX_GPIO    GPIO_NUM_26
// channel clock period = 1 uS
#define RMT_CLK_DIV    80

// WIFI values
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

// used when parsing integers from character arrays
typedef struct {
	int num;
	bool good;
} returnIntVal;

// WiFi event handler
static esp_err_t event_handler(void *ctx, system_event_t *event) {
	switch(event->event_id) {
	case SYSTEM_EVENT_STA_START:
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		/* This is a workaround as ESP32 WiFi libs don't currently
			auto-reassociate. */
		esp_wifi_connect();
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
		break;
	default:
		break;
	}
	return ESP_OK;
}

// initialize WiFi
static void initialise_wifi(void) {
	tcpip_adapter_init();
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK( esp_event_loop_init( event_handler, NULL ) );
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = WIFI_SSID,
			.password = WIFI_PASS,
		},
	};
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_STA, &wifi_config ) );
	ESP_ERROR_CHECK( esp_wifi_start() );
	printf("WiFi initialized\n");
}

// initialize RMT peripheral for output
// Note that some of these settings can be modified during operation
static void rmt_tx_init() {
	rmt_config_t rmt_tx;
	rmt_tx.rmt_mode = RMT_MODE_TX;
	rmt_tx.channel  = RMT_TX_CHANNEL;
	rmt_tx.gpio_num = RMT_TX_GPIO;
	rmt_tx.mem_block_num = 1;
	rmt_tx.clk_div = RMT_CLK_DIV;
	rmt_tx.tx_config.loop_en = false;
	rmt_tx.tx_config.carrier_duty_percent = 50;
	rmt_tx.tx_config.carrier_freq_hz = 40000;
	rmt_tx.tx_config.carrier_level   = RMT_CARRIER_LEVEL_HIGH;
	rmt_tx.tx_config.carrier_en      = true;
	rmt_tx.tx_config.idle_level      = RMT_IDLE_LEVEL_LOW;
	rmt_tx.tx_config.idle_output_en  = true;
	
	ESP_ERROR_CHECK( rmt_config(&rmt_tx) );
	ESP_ERROR_CHECK( rmt_driver_install(rmt_tx.channel, 0, 0) );
	printf("RMT initialized\n");
}

// find the start of the request body, which follows a blank line
// a blank line is created by two consecutive \n's, ignoring \r's
// more than two consecutive newlines are accepted
// returns the index of the character following the newlines
u16_t find_body( char* buf, u16_t length ) {
	printf("Finding POST body, buffer size %d\n", length);
	// counts number of consecutive newlines
	u16_t newlines = 0;
	// character pointer
	u16_t this_char;
	// the response starts with 'POST ', so start at character 5
	for ( this_char=5; this_char<=length; this_char++ ) {
		if ( buf[this_char] == '\r' ) { continue; }
		if ( buf[this_char] == '\n' ) { newlines++; }
		// only received a single newline, reset counter
		else if ( newlines == 1 ) { newlines = 0; }
		if ( ( newlines > 1 ) && ( buf[this_char] != '\n' ) ) {
			printf("Found body start %d\n", this_char);
			return this_char;
		}
	}
	// body not found
	return 0;
}

// push item onto rmt_item32_t array
// value_index points to: duration0/level0, duration1/level1, ...
//       value_index: 0 1 2 3 4 5 6 7
//   value_index / 2: 0 0 1 1 2 2 3 3
//       items index: 0 0 1 1 2 2 3 3
void pushItem( rmt_item32_t* items, u16_t value_index, bool level, u16_t duration ) {
	u16_t i = value_index / 2;
	// lower 16 bits of the 32-bit item
	if ( value_index % 2 == 0 ) {
		items[i].level0 = level;
		items[i].duration0 = duration;
		// just in case this is the last item, fill up the next entry
		items[i].level1 = level;
		items[i].duration1 = 0;
	}
	// upper 16 bits of the 32-bit item
	else {
		items[i].level1 = level;
		items[i].duration1 = duration;
	}
	printf("Item pushed to RMT data RAM: level %d, duration %u\n", level, duration);
}

// set the clock divisor and carrier clock high/low durations
// set during the decode of RMT data
void set_rmt_frequency( uint16_t div, uint16_t high, uint16_t low ) {
	printf("Set frequency %i, %i, %i\n", div, high, low);
	
	// if clock divisor changes, set to new value
	uint8_t previous_div;
	rmt_get_clk_div( RMT_TX_CHANNEL, &previous_div );
	printf("previous div: %i\n", previous_div);
	if ( (uint8_t) div != previous_div ) {
		rmt_set_clk_div( RMT_TX_CHANNEL, (uint8_t) div );
		printf("set div = %i\n", div);
	}
	
	// set clock high and low times
	// args: channel, carrier_en, high_level, low_level, carrier_level
	rmt_set_tx_carrier( RMT_TX_CHANNEL, true, high, low, RMT_CARRIER_LEVEL_HIGH );
	printf("Reset RMT frequency\n");
}

// returns the number of comma-separated fields in this line,
// including the first field, the line type designation
u16_t count_values( char* buf, u16_t start, u16_t end ) {
	u16_t count = 0;
	u16_t pointer;
	for ( pointer=start; pointer<=end; pointer++ ) {
		if ( buf[pointer] == ',' ) { count++; }
	}
	return count + 1;
}

// get the n-th field from the comma-separated line
// the first numerical field is index = 0
returnIntVal get_number( char* buf, u16_t start, u16_t end, int count ) {
	returnIntVal n;
	n.num = 0;
	n.good = true;
	bool is_neg = false;
	u16_t this_char = start;
	u16_t commas = 0;
	printf("get_number: look for %d\n", count);
	
	// count values separated by commas
	if ( count > 0 ) {
		while( this_char++ <= end ) {
			if ( buf[this_char] == ',' ) { commas++; }
			if ( commas == count ) { break; }
		}
		// point to the character after the comma
		this_char++;
	}
	
	// did not find the value
	if ( commas < count ) {
		printf("get_number not found\n");
		n.good = false;
		return n;
	}
	
	// is the number negative?
	if ( buf[this_char] == '-' ) {
		is_neg = true;
		this_char++;
	}
	
	// get numerical characters
	while( (this_char <= end) && 
		(buf[this_char]>47) && 
		(buf[this_char]<58 ) ) {
		n.num = ( 10 * n.num ) + ( buf[this_char] - 48 );
		this_char++;
	}
	
	// return
	if ( is_neg ) { n.num = -1 * n.num; }
	// printf("get_number found %d\n", n.num);
	return n;
}

// write frequency and clock data to the RMT peripheral
void send_freq( char* buf, u16_t start, u16_t end ) {
	// frequency settings: [Division Ratio, High Duration, Low Duration]
	int freq[3];
	bool good = true;
	returnIntVal n;
	printf("send_freq %d to %d\n", start, end);
	
	// division ratio, 8-bit register
	n = get_number( buf, start, end, 0 );
	if ( (n.good) && (n.num>0) && (n.num<256) ) {
		freq[0] = n.num;
		// printf("get_num 0 %d\n", n.num);
	}
	else { good = false; }
	
	// clock high duration, 16-bit register
	n = get_number( buf, start, end, 1 );
	if ( (n.good) && (n.num>0) && (n.num<65536) ) {
		freq[1] = n.num;
		// printf("get_num 1 %d\n", n.num);
	}
	else { good = false; }
	
	// clock low duration, 16-bit register
	n = get_number( buf, start, end, 2 );
	if ( (n.good) && (n.num>0) && (n.num<65536) ) {
		freq[2] = n.num;
		// printf("get_num 2 %d\n", n.num);
	}
	else { good = false; }
	
	// set register values
	if ( good ) {
		set_rmt_frequency( freq[0], freq[1], freq[2] );
		//printf("Set frequency %d, %d, %d\n", freq[0], freq[1], freq[2]);
	}
}

// write duration data to the RMT RAM
void send_duration( char* buf, u16_t start, u16_t end ) {
	u16_t count, c;
	returnIntVal val;
	bool good = true;
	
	// the number of comma-separated numeric values on this line
	count = count_values(buf, start, end);
	
	// each 32-bit RMT item holds 2 duration/level values
	// divide count of numerical values by 2 get required number of 32-bit values
	// multiply by 4 to get bytes
	//            count: 0 1 2 3 4  5  6  7  8
	//        count + 1: 1 2 3 4 5  6  7  8  9
	//  (count + 1) / 2: 0 1 1 2 2  3  3  4  4
	//    RMT RAM items: 0 1 1 2 2  3  3  4  4
	//     malloc bytes: 0 4 4 8 8 12 12 16 16
	
	// number of bytes in the 32-bit RMT item array
	size_t size = 4 * ( ( count + 1 ) / 2 );
	rmt_item32_t* items = (rmt_item32_t*) malloc(size);
	printf("Row has %i values, allocated %i bytes\n", count, size);
	printf("Starts with %c, ends with %c\n", buf[start], buf[end]);
	
	// create an object for RMT RAM values
	for ( c=0; c<count; c++ ) {
		val = get_number( buf, start, end, c );
		if ( val.good ) {
			// negative values create no RMT output pulses
			if ( val.num < 0 ) {
				pushItem( items, c, false, -1*val.num );
			}
			// positive values create RMT output pulses
			else {
				pushItem( items, c, true, val.num );
			}
		}
		// cannot write this value to RMT RAM
		else { good = false; }
	}
	if ( good ) {
		// args: channel, rmt_item, item_num wait_tx_done
		rmt_write_items(RMT_TX_CHANNEL, items, size, true);
		printf("Write %d durations\n", count);
	}
	else { printf("ERROR in transmit data\n"); }
	// free up memory
	free( items );
}

// FreeRTOS constant portTICK_PERIOD_MS
// example usage: vtaskDelay( 500 / portTICK_PERIOD_MS )
void delay_task( char* buf, u16_t start, u16_t end ) {
	returnIntVal val;
	val = get_number( buf, start, end, 0 );
	if ( ( val.good ) && ( val.num > 0 ) ) {
		vTaskDelay( val.num / portTICK_PERIOD_MS );
		printf("Delay %d\n", val.num);
	}
}

// get lines of RMT commands from POST request
// start is first valid character of the request
// end is the last character of the request
void get_request_line( char* buf, u16_t start, u16_t end ) {
	u16_t line_start;
	u16_t this_char = start;
	line_start = start;
	while ( this_char <= end ) {
		// found end of line, decode it
		if ( ( buf[this_char] == '\r' ) || 
			 ( buf[this_char] == '\n' ) || 
			 ( this_char == end ) ) {
			printf("Line %d to %d\n", line_start, this_char);
			
			// ignore newline character at end of this line
			if ( ( buf[this_char] == '\r' ) || ( buf[this_char] == '\n' ) ) {
				this_char--;
			}
			// is there anything to decode?
			if ( ( this_char - line_start ) > 1 ) {
				if ( ( buf[line_start] == 'c' ) && ( buf[line_start+1] == ',' ) ) {
					printf("clock\n");
					// send frequency data to the RMT peripheral
					send_freq( buf, line_start+2, this_char );
				}
				else if ( ( buf[line_start] == 't' ) && ( buf[line_start+1] == ',' ) ) {
					printf("transmit\n");
					// turn on the visible LED
					gpio_set_level(LED_BUILTIN,1);
					
					// send duration data to the RMT peripheral
					send_duration( buf, line_start+2, this_char );
					
					gpio_set_level(LED_BUILTIN,0);
				}
				else if ( ( buf[line_start] == 'd' ) && ( buf[line_start+1] == ',' ) ) {
					printf("delay\n");
					// delay this task
					delay_task( buf, line_start+2, this_char );
				}
				else {
					// do not recognize this line
					printf("unknown line\n");
				}
			}
			// advance to the character AFTER the end of the line
			if ( this_char < end ) { this_char++; }
			
			// skip possible multiple \r and \n
			while ( this_char <= end && 
				( buf[this_char] == '\r' || buf[this_char] == '\n' ) ) {
				this_char++;
			}
			line_start = this_char;
		}
		this_char++;
	}
}

// Process an HTTP POST request
static void http_server_netconn_serve(struct netconn *conn) {
	struct netbuf *inbuf;
	char *buf;
	u16_t buflen;
	u16_t rmt_start;
	err_t err;
	
	// Read the data from the port, blocking if nothing yet there.
	err = netconn_recv(conn, &inbuf);
	
	printf("Start looking for POST request\n");
	
	if (err == ERR_OK) {
		netbuf_data(inbuf, (void**)&buf, &buflen);
		
		// Is this an HTTP POST command?
		if ( buflen>5 && 
			buf[0]=='P' && buf[1]=='O' && buf[2]=='S' && buf[3]=='T' ) {
			
			printf("Decoding POST request\n");
			
			// process the POST request
			rmt_start = find_body( buf, buflen );
			if ( rmt_start > 0 ) {
				get_request_line( buf, rmt_start, buflen );
			}
			
			// HTTP Response to POST request
			netconn_write(conn, http_html_Accepted, sizeof(http_html_Accepted)-1, NETCONN_NOCOPY);
			
			// HTTP Debug response, instead of the normal response, echoes the entire request
			//netconn_write(conn, http_debug1, sizeof(http_debug1)-1, NETCONN_NOCOPY);
			//netconn_write(conn, http_debug2, sizeof(http_debug1)-1, NETCONN_NOCOPY);
			//netconn_write(conn, buf, buflen, NETCONN_NOCOPY);
			//netconn_write(conn, http_debug3, sizeof(http_debug1)-1, NETCONN_NOCOPY);
			
			// gpio_set_level(LED_BUILTIN,0);
		}
		// do not accept non-POST requests
		else {
			netconn_write(conn, http_html_Not_Allowed, sizeof(http_html_Not_Allowed)-1, NETCONN_NOCOPY);
		}
	}
	// Close the connection
	netconn_close(conn);
	
	// Delete the buffer
	netbuf_delete(inbuf);
}

// HTTP server
static void http_server(void *pvParameters) {
	struct netconn *conn, *newconn;
	err_t err;
	conn = netconn_new(NETCONN_TCP);
	netconn_bind(conn, NULL, 80);
	netconn_listen(conn);
	do {
		err = netconn_accept(conn, &newconn);
		if (err == ERR_OK) {
			http_server_netconn_serve(newconn);
			netconn_delete(newconn);
		}
	} while(err == ERR_OK);
	netconn_close(conn);
	netconn_delete(conn);
}

void app_main() {
	// set board built-in LED as an output
	gpio_pad_select_gpio(LED_BUILTIN);
	gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);
	
	// Initialize NVS flash storage
    nvs_flash_init();
    
    // Initialize the RMT peripheral for output
    rmt_tx_init();
    
    // Initialize WiFi
    initialise_wifi();
    
    // HTTP server task
    xTaskCreate(&http_server, "http_server", 2048, NULL, 5, NULL);
}
