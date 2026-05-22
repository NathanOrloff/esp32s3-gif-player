#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"

#include "gifdec.h"
#include "ili9340.h"
#include "spi.h"

#define SD_MOUNT_POINT "/sdcard"

static const char *TAG = "MAIN";

typedef uint16_t pixel_gif;

typedef struct {
    pixel_gif *pixels;
} gif_frame_t;

typedef struct {
    gif_frame_t *frames;
    int count;
    int width;
    int height;
} gif_animation_t;

esp_err_t decode_gif(pixel_gif *pixels, gd_GIF *gif) {
    uint8_t r, g, b;
    uint8_t idx;
    uint16_t color;

    for (int i = 0; i < gif->height; i++) {
        for (int j = 0; j < gif->width; j++) {
            idx = gif->frame[i * gif->width + j];
            r = gif->palette->colors[idx * 3 + 0];
            g = gif->palette->colors[idx * 3 + 1];
            b = gif->palette->colors[idx * 3 + 2];
            color = (uint16_t)rgb565(r, g, b);
            pixels[i * gif->width + j] = (color >> 8) | (color << 8);
        }
    }
    return ESP_OK;
}

int compare_filenames(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

esp_err_t load_animation(const char *folder, int width, int height, gif_animation_t *anim) {
    anim->frames = NULL;
    anim->count = 0;
    anim->width = width;
    anim->height = height;

    DIR *dir = opendir(folder);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open folder: %s", folder);
        return ESP_FAIL;
    }

    // count GIF files
    int file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && strcasecmp(ext, ".gif") == 0) file_count++;
    }

    if (file_count == 0) {
        ESP_LOGE(TAG, "No GIF files found in %s", folder);
        closedir(dir);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Found %d GIF files", file_count);

    // collect and sort filenames
    char **filenames = malloc(sizeof(char *) * file_count);
    if (!filenames) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    rewinddir(dir);
    int idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < file_count) {
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && strcasecmp(ext, ".gif") == 0) {
            filenames[idx] = strdup(entry->d_name);
            if (!filenames[idx]) {
                for (int i = 0; i < idx; i++) free(filenames[i]);
                free(filenames);
                closedir(dir);
                return ESP_ERR_NO_MEM;
            }
            idx++;
        }
    }
    closedir(dir);
    qsort(filenames, file_count, sizeof(char *), compare_filenames);

    // allocate frame array in PSRAM
    anim->frames = heap_caps_malloc(sizeof(gif_frame_t) * file_count, MALLOC_CAP_SPIRAM);
    if (!anim->frames) {
        ESP_LOGE(TAG, "Failed to allocate frame array");
        for (int i = 0; i < file_count; i++) free(filenames[i]);
        free(filenames);
        return ESP_ERR_NO_MEM;
    }

    size_t frame_size = width * height * sizeof(pixel_gif);
    char filepath[256];

    for (int i = 0; i < file_count; i++) {
        snprintf(filepath, sizeof(filepath), "%s/%s", folder, filenames[i]);

        pixel_gif *pixels = heap_caps_malloc(frame_size, MALLOC_CAP_SPIRAM);
        if (!pixels) {
            ESP_LOGW(TAG, "PSRAM full at frame %d/%d — loaded %d frames", i, file_count, anim->count);
            break;
        }

        gd_GIF *gif = gd_open_gif(filepath);
        if (!gif) {
            ESP_LOGE(TAG, "Failed to open: %s", filepath);
            free(pixels);
            continue;
        }

        if (gd_get_frame(gif) == 1) {
            decode_gif(pixels, gif);
            anim->frames[anim->count++].pixels = pixels;
        } else {
            ESP_LOGW(TAG, "No frame in: %s", filepath);
            free(pixels);
        }

        gd_close_gif(gif);
    }

    for (int i = 0; i < file_count; i++) free(filenames[i]);
    free(filenames);

    ESP_LOGI(TAG, "Loaded %d frames into PSRAM (%u bytes used)",
        anim->count, (unsigned)(anim->count * frame_size));

    return anim->count > 0 ? ESP_OK : ESP_FAIL;
}

void play_animation(TFT_t *dev, gif_animation_t *anim, int frame_delay_ms) {
    for (int i = 0; i < anim->count; i++) {
        lcdDrawImage(dev, 0, 0, anim->width, anim->height, anim->frames[i].pixels);
        if (frame_delay_ms > 0) vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
    }
}

esp_err_t mountSPIFFS(char *path, char *label, int max_files) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = path,
        .partition_label = label,
        .max_files = max_files,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL)
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        else if (ret == ESP_ERR_NOT_FOUND)
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        else
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Mount %s to %s success", path, label);
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ret;
}

esp_err_t add_sd_card_spi_device(spi_host_device_t host) {
    esp_err_t ret = ESP_OK;
    sdmmc_card_t *card;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t sd_host = SDSPI_HOST_DEFAULT();
    sd_host.slot = host;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_MINI_SD_CS_GPIO;
    slot_config.host_id = host;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &sd_host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card (%s).", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    return ret;
}

void app_main(void)
{
    esp_err_t ret = ESP_OK;
    TFT_t dev;

    spi_set_clock_speed(SPI_MASTER_FREQ_40M);

    ret = spi_bus_init(SPI_HOST_ID, CONFIG_MOSI_GPIO, CONFIG_MISO_GPIO, CONFIG_SCLK_GPIO, 8192);
    assert(ret == ESP_OK);

    add_tft_spi_device(&dev, CONFIG_TFT_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
    add_sd_card_spi_device(SPI_HOST_ID);

    uint16_t model = 0x9340;
    lcdInit(&dev, model, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);

    ESP_LOGI(TAG, "Free SPIRAM before load: %u", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    gif_animation_t anim = {0};
    ESP_ERROR_CHECK(load_animation("/sdcard/ZORO_F~1", CONFIG_WIDTH, CONFIG_HEIGHT, &anim));

    ESP_LOGI(TAG, "Free SPIRAM after load:  %u", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Starting playback of %d frames", anim.count);

    while (1) {
        play_animation(&dev, &anim, 50);
    }
}