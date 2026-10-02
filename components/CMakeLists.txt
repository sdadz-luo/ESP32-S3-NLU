# components/ 采用「目录即组件」模式：本目录含 CMakeLists.txt 时，
# ESP-IDF 构建系统将目录自身注册为一个组件（project.cmake 的
# __project_component_dir），不再扫描其子目录——因此源文件全部平铺于此。
idf_component_register(
    SRCS
        "app.c"
        "app_queues.c"
        "input_serial.c"
        "input_key.c"
        "nlu.c"
        "resp.c"
        "tools.c"
        "rhythm.c"
        "led.c"
        "iic.c"
        "xl9555.c"
        "spi.c"
        "lcd.c"
        "font16.c"
        "ui.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_driver_gpio esp_driver_spi esp_timer esp_hw_support esp_partition
)
