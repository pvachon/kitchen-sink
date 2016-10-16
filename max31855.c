#include "max31855.h"

#include <driver/spi_interface.h>

#include <c99_fixups.h>
#include <gpio.h>
#include <osapi.h>

#define MAX31855_OC_BIT             (1ul << 0)
#define MAX31855_SCG_BIT            (1ul << 1)
#define MAX31855_SCV_BIT            (1ul << 2)

#define MAX31855_INTERNAL_TEMP(x)   (((x) >> 4) & 0xfff)
#define MAX31855_FAULT_BIT          (1ul << 16)
#define MAX31855_THERMO_TEMP(x)     (((x) >> 18) & 0x3fff)


static ICACHE_FLASH_ATTR
uint32_t _max31855_spi_read(struct max31855_dev *dev)
{
    SpiData data_rx;
    uint32_t v = 0;

    data_rx.cmd = MASTER_READ_DATA_FROM_SLAVE_CMD;
    data_rx.cmdLen = 0;
    data_rx.addr = NULL;
    data_rx.addrLen = 0;
    data_rx.data = (void *)&v;
    data_rx.dataLen = 4;

    /* Assert the chip select for the MAX31855 */
    gpio_output_set(0, (1 << dev->cs_gpio), 0, 0);

    /* Receive 32 bits from the interface */
    if (0 > SPIMasterRecvData(dev->spi_bus, &data_rx)) {
        os_printf("MAX31855: Failed to receive %u bytes.\r\n", data_rx.dataLen);
    }

    /* De-sert the GPIO */
    gpio_output_set((1 << dev->cs_gpio), 0, 0, 0);

    v = __builtin_bswap32(v);

    return v;
}

ICACHE_FLASH_ATTR
int max31855_init(struct max31855_dev *dev, unsigned spi_bus, unsigned csn_gpio)
{
    int status = MAX31855_OK;

    memset(dev, 0, sizeof(*dev));

    if (spi_bus > SpiNum_HSPI) {
        os_printf("MAX31855: Error: SPI bus must be SpiNum_HSPI or SpiNum_SPI.\r\n");
        status = MAX31855_BAD_ARGS;
        goto done;
    }

    if (csn_gpio > 15) {
        os_printf("MAX31855: Error: CSN GPIO must be less than or equal to 15\r\n");
        status = MAX31855_BAD_ARGS;
        goto done;
    }

    dev->spi_bus = spi_bus;
    dev->cs_gpio = csn_gpio;
    dev->flags = MAX31855_FLAG_NO_PROBE;

done:
    return status;
}

ICACHE_FLASH_ATTR
int max31855_read(struct max31855_dev *dev)
{
    int status = MAX31855_OK;
    uint32_t v = 0;

    v = _max31855_spi_read(dev);

    os_printf("Clear flags\r\n");

    dev->flags = 0;

    os_printf("Set flags\r\n");
    if ((MAX31855_OC_BIT & v)) {
        dev->flags |= MAX31855_FLAG_NO_PROBE;
        status = MAX31855_PROBE_FAULT;
    }

    if ((MAX31855_SCG_BIT & v)) {
        dev->flags |= MAX31855_FLAG_SHORT_GND;
        status = MAX31855_PROBE_FAULT;
    }

    if ((MAX31855_SCV_BIT & v)) {
        dev->flags |= MAX31855_FLAG_SHORT_VCC;
        status = MAX31855_PROBE_FAULT;
    }

    os_printf("Set probe temp\r\n");
    dev->probe_temp = MAX31855_THERMO_TEMP(v);
    os_printf("Set internal temp\r\n");
    dev->int_temp = MAX31855_INTERNAL_TEMP(v);
    os_printf("Done.\r\n");

    return status;
}

