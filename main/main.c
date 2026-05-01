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

#include "gifdec.h"
#include "ili9340.h"

static const char *TAG = "MAIN";

//rgb565 format
typedef uint16_t pixel_gif;

esp_err_t release_pixels(pixel_gif **pixels) {
    if (*pixels != NULL) {
        free(*pixels);
        *pixels = NULL;
    }
    return ESP_OK;
}

esp_err_t decode_gif(pixel_gif **pixels, gd_GIF *gif) {
    uint8_t r, g, b;
    uint8_t idx;
    uint16_t color;

    *pixels = (pixel_gif *)malloc(gif->width * gif->height * sizeof(pixel_gif));
    if (*pixels == NULL) {
        ESP_LOGE(__FUNCTION__, "malloc failed for pixel buffer");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < gif->height; i++) {
        for (int j = 0; j < gif->width; j++) {
            idx = gif->frame[i * gif->width + j];
            r = gif->palette->colors[idx * 3 + 0];
            g = gif->palette->colors[idx * 3 + 1];
            b = gif->palette->colors[idx * 3 + 2];
            color = (uint16_t)rgb565(r, g, b);
            (*pixels)[i * gif->width + j] = (color >> 8) | (color << 8);
        }
    }
    return ESP_OK;
}

esp_err_t display_image(TFT_t * dev, char * file, int width, int height) {
    pixel_gif *pixels = NULL;
    gd_GIF *gif = NULL;
    esp_err_t ret = ESP_OK;

    gif = gd_open_gif(file);
    if (gif == NULL) {
        ret = ESP_ERR_NOT_FOUND;
        goto err;
    }

    int frame_ret = gd_get_frame(gif);
    ESP_LOGI(TAG, "gif fw=%d fh=%d fx=%d fy=%d", gif->fw, gif->fh, gif->fx, gif->fy);
    if (frame_ret == 0) {
        ret = ESP_FAIL;
        goto err;
    }

    // get pixels
    ret = decode_gif(&pixels, gif);
    if (ret != ESP_OK) {
        goto err;
    }

    ESP_LOGI(TAG, "Drawing: x=0 y=0 w=%d h=%d", gif->width, gif->height);
    lcdDrawImage(dev, 0, 0, gif->width, gif->height, pixels);

    // clean up and exit
err:
    if (pixels != NULL) release_pixels(&pixels);
    if (gif != NULL) gd_close_gif(gif);

    return ret;
}

static void listSPIFFS(char * path) {
	DIR* dir = opendir(path);
	assert(dir != NULL);
	while (true) {
		struct dirent*pe = readdir(dir);
		if (!pe) break;
		ESP_LOGI(__FUNCTION__,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
	}
	closedir(dir);
}

esp_err_t mountSPIFFS(char * path, char * label, int max_files) {
	esp_vfs_spiffs_conf_t conf = {
		.base_path = path,
		.partition_label = label,
		.max_files = max_files,
		.format_if_mount_failed = true
	};

	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret ==ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret== ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
		}
		return ret;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(conf.partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG,"Mount %s to %s success", path, label);
		ESP_LOGI(TAG,"Partition size: total: %d, used: %d", total, used);
	}

	return ret;
}

void app_main(void)
{
    TFT_t dev;

	int XPT_MISO_GPIO = -1;
	int XPT_CS_GPIO = -1;
	int XPT_IRQ_GPIO = -1;
	int XPT_SCLK_GPIO = -1;
	int XPT_MOSI_GPIO = -1;

    spi_clock_speed(SPI_MASTER_FREQ_40M);
    spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_TFT_CS_GPIO, CONFIG_DC_GPIO, 
		CONFIG_RESET_GPIO, CONFIG_BL_GPIO, XPT_MISO_GPIO, XPT_CS_GPIO, XPT_IRQ_GPIO, XPT_SCLK_GPIO, XPT_MOSI_GPIO);

    uint16_t model = 0x9340;

    lcdInit(&dev, model, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);

    ESP_ERROR_CHECK(mountSPIFFS("/gifs", "storage0", 1));
    listSPIFFS("/gifs/");

    char file[64];
    strcpy(file, "/gifs/frame_00_delay-0.1s.gif");
    display_image(&dev, file, CONFIG_WIDTH, CONFIG_HEIGHT);

    while (1) {
        vTaskDelay(1000);
    }
}
