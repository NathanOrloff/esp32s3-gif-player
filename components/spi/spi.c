#include <string.h>

#include "spi.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"


#define TAG "SPI"

#define SPI_DEFAULT_FREQUENCY SPI_MASTER_FREQ_10M; // 10MHz

int clock_speed_hz = SPI_DEFAULT_FREQUENCY;

void spi_set_clock_speed(int speed) {
	ESP_LOGI(TAG, "SPI clock speed=%d MHz", speed/1000000);
	clock_speed_hz = speed;
}

int spi_get_clock_speed() {
    return clock_speed_hz;
}

esp_err_t spi_bus_init(spi_host_device_t host, int mosi, int miso, int sclk, int max_transfer_sz) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = max_transfer_sz,
    };

    esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    ESP_LOGI(TAG, "spi_bus_initialize host=%d ret=%d", host, ret);
    return ret;
}

esp_err_t spi_bus_add_dev(spi_host_device_t host, spi_device_params_t *params, spi_device_handle_t *out_handle) {
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = params->clock_speed_hz,
        .spics_io_num   = params->cs,
        .queue_size     = params->queue_size,
        .flags          = params->flags,
    };

    esp_err_t ret = spi_bus_add_device(host, &devcfg, out_handle);
    ESP_LOGI(TAG, "spi_bus_add_device host=%d cs=%d ret=%d", host, params->cs, ret);
    return ret;
}

bool spi_master_write_byte(spi_device_handle_t SPIHandle, const uint8_t* Data, size_t DataLength)
{
	spi_transaction_t SPITransaction;
	esp_err_t ret;

	if ( DataLength > 0 ) {
		memset( &SPITransaction, 0, sizeof( spi_transaction_t ) );
		SPITransaction.length = DataLength * 8;
		SPITransaction.tx_buffer = Data;
#if 1
		ret = spi_device_transmit( SPIHandle, &SPITransaction );
#endif
#if 0
		ret = spi_device_polling_transmit( SPIHandle, &SPITransaction );
#endif
		assert(ret==ESP_OK); 
	}

	return true;
}
