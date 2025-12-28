PHONY = flash flash-4mb flash-8mb

# CSV file to use (default: settings.csv)
# Can be overridden: make flash CSV=settings.op64.csv
CSV ?= settings.csv

# Detect flash size from sdkconfig or sdkconfig.defaults
FLASH_SIZE := $(shell if [ -f ../sdkconfig ] && grep -q "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" ../sdkconfig; then echo "8mb"; elif [ -f ../sdkconfig.defaults ] && grep -q "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" ../sdkconfig.defaults; then echo "8mb"; else echo "4mb"; fi)

# NVS partition settings based on flash size
ifeq ($(FLASH_SIZE),8mb)
	NVS_OFFSET := 0x630000
	NVS_SIZE := 1884160
	NVS_SIZE_HEX := 0x1CC000
else
	NVS_OFFSET := 0x3E0000
	NVS_SIZE := 131072
	NVS_SIZE_HEX := 0x20000
endif

# Output binary file name (based on input CSV)
BIN := $(CSV:.csv=.bin)

$(BIN): $(CSV)
	@echo "Generating $(BIN) from $(CSV) for $(FLASH_SIZE) flash (size: $(NVS_SIZE_HEX))"
	# The size is specified in the partitions.csv for the NVS partition.
	# For 8MB: 0x1CC000 (1884160 bytes)
	# For 4MB: 0x20000 (131072 bytes)
	${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate $(CSV) $(BIN) $(NVS_SIZE)

flash: $(BIN)
	@echo "Flashing $(BIN) to offset $(NVS_OFFSET) (detected: $(FLASH_SIZE))"
	@echo "Using CSV file: $(CSV)"
	python ${IDF_PATH}/components/nvs_flash/nvs_partition_tool/nvs_tool.py -i $(BIN)
	esptool.py write_flash $(NVS_OFFSET) $(BIN)

flash-4mb: $(CSV)
	@echo "Generating and flashing for 4MB flash"
	@echo "Using CSV file: $(CSV)"
	${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate $(CSV) $(BIN) 131072
	python ${IDF_PATH}/components/nvs_flash/nvs_partition_tool/nvs_tool.py -i $(BIN)
	esptool.py write_flash 0x3E0000 $(BIN)

flash-8mb: $(CSV)
	@echo "Generating and flashing for 8MB flash"
	@echo "Using CSV file: $(CSV)"
	${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate $(CSV) $(BIN) 1884160
	python ${IDF_PATH}/components/nvs_flash/nvs_partition_tool/nvs_tool.py -i $(BIN)
	esptool.py write_flash 0x630000 $(BIN)

clean:
	rm -f *.bin
