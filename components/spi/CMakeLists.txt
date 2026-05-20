set(srcs "spi.c")

idf_component_register(
	SRCS "spi.c" "${srcs}"
	PRIV_REQUIRES driver esp_driver_spi esp_driver_gpio
	INCLUDE_DIRS "."
)
