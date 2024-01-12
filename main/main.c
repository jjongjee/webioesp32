/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "driver/gpio.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#if !CONFIG_IDF_TARGET_LINUX
#include <esp_wifi.h>
#include <esp_system.h>
#include "nvs_flash.h"
#include "esp_eth.h"
#endif  // !CONFIG_IDF_TARGET_LINUX

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */
static const char *TAG = "webio";

#if CONFIG_IDF_TARGET_ESP32
    #define GPIOCOUNT 49
static const char *DEVICEINFO = "[\"esp32\",\"s3\",\"1.0\"]";
static const char *PINOUT = "[\"V33\",\"GND\",\"V33\",43,\"RST\",44,4,1,5,2,6,42,7,41,15,40,16,39,17,38,18,37,8,36,3,35,46,0,9,45,10,48,11,47,12,21,13,20,14,19,\"V50\",\"GND\",\"GND\",\"GND\"]";
#elif CONFIG_IDF_TARGET_ESP32C2
    #define GPIOCOUNT 21
static const char *DEVICEINFO = "[\"esp32\",\"c2\",\"1.0\"]";
static const char *PINOUT = "[\"V33\",\"GND\",\"V33\",43,\"RST\",44,4,1,5,2,6,42,7,41,15,40,16,39,17,38,18,37,8,36,3,35,46,0,9,45,10,48,11,47,12,21,13,20,14,19,\"V50\",\"GND\",\"GND\",\"GND\"]";
#elif CONFIG_IDF_TARGET_ESP32C3
    #define GPIOCOUNT 22
static const char *DEVICEINFO = "[\"esp32\",\"c3\",\"1.0\",24,22]";
static const char *PINOUT = "[0,20,1,21,2,19,3,18,4,10,5,9,6,8,7,\"RST\",\"V50\",\"V33\",\"V50\",\"V33\",\"GND\",\"GND\",\"GND\",\"GND\"]";
static const char GPIOBMP[] = {1,1,1,1,1, 1,1,1,1,1, 1,0,0,0,0, 0,0,0,0,0, 1,1};
#elif CONFIG_IDF_TARGET_ESP32C6
    #define GPIOCOUNT 31
static const char *DEVICEINFO = "[\"esp32\",\"c6\",\"1.0\"]";
static const char *PINOUT = "[\"V33\",\"GND\",\"V33\",43,\"RST\",44,4,1,5,2,6,42,7,41,15,40,16,39,17,38,18,37,8,36,3,35,46,0,9,45,10,48,11,47,12,21,13,20,14,19,\"V50\",\"GND\",\"GND\",\"GND\"]";
#elif CONFIG_IDF_TARGET_ESP32S3
    #define GPIOCOUNT 49
static const char *DEVICEINFO = "[\"esp32\",\"s3\",\"1.0\",44,49]";
static const char *PINOUT = "[\"V33\",\"GND\",\"V33\",43,\"RST\",44,4,1,5,2,6,42,7,41,15,40,16,39,17,38,18,37,8,36,3,35,46,0,9,45,10,48,11,47,12,21,13,20,14,19,\"V50\",\"GND\",\"GND\",\"GND\"]";
static const char GPIOBMP[] = {1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,0, 0,1,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 1,1,1,1,1, 1,1,0,0,1, 1,1,1,1};
#endif

static char GPIODIR[GPIOCOUNT];
static char GPIOVAL[GPIOCOUNT];


static void gpio_init_all(void);



void gpio_init_all(void)
{
    int i;


    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;

    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = 0;


    for (i = 0; i < GPIOCOUNT; i++)
    {
        if (GPIOBMP[i] == 0) continue;

        io_conf.pin_bit_mask |= (1ULL << i);
        GPIODIR[i] = GPIO_MODE_INPUT;
        GPIOVAL[i] = 0;
    }


    //disable pull-down mode
    io_conf.pull_down_en = 1;
    //disable pull-up mode
    //io_conf.pull_up_en = 1;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
}


/* An HTTP GET handler */
static esp_err_t gpio_html_get_handler(httpd_req_t *req)
{
    extern const unsigned char gpio_html_start[] asm("_binary_gpio_html_start");
    extern const unsigned char gpio_html_end[]   asm("_binary_gpio_html_end");
    const size_t gpio_html_size = (gpio_html_end - gpio_html_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)gpio_html_start, gpio_html_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static const httpd_uri_t gpio_html = {
    .uri       = "/gpio.html",
    .method    = HTTP_GET,
    .handler   = gpio_html_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};


/* An HTTP GET handler */
static esp_err_t gpio_getall_get_handler(httpd_req_t *req)
{
    int i;
    bool is_first;
    char json_txt[1024];
    char gpio_ln[64];

    httpd_resp_set_type(req, HTTPD_TYPE_JSON);

    json_txt[0] = 0;
    strcat(json_txt, "{\"GPIO\":{");


    is_first = true;

    for (i = 0; i < GPIOCOUNT; i++)
    {
        if (GPIOBMP[i] == 0) continue;
        if (GPIOBMP[i] == (char)(-1)) break;


        if (GPIODIR[i] == GPIO_MODE_INPUT)
        {
            GPIOVAL[i] = gpio_get_level(i);
        }

        if (is_first)
        {
            sprintf(gpio_ln, "\"%d\": {\"function\": \"%s\", \"value\": %d}",
                    i, 
                    ((GPIODIR[i] == GPIO_MODE_OUTPUT) ? "OUT" : "IN"),
                    GPIOVAL[i]);
            
            is_first = false;
        }
        else
        {
            sprintf(gpio_ln, ",\"%d\": {\"function\": \"%s\", \"value\": %d}",
                    i, 
                    ((GPIODIR[i] == GPIO_MODE_OUTPUT) ? "OUT" : "IN"),
                    GPIOVAL[i]);
        }

        strcat(json_txt, gpio_ln);

        if (i && ((i % 10) == 0))
        {
            httpd_resp_send_chunk(req, json_txt, strlen(json_txt));

            json_txt[0] = 0;
        }
    }

    strcat(json_txt, "}}");

    httpd_resp_send_chunk(req, json_txt, strlen(json_txt));
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static const httpd_uri_t gpio_getall = {
    .uri       = "/gpio_getall",
    .method    = HTTP_GET,
    .handler   = gpio_getall_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};


/* An HTTP GET handler */
static esp_err_t gpio_ctrl_get_handler(httpd_req_t *req)
{
    char   retstr[128];
    int    portnum;
    int    portval;
    char*  buf;
    size_t buf_len;


    retstr[0] = 0;
    portnum = -1;
    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[EXAMPLE_HTTP_QUERY_KEY_MAX_LEN], dec_param[EXAMPLE_HTTP_QUERY_KEY_MAX_LEN] = {0};
            /* Get value of expected key from query string */
 
            memset(dec_param, 0, sizeof(dec_param));
            if (httpd_query_key_value(buf, "port", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => port=%s", param);
                example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
                
                portnum = atoi(param);
                if ( ! GPIO_IS_VALID_OUTPUT_GPIO(portnum))
                {
                    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
                    httpd_resp_send(req, "GPIO number error", HTTPD_RESP_USE_STRLEN);

                    return ESP_OK;
                }

                if ( ! GPIOBMP[portnum])
                {
                    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
                    httpd_resp_send(req, "GPIO valid error", HTTPD_RESP_USE_STRLEN);

                    return ESP_OK;
                }
            }
 
            memset(dec_param, 0, sizeof(dec_param));
            if (httpd_query_key_value(buf, "func", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => func=%s", param);
                example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
                sprintf(retstr, "%s", dec_param);

                if ( ! GPIO_IS_VALID_OUTPUT_GPIO(portnum))
                {
                    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
                    httpd_resp_send(req, "GPIO number error", HTTPD_RESP_USE_STRLEN);

                    return ESP_OK;
                }

                if (param[0] == 'O')
                {
                    gpio_set_direction(portnum, GPIO_MODE_OUTPUT);
                    GPIODIR[portnum] = GPIO_MODE_OUTPUT;
                    GPIOVAL[portnum] = 0;
                    //gpio_pullup_dis(portnum);
                    //gpio_pulldown_en(portnum);
                }
                else
                {
                    gpio_set_direction(portnum, GPIO_MODE_INPUT);
                    GPIODIR[portnum] = GPIO_MODE_INPUT;
                    //gpio_pullup_en(portnum);
                    //gpio_pulldown_dis(portnum);
                }
            }

            memset(dec_param, 0, sizeof(dec_param));
            if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => value=%s", param);
                example_uri_decode(dec_param, param, strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_LEN));
                ESP_LOGI(TAG, "Decoded query parameter => %s", dec_param);
                sprintf(retstr, "%s", dec_param);
                portval = atoi(param);

                if ( ! GPIO_IS_VALID_OUTPUT_GPIO(portnum))
                {
                    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
                    httpd_resp_send(req, "GPIO number error", HTTPD_RESP_USE_STRLEN);

                    return ESP_OK;
                }

                gpio_set_level(portnum, portval);

                GPIOVAL[portnum] = portval;
            }
        }
        free(buf);
    }

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, retstr, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static const httpd_uri_t gpio_ctrl = {
    .uri       = "/gpio_ctrl",
    .method    = HTTP_GET,
    .handler   = gpio_ctrl_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};


/* An HTTP GET handler */
static esp_err_t deviceinfo_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_send(req, DEVICEINFO, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static const httpd_uri_t deviceinfo = {
    .uri       = "/deviceinfo",
    .method    = HTTP_GET,
    .handler   = deviceinfo_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};


/* An HTTP GET handler */
static esp_err_t pinout_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_send(req, PINOUT, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static const httpd_uri_t pinout = {
    .uri       = "/pinout",
    .method    = HTTP_GET,
    .handler   = pinout_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};

/* An HTTP GET handler */
static esp_err_t jquery_js_get_handler(httpd_req_t *req)
{
    extern const unsigned char jquery_js_start[] asm("_binary_jquery_js_start");
    extern const unsigned char jquery_js_end[]   asm("_binary_jquery_js_end");
    const size_t jquery_js_size = (jquery_js_end - jquery_js_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)jquery_js_start, jquery_js_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static const httpd_uri_t jquery_js = {
    .uri       = "/jquery.js",
    .method    = HTTP_GET,
    .handler   = jquery_js_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};


/* An HTTP GET handler */
static esp_err_t webioesp_css_get_handler(httpd_req_t *req)
{
    extern const unsigned char webioesp_css_start[] asm("_binary_webioesp_css_start");
    extern const unsigned char webioesp_css_end[]   asm("_binary_webioesp_css_end");
    const size_t webioesp_css_size = (webioesp_css_end - webioesp_css_start);


    httpd_resp_set_type(req, "text/css");

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)webioesp_css_start, webioesp_css_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static const httpd_uri_t webioesp_css = {
    .uri       = "/webioesp.css",
    .method    = HTTP_GET,
    .handler   = webioesp_css_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};


/* An HTTP GET handler */
static esp_err_t webioesp_js_get_handler(httpd_req_t *req)
{
    extern const unsigned char webioesp_js_start[] asm("_binary_webioesp_js_start");
    extern const unsigned char webioesp_js_end[]   asm("_binary_webioesp_js_end");
    const size_t webioesp_js_size = (webioesp_js_end - webioesp_js_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)webioesp_js_start, webioesp_js_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static const httpd_uri_t webioesp_js = {
    .uri       = "/webioesp.js",
    .method    = HTTP_GET,
    .handler   = webioesp_js_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};



/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_LINUX
    // Setting port as 8001 when building for Linux. Port 80 can be used only by a priviliged user in linux.
    // So when a unpriviliged user tries to run the application, it throws bind error and the server is not started.
    // Port 8001 can be used by an unpriviliged user as well. So the application will not throw bind error and the
    // server will be started.
    config.server_port = 8001;
#endif // !CONFIG_IDF_TARGET_LINUX
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &jquery_js);
        httpd_register_uri_handler(server, &webioesp_css);
        httpd_register_uri_handler(server, &webioesp_js);
        httpd_register_uri_handler(server, &gpio_html);

        httpd_register_uri_handler(server, &deviceinfo);
        httpd_register_uri_handler(server, &pinout);
        httpd_register_uri_handler(server, &gpio_getall);
        httpd_register_uri_handler(server, &gpio_ctrl);
        #if CONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(server);
        #endif
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}
#endif // !CONFIG_IDF_TARGET_LINUX

void app_main(void)
{
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
#if !CONFIG_IDF_TARGET_LINUX
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET
#endif // !CONFIG_IDF_TARGET_LINUX

    /* Start the server for the first time */
    server = start_webserver();

    gpio_init_all();

    while (server) {
        sleep(5);
    }
}
