// rx-s3

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "nvs_flash.h"

static const char *TAG = "csi_rx";

#define WINDOW_SIZE 100
static float g_amp_window[WINDOW_SIZE];
static int g_window_idx = 0;
static bool g_window_full = false;

// Simple threshold — you will TUNE this number during calibration.
// Starting guess only; walk through the sensed area and watch the
// printed motion_score to pick a real value.
#define MOTION_THRESHOLD 5.0f

// ---------------------------------------------------------------------
// CSI callback — fires for every WiFi packet received, including the
// ESP-NOW packets sent by the C6.
// ---------------------------------------------------------------------

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf) {
        return;
    }

    int len = info->len;
    int8_t *buf = info->buf;

    float sum_amp = 0;
    int count = 0;
    for (int i = 0; i < len - 1; i += 2) {
        int8_t im = buf[i];
        int8_t re = buf[i + 1];
        float amp = sqrtf((float)(im * im + re * re));
        sum_amp += amp;
        count++;
    }
    if (count == 0) {
        return;
    }
    float avg_amp = sum_amp / count;

    g_amp_window[g_window_idx] = avg_amp;
    g_window_idx = (g_window_idx + 1) % WINDOW_SIZE;
    if (g_window_idx == 0) {
        g_window_full = true;
    }

    int n = g_window_full ? WINDOW_SIZE : g_window_idx;
    if (n < 2) {
        return;
    }

    float mean = 0;
    for (int i = 0; i < n; i++) {
        mean += g_amp_window[i];
    }
    mean /= n;

    float variance = 0;
    for (int i = 0; i < n; i++) {
        float d = g_amp_window[i] - mean;
        variance += d * d;
    }
    variance /= n;

    const char *status = (variance > MOTION_THRESHOLD) ? "MOTION" : "clear";

    ESP_LOGI(TAG, "rssi=%d subcarriers=%d avg_amp=%.2f motion_score=%.2f [%s]",
             info->rx_ctrl.rssi, count, avg_amp, variance, status);
}

static void csi_init(void)
{
    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    ESP_LOGI(TAG, "CSI capture enabled");
}

// ---------------------------------------------------------------------
// ESP-NOW receive callback — we don't actually care about the data
// payload itself, only that a packet arrived (which triggers the CSI
// callback above). Kept minimal on purpose.
// ---------------------------------------------------------------------

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    // Intentionally empty — the CSI callback above does the real work.
    // This just needs to exist so ESP-NOW has somewhere to deliver packets.
}

static void espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_LOGI(TAG, "ESP-NOW receiver ready");
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // IMPORTANT: TX and RX must be on the same WiFi channel for
    // ESP-NOW to work. Channel 1 is used here — make sure the TX
    // firmware (C6) uses the same channel number.
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    // Print this board's MAC address — copy it into the TX firmware.
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "MY MAC ADDRESS (copy this into TX firmware): %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    espnow_init();
    csi_init();
}
