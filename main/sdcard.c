#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_vfs_fat.h>
#include <driver/sdspi_host.h>
#include <sdmmc_cmd.h>
#include <sdkconfig.h>

static const char* TAG = "SDCARD";
static const char* base_path = "/sdcard";
static sdmmc_card_t* card;
static sdmmc_host_t host = SDSPI_HOST_DEFAULT();

void write_file(char *path, char *content);
void read_file(char *path);


esp_err_t init_sd_card() {
    esp_err_t ret_code;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    spi_bus_config_t spi_bus_config = {
        .mosi_io_num = CONFIG_SPI_MOSI_PIN,
        .miso_io_num = CONFIG_SPI_MISO_PIN,
        .sclk_io_num = CONFIG_SPI_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret_code = spi_bus_initialize(host.slot, &spi_bus_config, SDSPI_DEFAULT_DMA);
    if (ret_code != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize SPI bus");
        return ret_code;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SPI_SDCARD_CS_PIN;
    slot_config.host_id = host.slot;

    ret_code = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &card);
    if (ret_code != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mount SD card");
        return ret_code;
    }
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;

}

void umount_card() {
    esp_vfs_fat_sdcard_unmount(base_path, card);
    spi_bus_free(host.slot);
}


void read_file(char *path)
{
    ESP_LOGI(TAG, "reading file %s", path);
    FILE *file = fopen(path, "r");
    char buffer[100];
    fgets(buffer, 99, file);
    fclose(file);
    ESP_LOGI(TAG, "file contains: %s", buffer);
}

void write_file(char *path, char *content)
{
    ESP_LOGI(TAG, "Writing \"%s\" to %s", content, path);
    FILE *file = fopen(path, "w");
    int count = fputs(content, file);
    printf("Wrote %d bytes\n", count);
    fclose(file);
}