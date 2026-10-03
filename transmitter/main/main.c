// tx-s3
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

#define CSI_CHANNEL 1

static uint8_t RX_MAC_ADDR[6] = {0x28, 0x84, 0x85, 0x92, 0x3E, 0xB8};

static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Packet not ACKed by RX (check RX MAC / channel / RX running)");
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CSI_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

static void espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, RX_MAC_ADDR, 6);
    peer.channel = CSI_CHANNEL;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    // Force an HT rate so each packet carries LLTF + HT-LTF (default 1 Mbps has no CSI)
    esp_now_rate_config_t rate_cfg = {
        .phymode = WIFI_PHY_MODE_HT20,
        .rate    = WIFI_PHY_RATE_MCS0_LGI,
        .ersu    = false,
        .dcm     = false,
    };
    ESP_ERROR_CHECK(esp_now_set_peer_rate_config(RX_MAC_ADDR, &rate_cfg));

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

    ESP_LOGI(TAG, "Starting packet transmission (10 packets/sec)...");
    xTaskCreate(send_task, "send_task", 4096, NULL, 5, NULL);
}