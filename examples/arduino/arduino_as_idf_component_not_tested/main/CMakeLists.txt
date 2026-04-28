idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES arduino JkkSchedule nvs_flash esp_sntp
)
