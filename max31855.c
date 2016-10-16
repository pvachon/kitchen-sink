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
uint32_t _max31855_spi_read(void)
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
    gpio_output_set(0, (1 << MAX31855_SPI_CSN), 0, 0);

    /* Receive 32 bits from the interface */
    if (0 > SPIMasterRecvData(MAX31855_SPI_IFACE, &data_rx)) {
        os_printf("MAX31855: Failed to receive %u bytes.\r\n", data_rx.dataLen);
    }

    /* De-sert the GPIO */
    gpio_output_set((1 << MAX31855_SPI_CSN), 0, 0, 0);

    v = __builtin_bswap32(v);

    return v;
}

ICACHE_FLASH_ATTR
int max31855_read(uint32_t *probe_temp, uint32_t *internal_temp, bool *no_probe, bool *short_ground, bool *short_vcc)
{
    int status = MAX31855_OK;
    uint32_t v = 0;

    v = _max31855_spi_read();

    if (NULL != no_probe) {
        bool val = !!(MAX31855_OC_BIT & v);
        *no_probe = val;
        if (true == val) {
            status = MAX31855_PROBE_FAULT;
        }
    }

    if (NULL != short_ground) {
        bool val = !!(MAX31855_SCG_BIT & v);
        *short_ground = val;
        if (true == val) {
            status = MAX31855_PROBE_FAULT;
        }
    }

    if (NULL != short_vcc) {
        bool val = !!(MAX31855_SCV_BIT & v);
        *short_vcc = val;
        if (true == val) {
            status = MAX31855_PROBE_FAULT;
        }
    }

    /* TODO: Sign extend */
    if (NULL != probe_temp) {
        *probe_temp = MAX31855_THERMO_TEMP(v);
    }

    if (NULL != internal_temp) {
        *internal_temp = MAX31855_INTERNAL_TEMP(v);
    }

    return status;
}

