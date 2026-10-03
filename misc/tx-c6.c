// tx-c6

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char *TAG = "csi_tx";

// ---- EDIT THIS: paste the S3's MAC address here, printed at its boot ----
static uint8_t RX_MAC_ADDR[6] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
// ---------------------------------------------------------------------

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Must match the RX board's channel exactly.
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

static void espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, RX_MAC_ADDR, 6);
    peer.channel = 1;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "ESP-NOW sender ready, targeting RX board");
}

static void send_task(void *pvParameters)
{
    const char *msg = "ping";
    while (1) {
        esp_err_t result = esp_now_send(RX_MAC_ADDR, (const uint8_t *)msg, strlen(msg));
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10 packets/sec
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    espnow_init();

    ESP_LOGI(TAG, "Starting to send packets at 10/sec...");
    xTaskCreate(send_task, "send_task", 4096, NULL, 5, NULL);
}
