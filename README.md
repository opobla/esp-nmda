# Neutron Monitor Data Acquisition


## Partitiones 

El ESP32 tiene la posibilidad de particionar su memoria interna no volátil de 8Mb. Cada una de estas particiones se usa para un fin específico. Cada partición se identifica con una etiqueta. En concreto en nuestro sistema `esp-nmda`, la tabla de particiones está definida en `partitions/partitions.csv` y tiene el siguiente contenido:

```
# Name,       Type, SubType, Offset,   Size,  Flags
nvs,          data, nvs,     0x9000,   0x24000
otadata,      data, ota,     0x2D000,  0x2000
phy_init,     data, phy,     0x2F000,  0x1000
factory,      app,  factory, 0x30000,  0x200000
ota_0,        app,  ota_0,   0x230000, 0x200000
ota_1,        app,  ota_1,   0x430000, 0x200000
nvs_settings, data, nvs,     0x630000, 0x1CC000
```

Las posiciones concretas y el tamaño de cada partición has hice con la ayuda de [esta hoja de cálculo](https://docs.google.com/spreadsheets/d/1GGQgFF905QJ1zDRdo4AWnYGhVPz9GH1OXI0z4Kxn5Zk/edit#gid=0).

Para que la cadena de compilación haga uso de este archivo de configuración, es necesario especificarlo en `sdkconfig`, concretamente indicando las siguientes variables:

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions/partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
```

La tabla de particiones define cómo se divide la memoria flash en un dispositivo ESP32 y especifica el propósito de cada partición. Aquí tienes una explicación del propósito de cada partición:

1. **nvs** (Type: data, SubType: nvs):
   - **Offset**: 0x9000
   - **Size**: 0x24000 (144 KB)
   - **Flags**: Esta partición almacena datos no volátiles (NVS) utilizados para configuración y almacenamiento de datos en la memoria flash. Los datos en esta partición son persistentes y se utilizan para configurar diversos aspectos del dispositivo, como la configuración de Wi-Fi, la configuración de la aplicación, etc.

2. **otadata** (Type: data, SubType: ota):
   - **Offset**: 0x2D000
   - **Size**: 0x2000 (8 KB)
   - **Flags**: Esta partición almacena datos relacionados con las actualizaciones "Over-The-Air" (OTA) del firmware. Puede contener información sobre la versión actual del firmware y otros datos relacionados con las actualizaciones OTA.

3. **phy_init** (Type: data, SubType: phy):
   - **Offset**: 0x2F000
   - **Size**: 0x1000 (4 KB)
   - **Flags**: Esta partición almacena datos de inicialización específicos del hardware relacionados con el funcionamiento de la capa física (PHY) de la comunicación inalámbrica. Es esencial para la configuración y el funcionamiento correcto de las comunicaciones inalámbricas.

4. **factory** (Type: app, SubType: factory):
   - **Offset**: 0x30000
   - **Size**: 0x200000 (2 MB)
   - **Flags**: Esta partición almacena la imagen del firmware de fábrica. Es el firmware original del dispositivo antes de cualquier actualización OTA. Siempre se puede volver a esta imagen si es necesario.

5. **ota_0** (Type: app, SubType: ota_0):
   - **Offset**: 0x230000
   - **Size**: 0x200000 (2 MB)
   - **Flags**: Esta partición almacena una de las imágenes del firmware para las actualizaciones OTA. En el caso de actualizaciones OTA, el firmware nuevo se carga en una de estas particiones mientras que la otra partición (ota_1 en este caso) contiene el firmware en uso actualmente. Esto permite cambiar a la nueva imagen después de una actualización exitosa.

6. **ota_1** (Type: app, SubType: ota_1):
   - **Offset**: 0x430000
   - **Size**: 0x200000 (2 MB)
   - **Flags**: Al igual que ota_0, esta partición almacena una de las imágenes del firmware para las actualizaciones OTA. Durante una actualización, se utiliza una partición mientras que la otra contiene el firmware actual en uso.

7. **nvs_settings** (Type: data, SubType: nvs):
   - **Offset**: 0x630000
   - **Size**: 0x1CC000 (1.99 MB)
   - **Flags**: Esta partición adicional almacena datos NVS para configuración y almacenamiento de datos adicionales. En concreto, los valores de esta partición permiten configurar cada uno de los sistemas de adquisición de forma particular. 

   En el siguiente apartado se explica cómo configurar cada sistema a través de los parámetros de configuración que grabaremos externamente en la partición nvs_settings.

## Datos específicos de configuración

Los datos espécificos de configuración se generan a partir de un archivo que se encuentra en `partitions/settings.csv`. Existe en ese mismo directorio un ejemplo `settings.sample.csv`.

Para generar el archivo binario correspondiente se hace `make`, y si se quiere flashear la nvs del dispositivo con el contenido de este archivo se puede hacer `make flash`.
