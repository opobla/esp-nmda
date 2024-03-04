PHONY = flash

settings.bin: settings.csv
	# The 1884160 (0x1CC000) is the size specified in the partitions.csv for the
	# NVS partition. In our case, this is the nvs_keys partition, not nvs, which
	# is used for wifi parameters
	${IDF_PATH}/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate settings.csv settings.bin 1884160

flash: settings.bin
	python ${IDF_PATH}/components/nvs_flash/nvs_partition_tool/nvs_tool.py -i settings.bin
	esptool.py write_flash 0x630000 settings.bin

clean:
	rm -f settings.bin
