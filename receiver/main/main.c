// rx-s3
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

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

#define CSI_CHANNEL 1

// ---- Source filter -------------------------------------------------------
// Fill in the TX board's STA MAC, then set FILTER_BY_TX_MAC to 1.
#define FILTER_BY_TX_MAC 1
static const uint8_t TX_MAC[6] = {0x90, 0x70, 0x69, 0x06, 0xF7, 0x70};

// ---- Motion detection ----------------------------------------------------
#define WINDOW_SIZE        50     // ~5 s at 10 pkt/s
#define CALIB_SAMPLES      100    // ~10 s of variance readings after window fills
#define THRESHOLD_MULT     3.0f   // threshold = baseline_max * this
#define THRESHOLD_MIN      0.05f  // floor so a very quiet baseline isn't hypersensitive

static float g_amp_window[WINDOW_SIZE];
static int   g_window_idx  = 0;
static bool  g_window_full = false;

static int   g_calib_count = 0;
static float g_baseline_max = 0.0f;
static float g_threshold    = 0.0f;
static bool  g_calibrated   = false;

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf) {
        return;
    }

#if FILTER_BY_TX_MAC
    if (memcmp(info->mac, TX_MAC, 6) != 0) {
        return;
    }
#endif

    int len = info->len;
    int8_t *buf = info->buf;

    // First 4 bytes can be invalid when flagged
    int start = info->first_word_invalid ? 4 : 0;

    float sum_amp = 0;
    int count = 0;

    // Samples are (imaginary, real) int8 pairs per subcarrier
    for (int i = start; i < len - 1; i += 2) {
        int im = buf[i];
        int re = buf[i + 1];
        sum_amp += sqrtf((float)(im * im + re * re));
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

    // Wait for a full window so variance isn't computed from a few samples
    if (!g_window_full) {
        return;
    }

    float mean = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        mean += g_amp_window[i];
    }
    mean /= WINDOW_SIZE;

    float variance = 0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        float d = g_amp_window[i] - mean;
        variance += d * d;
    }
    variance /= WINDOW_SIZE;

    // Calibration: keep the room still while this runs
    if (!g_calibrated) {
        if (variance > g_baseline_max) {
            g_baseline_max = variance;
        }
        g_calib_count++;
        if (g_calib_count >= CALIB_SAMPLES) {
            g_threshold = g_baseline_max * THRESHOLD_MULT;
            if (g_threshold < THRESHOLD_MIN) {
                g_threshold = THRESHOLD_MIN;
            }
            g_calibrated = true;
            ESP_LOGI(TAG, "Calibration done: baseline_max=%.4f threshold=%.4f",
                     g_baseline_max, g_threshold);
        } else if (g_calib_count % 10 == 0) {
            ESP_LOGI(TAG, "Calibrating... keep room still (%d/%d) var=%.4f",
                     g_calib_count, CALIB_SAMPLES, variance);
        }
        return;
    }

    const char *status = (variance > g_threshold) ? "MOTION" : "clear";
    ESP_LOGI(TAG, "rssi=%d len=%d sc=%d avg_amp=%.2f var=%.4f thr=%.4f [%s]",
             info->rx_ctrl.rssi, len, count, avg_amp, variance, g_threshold, status);
}

static void csi_init(void)
{
    wifi_csi_config_t csi_config;
    memset(&csi_config, 0, sizeof(csi_config));

    csi_config.lltf_en           = true;
    csi_config.htltf_en          = true;
    csi_config.stbc_htltf2_en    = false;
    csi_config.ltf_merge_en      = true;
    csi_config.channel_filter_en = false;
    csi_config.manu_scale        = false;
    csi_config.shift             = false;

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    ESP_LOGI(TAG, "CSI capture enabled");
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    // Payload unused; CSI comes from the Wi-Fi PHY callback
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
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CSI_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    espnow_init();
    csi_init();
}