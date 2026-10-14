idf_component_register(
    SRCS "atividade_7_interrupcoes_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_timer spi_flash
)