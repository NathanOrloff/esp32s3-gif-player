set(srcs "mini_sd_card.c")

idf_component_register(
	SRCS "mini_sd_card.c" "${srcs}"
	PRIV_REQUIRES driver esp_driver_spi esp_driver_gpio
	INCLUDE_DIRS "."
)
