#ifndef SPI_H
#define SPI_H

#include "driver/spi_master.h"

#if CONFIG_SPI2_HOST
#define SPI_HOST_ID SPI2_HOST
#elif CONFIG_SPI3_HOST
#define SPI_HOST_ID SPI3_HOST
#else
#define SPI_HOST_ID SPI2_HOST  // default
#endif

typedef struct {
    int mosi;
    int miso;
    int sclk;
    int cs;
    int clock_speed_hz;
    int queue_size;
    uint32_t flags;
    int max_transfer_sz;
} spi_device_params_t;

esp_err_t spi_bus_init(spi_host_device_t host, int mosi, int miso, int sclk, int max_transfer_sz);
esp_err_t spi_bus_add_dev(spi_host_device_t host, spi_device_params_t *params, spi_device_handle_t *out_handle);

bool spi_master_write_byte(spi_device_handle_t SPIHandle, const uint8_t* Data, size_t DataLength);

void spi_set_clock_speed(int speed);
int spi_get_clock_speed();

#endif // SPI_H