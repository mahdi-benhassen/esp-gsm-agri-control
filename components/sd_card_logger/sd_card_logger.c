#include "sd_card_logger.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "rtc_manager.h"
#include "sdmmc_cmd.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "SD_LOGGER";
static bool s_mounted = false;
static char s_log_path[64] = "/sdcard/log.txt";
static FILE *s_log_file = NULL;

esp_err_t sd_card_logger_init(void) {
  esp_err_t err;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = CONFIG_SD_SPI_MOSI,
      .miso_io_num = CONFIG_SD_SPI_MISO,
      .sclk_io_num = CONFIG_SD_SPI_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4092,
  };

  err = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
    return err;
  }

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = CONFIG_SD_SPI_CS;
  slot_config.host_id = host.slot;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_card_t *card;
  err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config,
                                 &mount_config, &card);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
    spi_bus_free(host.slot);
    return err;
  }

  sdmmc_card_print_info(stdout, card);
  s_mounted = true;

  struct tm now;
  if (rtc_manager_get_time(&now) == ESP_OK) {
    snprintf(s_log_path, sizeof(s_log_path),
             "/sdcard/log_%04d%02d%02d.txt",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
  }

  ESP_LOGI(TAG, "SD card mounted. Log: %s", s_log_path);
  return ESP_OK;
}

esp_err_t sd_card_logger_write(const char *line) {
  if (!s_mounted || line == NULL) return ESP_ERR_INVALID_STATE;

  s_log_file = fopen(s_log_path, "a");
  if (s_log_file == NULL) {
    ESP_LOGE(TAG, "Failed to open log file");
    return ESP_FAIL;
  }

  struct tm now;
  char ts[64] = "";
  if (rtc_manager_get_time(&now) == ESP_OK) {
    snprintf(ts, sizeof(ts), "[%04d-%02d-%02d %02d:%02d:%02d] ",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
             now.tm_hour, now.tm_min, now.tm_sec);
  }

  fprintf(s_log_file, "%s%s\n", ts, line);
  fclose(s_log_file);
  s_log_file = NULL;
  return ESP_OK;
}

bool sd_card_is_mounted(void) {
  return s_mounted;
}

esp_err_t sd_card_logger_get_info(char *buf, size_t len) {
  if (!s_mounted || buf == NULL) return ESP_ERR_INVALID_STATE;

  FATFS *fs;
  DWORD free_clust;
  if (f_getfree("/sdcard", &free_clust, &fs) == FR_OK) {
    uint64_t free_bytes = (uint64_t)free_clust * fs->csize * 512;
    uint64_t total_bytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
    snprintf(buf, len, "SD: total=%llu, free=%llu",
             (unsigned long long)total_bytes, (unsigned long long)free_bytes);
  } else {
    snprintf(buf, len, "SD: mounted");
  }
  return ESP_OK;
}
