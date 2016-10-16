#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "max31855_config.h"

#define MAX31855_OK                 0x0
#define MAX31855_PROBE_FAULT        0x1

#define MAX31855_FLAG_NO_PROBE      0x1
#define MAX31855_FLAG_SHORT_GND     0x2
#define MAX31855_FLAG_SHORT_VCC     0x4

struct max31855_dev {
    /**
     * The SPI Bus ID to use
     */
    uint8_t spi_bus;

    /**
     * The GPIO to be used as a chip select
     */
    uint8_t cs_gpio;

    /**
     * Status flags for the MAX31855
     */
    uint8_t flags;

    /**
     * The last measured probe temperature. Not valid if flags != 0
     */
    uint32_t probe_temp;

    /**
     * Internal calibration temperature.
     */
    uint32_t int_temp;
};

/**
 * Initialize a MAX31855 Status structure.
 *
 * \param dev The status structure to be initialized
 * \param spi_bus The SPI bus ID to use
 * \param csn_gpio The GPIO ID for the chip select.
 */
void max31855_init(struct max31855_dev *dev, unsigned spi_bus, unsigned csn_gpio);

/**
 * Read the temperature from the attached MAX31855. Any parameters you're not interested
 * in can be set to NULL.
 *
 * \param probe_temp The probe temerature, in millidegrees celsius
 * \param internal_temp The internal temperature, in millidegrees celsius
 * \param no_probe Boolean. Set to true if no probe is present.
 * \param short_ground Boolean. Set to true if the probe connector is shorted to ground.
 * \param short_vcc Boolean. Set to true if the probe connector is shorted to Vcc.
 *
 * \return MAX31855_OK if the values read are correct, MAX31855_PROBE_FAULT if there is an
 *         error with the probe configuration.
 */
int max31855_read(uint32_t *probe_temp, uint32_t *internal_temp, bool *no_probe, bool *short_ground, bool *short_vcc);

