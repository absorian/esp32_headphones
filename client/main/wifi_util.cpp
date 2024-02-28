#include "wifi_util.h"

#include <cstring>
#include <impl.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "common_util.h"

#define NETIF_TXT_DESC "HEADPHONES_NETIF_STA"
#define CONN_MAX_RETRY 6

static const char *TAG = "WIFI_UTIL";

static esp_netif_t *sta_netif = nullptr;
static SemaphoreHandle_t ip_addr_sem = nullptr;
static int retry_counter = 0;

static void wifi_disconnect_cb(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    retry_counter++;
    if (retry_counter > CONN_MAX_RETRY) {
        logi(TAG, "WiFi Connect failed %d times, stop reconnect.", retry_counter);
        /* let wifi_do_connect() return */
        xSemaphoreGive(ip_addr_sem);
        return;
    }
    logi(TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void wifi_connect_cb(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data) {
}

static void sta_got_ip_cb(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    retry_counter = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    if (strncmp(NETIF_TXT_DESC, esp_netif_get_desc(event->esp_netif), strlen(NETIF_TXT_DESC) - 1) != 0) {
        return;
    }
    logi(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif),
         IP2STR(&event->ip_info.ip));
    xSemaphoreGive(ip_addr_sem);
}

static void wifi_start() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    esp_netif_config.if_desc = NETIF_TXT_DESC;
    esp_netif_config.route_prio = 128;
    sta_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void wifi_stop() {
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(sta_netif));
    esp_netif_destroy(sta_netif);
    sta_netif = nullptr;
}

void wifi_util::init() {
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

void wifi_util::shutdown() {
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_disconnect_cb));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_got_ip_cb));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_connect_cb));
    esp_wifi_disconnect();

    wifi_stop();

    if (ip_addr_sem) {
        vSemaphoreDelete(ip_addr_sem);
    }

    ESP_ERROR_CHECK(esp_unregister_shutdown_handler(&shutdown));
}

esp_err_t wifi_util::connect() {
    wifi_start();
    wifi_config_t wifi_config = {
            .sta = {
                    .ssid = CONFIG_EXAMPLE_WIFI_SSID,
                    .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
                    .scan_method = WIFI_ALL_CHANNEL_SCAN,
                    .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
                    .threshold = {
                            .rssi = -127, // ?? research
                            .authmode = WIFI_AUTH_OPEN,
                    }
            },
    };

    if (ip_addr_sem == nullptr) {
        ip_addr_sem = xSemaphoreCreateBinary();
        BAD_VAL_IF(ip_addr_sem, nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    retry_counter = 0;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_disconnect_cb, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_got_ip_cb, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_connect_cb, sta_netif));

    logi(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        loge(TAG, "WiFi connect failed! ret:%x", ret);
        return ret;
    }

    logi(TAG, "Waiting for IP(s)");
    while (!xSemaphoreTake(ip_addr_sem, pdMS_TO_TICKS(500))) {
        logr(".");
    }
    logr("\n");
    if (retry_counter > CONN_MAX_RETRY) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_register_shutdown_handler(&shutdown));
    return ESP_OK;
}
