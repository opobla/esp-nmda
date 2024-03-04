#include "meteo_bmp280.h"
#include <errno.h>
#include <esp_log.h>
#include <bmp2.h>
#include <string.h>

spi_device_handle_t spi;

int8_t initialize_spi_communication() {
	int8_t ret;
	ret = ENOSYS;
	ret = initialize_spi_sensor();
	if (ret < 0) {
		ESP_ERROR_CHECK(ret);
		printf("Error while initializing spi sensor\n");
		return ret;
	}

	return ret;
}

int8_t initialize_spi_sensor() {
	int8_t ret;

	spi_bus_config_t buscfg = {.mosi_io_num = SPI_MOSI_PIN,
	                           .miso_io_num = SPI_MISO_PIN,
	                           .sclk_io_num = SPI_CLK_PIN,
	                           .quadwp_io_num = -1,
	                           .quadhd_io_num = -1};

	spi_device_interface_config_t devcfg = {.clock_speed_hz = 1 * 1000 * 1000, // Clock out at 10 MHz
	                                        .mode = 0,
	                                        .spics_io_num = SPI_CS_PIN,
	                                        .queue_size = 1,
	                                        .address_bits = 8};

	// Initialize the SPI bus
	ret = ENOSYS;
	ret = spi_bus_initialize(HSPI_HOST, &buscfg, DMA_CHAN);
	if (ret < 0) {
		printf("Error while initializing SPI bus\n");
		ESP_ERROR_CHECK(ret);
		return ret;
	}

	// Attach the device to the SPI bus
	ret = ENOSYS;
	ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
	if (ret < 0) {
		ESP_ERROR_CHECK(ret);
		printf("Error while adding device to SPI bus\n");
		return ret;
	}

	return ret;
}

BMP2_INTF_RET_TYPE bmp240_read  (uint8_t reg_addr, uint8_t *reg_data, uint32_t length, const void *intf_ptr) {
    int ret;
	static struct spi_transaction_t trans;
    printf("----- BEGIN READ ----- \n");

	trans.flags = 0;
	trans.addr = reg_addr;
	trans.length = length * 8;
	trans.tx_buffer = heap_caps_malloc(1, MALLOC_CAP_DMA);
	trans.rx_buffer = heap_caps_malloc(length, MALLOC_CAP_DMA);

	/* debug */
	printf("READ -- length = %lu\n", length);
	printf("READ -- reg_addr = %x\n", reg_addr);

	ret = ENOSYS;
	ret = spi_device_transmit(spi, &trans);
	if (ret < 0) {
		printf("Error: transaction transmission failled\n");
		return ret;
	}

	memcpy(reg_data, trans.rx_buffer, length);

	/* debug */
	for (int i = 0; i < length; i++) {
	    printf("READ -- rx_buffer[%d] = %d %x\n", i, reg_data[i], reg_data[i]);
    }
    heap_caps_free(trans.tx_buffer);
    heap_caps_free(trans.rx_buffer);
	printf("----- END READ ----\n");

	return ret;
}

BMP2_INTF_RET_TYPE bmp240_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, const void *intf_ptr) {
    int8_t ret;
	static struct spi_transaction_t trans;
    printf("----------- WRITE -----------\n");

	trans.tx_buffer = heap_caps_malloc(length, MALLOC_CAP_DMA);

	trans.flags = 0;
	trans.addr = reg_addr;
	trans.length = length * 8;

	memcpy((void *) trans.tx_buffer, reg_data, length);

    /* debug */
	printf("WRITE -- length = %lu\n", length);
	printf("WRITE -- reg_addr = %x\n", reg_addr);

    /* debug */
	for (int i = 0; i < length; i++) {
	    printf("WRITE -- tx_buffer[%d] = %d %x\n", i, ((uint8_t *) trans.tx_buffer)[i], ((uint8_t *) trans.tx_buffer)[i]);
    }
	ret = spi_device_transmit(spi, &trans);
	if (ret < 0) {
		printf("Error: transaction transmission failled\n");
		return ret;
	}
    heap_caps_free(trans.tx_buffer);
    printf("----------- END OF WRITE -----------\n");
	return ret;
}

void delay_us(uint32_t period_us, void* intf_ptr) {
	vTaskDelay(period_us / 1000 / portTICK_RATE_MS);
}

int bmp280_task(void *parameters) {
    int ret;
    struct bmp2_dev dev;
    struct bmp2_data comp_data;
    struct bmp2_config conf;
    uint32_t meas_time;

    ESP_LOGI(TAG, "Starting on %d Core", xPortGetCoreID());
    initialize_spi_communication();
    
    // Initialize the BMP280
    dev.intf = BMP2_SPI_INTF;
    dev.read = &bmp240_read;
    dev.write = &bmp240_write;
	dev.delay_us = &delay_us;

    bmp2_init(&dev);

    printf("Calibration data p1 %d:\n", dev.calib_param.dig_p1);
    printf("Calibration data p2 %d:\n", dev.calib_param.dig_p2);


    /**
    printf("- RESET -\n");
    ret = bmp2_soft_reset(&dev);
    bmp2_error_codes_print_result("bmp2_soft_reset", ret);

    printf("- INIT -\n");
	ret = bmp2_init(&dev);
    ESP_LOGI(TAG, "bmp2_init result: %d ", ret);

    printf("- GET CONFIG -\n");
    ret = bmp2_get_config(&conf, &dev);
    bmp2_error_codes_print_result("bmp2_get_config", ret);

    */

    printf("- SET CONFIG -\n");
    /* Configuring the over-sampling mode, filter coefficient and output data rate */
    /* Overwrite the desired settings */
    conf.filter = BMP2_FILTER_COEFF_16;

    /* Over-sampling mode is set as high resolution i.e., os_pres = 8x and os_temp = 1x */
    conf.os_mode = BMP2_OS_MODE_HIGH_RESOLUTION;

    /* Setting the output data rate */
    conf.odr = BMP2_ODR_250_MS;
    conf.spi3w_en = BMP2_SPI3_WIRE_DISABLE;

    ret = bmp2_set_config(&conf, &dev);
    bmp2_error_codes_print_result("bmp2_set_config", ret);

    printf("- SET POWER -\n");
    /* Set normal power mode */
    ret = bmp2_set_power_mode(BMP2_POWERMODE_NORMAL, &conf, &dev);
    bmp2_error_codes_print_result("bmp2_set_power_mode", ret);

    /* Calculate measurement time in microseconds */
    ret = bmp2_compute_meas_time(&meas_time, &conf, &dev);
    bmp2_error_codes_print_result("bmp2_compute_meas_time", ret);
    printf("Measurement time: %lu us\n", (long unsigned int)meas_time);

    conf.filter = 0;
    bmp2_get_config(&conf, &dev);
    printf("Filter: %d\n", conf.filter);
    printf("Oversampling mode: %d\n", conf.os_mode);
    printf("Oversampling pressure: %d\n", conf.os_pres);
    printf("Oversampling temperature: %d\n", conf.os_temp);
    printf("Output data rate: %d\n", conf.odr);
    printf("SPI 3 wire: %d\n", conf.spi3w_en);


    
    ret = bmp2_get_sensor_data(&comp_data, &dev);
    ESP_LOGI(TAG, "bmp2_get_sensor_data result: %d ", ret);

    while (true) {
        #ifdef BMP2_DOUBLE_COMPENSATION
        printf("Data[%d]:    Temperature: %.4lf deg C	Pressure: %.4lf Pa\n",
            0,
            comp_data.temperature,
            comp_data.pressure);
        #else
        printf("Data[%d]:    Temperature: %ld deg C	Pressure: %lu Pa\n", 0, (long int)comp_data.temperature,
            (long unsigned int)comp_data.pressure);
        #endif
        vTaskDelay(3000 / portTICK_RATE_MS);
        ret = bmp2_get_sensor_data(&comp_data, &dev);
        ESP_LOGI(TAG, "bmp2_get_sensor_data result: %d ", ret);
    }
    
    ESP_LOGI(TAG, "Finishing on %d Core", xPortGetCoreID());
    return 0;
}


static int8_t get_data(uint32_t period, struct bmp2_dev *dev)
{
    int8_t rslt = BMP2_E_NULL_PTR;
    int8_t idx = 1;
    struct bmp2_status status;
    struct bmp2_data comp_data;

    printf("Measurement delay : %lu us\n", (long unsigned int)period);

    while (idx <= 50)
    {
        rslt = bmp2_get_status(&status, dev);
        bmp2_error_codes_print_result("bmp2_get_status", rslt);

        if (status.measuring == BMP2_MEAS_DONE)
        {
            /* Delay between measurements */
            dev->delay_us(period, dev->intf_ptr);

            /* Read compensated data */
            rslt = bmp2_get_sensor_data(&comp_data, dev);
            bmp2_error_codes_print_result("bmp2_get_sensor_data", rslt);

            #ifdef BMP2_64BIT_COMPENSATION
            comp_data.pressure = comp_data.pressure / 256;
            #endif

            #ifdef BMP2_DOUBLE_COMPENSATION
            printf("Data[%d]:    Temperature: %.4lf deg C	Pressure: %.4lf Pa\n",
                   idx,
                   comp_data.temperature,
                   comp_data.pressure);
            #else
            printf("Data[%d]:    Temperature: %ld deg C	Pressure: %lu Pa\n", idx, (long int)comp_data.temperature,
                   (long unsigned int)comp_data.pressure);
            #endif

            idx++;
        }
    }

    return rslt;
}

void bmp2_error_codes_print_result(const char api_name[], int8_t rslt)
{
    if (rslt != BMP2_OK)
    {
        printf("%s\t", api_name);

        switch (rslt)
        {
            case BMP2_E_NULL_PTR:
                printf("Error [%d] : Null pointer error.", rslt);
                printf(
                    "It occurs when the user tries to assign value (not address) to a pointer, which has been initialized to NULL.\r\n");
                break;
            case BMP2_E_COM_FAIL:
                printf("Error [%d] : Communication failure error.", rslt);
                printf(
                    "It occurs due to read/write operation failure and also due to power failure during communication\r\n");
                break;
            case BMP2_E_INVALID_LEN:
                printf("Error [%d] : Invalid length error.", rslt);
                printf("Occurs when length of data to be written is zero\n");
                break;
            case BMP2_E_DEV_NOT_FOUND:
                printf("Error [%d] : Device not found error. It occurs when the device chip id is incorrectly read\r\n",
                       rslt);
                break;
            case BMP2_E_UNCOMP_TEMP_RANGE:
                printf("Error [%d] : Uncompensated temperature data not in valid range error.", rslt);
                break;
            case BMP2_E_UNCOMP_PRESS_RANGE:
                printf("Error [%d] : Uncompensated pressure data not in valid range error.", rslt);
                break;
            case BMP2_E_UNCOMP_TEMP_AND_PRESS_RANGE:
                printf(
                    "Error [%d] : Uncompensated pressure data and uncompensated temperature data are not in valid range error.",
                    rslt);
                break;
            default:
                printf("Error [%d] : Unknown error code\r\n", rslt);
                break;
        }
    }
}