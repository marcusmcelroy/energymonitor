#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "rom/ets_sys.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
// #include "freertos/semphr.h"
// #include "freertos/queue.h"

// #include "lwip/sockets.h"
// #include "lwip/dns.h"
// #include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "esp_sleep.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc_periph.h"
#include "esp32/ulp.h"
#include "ulp_main.h"

#include "secrets.h"

static const char *TAG = "PULSECOUNTER";

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

struct node {
   int key;
   struct node *next;
};

struct node *head = NULL;
struct node *current = NULL;

//insert link at the first location
void insertFirst(int key) {
   //create a link
   struct node *link = (struct node*) malloc(sizeof(struct node));
	
   link->key = key;
	
   //point it to old first node
   link->next = head;
	
   //point first to new first node
   head = link;
}

//delete a link with given key
struct node* delete(int key) {

   //start from the first link
   struct node* current = head;
   struct node* previous = NULL;
	
   //if list is empty
   if(head == NULL) {
      return NULL;
   }

   //navigate through list
   while(current->key != key) {

      //if it is last node
      if(current->next == NULL) {
         return NULL;
      } else {
         //store reference to current link
         previous = current;
         //move to next link
         current = current->next;
      }
   }

   //found a match, update the link
   if(current == head) {
      //change first to point to next link
      head = head->next;
   } else {
      //bypass the current link
      previous->next = current->next;
   }    
	
   return current;
}

//is list empty
bool isEmpty() {
   return head == NULL;
}

void wait_for_all_messages_to_be_published(void) {
   int retry = 0;
   const int retry_count = 20;
   // isEmpty() here is checking how many items in the "pending message" list
   while (!isEmpty() && ++retry < retry_count) {
      // Under normal conditions, even 100ms is enough wait time for one message
      vTaskDelay(100 / portTICK_PERIOD_MS);
   }
}

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */

#define MAXIMUM_RETRY 5

//#define MAX_RETRY 10
//uint32_t WIFI_READY = 0;
//static int retry_cnt = 0;



esp_mqtt_client_handle_t client = NULL;
//uint32_t MQTT_CONNECTED = 0;

// Functions in mqqt.c
void mqtt_app_start(void);
void mqtt_app_publish(char* topic, char *publish_string);

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        //     /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
        //      * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
        //      * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	    //  * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
        //      */
        //     .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        //     .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 WIFI_SSID, WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 WIFI_SSID, WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static void init_ulp_program(void)
{
    esp_err_t err = ulp_load_binary(0, ulp_main_bin_start,
            (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    ESP_ERROR_CHECK(err);

    /* GPIO used for pulse counting. */
    gpio_num_t gpio_num = GPIO_NUM_4;
    int rtcio_num = rtc_io_number_get(gpio_num);
    assert(rtc_gpio_is_valid_gpio(gpio_num) && "GPIO used for pulse counting must be an RTC IO");

    /* Initialize some variables used by ULP program.
     * Each 'ulp_xyz' variable corresponds to 'xyz' variable in the ULP program.
     * These variables are declared in an auto generated header file,
     * 'ulp_main.h', name of this file is defined in component.mk as ULP_APP_NAME.
     * These variables are located in RTC_SLOW_MEM and can be accessed both by the
     * ULP and the main CPUs.
     *
     * Note that the ULP reads only the lower 16 bits of these variables.
     */
 //   ulp_counter = 0;
 //   ulp_debounce_counter = 0;
 //   ulp_debounce_max_count = 0;
    ulp_next_edge = 0;
    ulp_io_number = rtcio_num; /* map from GPIO# to RTC_IO# */
    // ulp_edge_count_to_wake_up = 10;

    /* Initialize selected GPIO as RTC IO, enable input, disable pullup and pulldown */
    rtc_gpio_init(gpio_num);
    rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(gpio_num);
    rtc_gpio_pullup_dis(gpio_num);
    rtc_gpio_hold_en(gpio_num);

#if CONFIG_IDF_TARGET_ESP32
    /* Disconnect GPIO12 and GPIO15 to remove current drain through
     * pullup/pulldown resistors.
     * GPIO12 may be pulled high to select flash voltage.
     */
    rtc_gpio_isolate(GPIO_NUM_12);
    rtc_gpio_isolate(GPIO_NUM_15);
#endif // CONFIG_IDF_TARGET_ESP32  

    esp_deep_sleep_disable_rom_logging(); // suppress boot messages

    /* Set ULP wake up period to T = 10ms.
     * Minimum pulse width has to be T * (ulp_debounce_counter + 1) = 10ms.
     */
    ulp_set_wakeup_period(0, 10000);

    /* Start the program */
    err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
    ESP_ERROR_CHECK(err);
}

int update_pulse_count(void)
{
    // const char* namespace = "plusecnt";
    // const char* count_key = "count";

    // ESP_ERROR_CHECK( nvs_flash_init() );
    // nvs_handle_t handle;
    // ESP_ERROR_CHECK( nvs_open(namespace, NVS_READWRITE, &handle));
    // uint32_t pulse_count = 0;
    // esp_err_t err = nvs_get_u32(handle, count_key, &pulse_count);
    // assert(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);
    // printf("Read pulse count from NVS: %5d\n", pulse_count);

    /* ULP program counts signal edges, convert that to the number of pulses */
    uint32_t pulse_count_from_ulp = (ulp_edge_count & UINT16_MAX) / 2;
    /* In case of an odd number of edges, keep one until next time */
    ulp_edge_count = ulp_edge_count % 2;
    return(pulse_count_from_ulp);
    // printf("Pulse count from ULP: %5d\n", pulse_count_from_ulp);

    // /* Save the new pulse count to NVS */
    // pulse_count += pulse_count_from_ulp;
    // ESP_ERROR_CHECK(nvs_set_u32(handle, count_key, pulse_count));
    // ESP_ERROR_CHECK(nvs_commit(handle));
    // nvs_close(handle);
    // printf("Wrote updated pulse count to NVS: %5d\n", pulse_count);
}

void app_main()
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_TIMER) {
        printf("Initializing ULP\n");
        init_ulp_program();  
    } 
    
    nvs_flash_init();
    wifi_init_sta(); 

    mqtt_app_start();

    int pulses = update_pulse_count();
    
    char msg_out[16];
    sprintf(msg_out, "%d", pulses);

    // unsigned char tmp[2];
    // tmp[0] = (u_int8_t) (pulses >> 8) & 0xFF;
    // tmp[1] = (u_int8_t) (pulses & 0xFF);
  
    mqtt_app_publish(MQTT_TOPIC, msg_out);
    wait_for_all_messages_to_be_published();

    printf("Entering deep sleep\n\n");
    //mqqt_app_stop();
    //ESP_ERROR_CHECK(esp_wifi_stop() );

    ESP_ERROR_CHECK( esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR));
    esp_deep_sleep_start();
}