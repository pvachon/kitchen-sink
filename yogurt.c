#include <driver/spi_interface.h>
#include <driver/uart.h>

#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include <user_interface.h>

#include "c99_fixups.h"
#include "max31855.h"
#include "sh1106.h"

#include <stdint.h>

static volatile
os_timer_t temp_timer;

static
uint32_t probe_temp = 0;

static
uint32_t int_temp = 0;

static
bool no_probe = false;

static
bool short_vcc = false;

static
bool short_ground = false;

static
bool wifi_connected = false;

static
bool wifi_changed = false;

static
int wifi_last_status = STATION_IDLE;

static
bool temp_showing = false;

static
char ssid[32] = "SiprExtend",
     psk[64] = "lolnsaownsyou";

static ICACHE_FLASH_ATTR
void setup_wifi_interface(void)
{
    struct station_config wifi_sta_cfg;

    /* Fire up the wifi interface */
    ETS_UART_INTR_DISABLE();
    wifi_set_opmode(STATION_MODE);
    ETS_UART_INTR_ENABLE();

    os_memcpy(&wifi_sta_cfg.ssid, ssid, 32);
    os_memcpy(&wifi_sta_cfg.password, psk, 64);
    wifi_sta_cfg.bssid_set = 0;
    wifi_station_set_config(&wifi_sta_cfg);

    os_printf("WIFI: SSID=%s PSK=%s\r\n", wifi_sta_cfg.ssid, wifi_sta_cfg.password);

    if (true == wifi_station_connect()) {
        os_printf("WIFI: Connected.\r\n");
    }
}

static ICACHE_FLASH_ATTR
void redraw_display(void)
{
    char temp_str[16];

    if (true == wifi_changed) {
        /* Draw the top line, inverted */
        sh1106_clear_page(0, true);
        sh1106_display_puts(0, 102, "WiFi", true, SH1106_TEXT_ALIGN_RIGHT);

        if (STATION_GOT_IP == wifi_last_status) {
            sh1106_display_puts(0, 2, ssid, true, SH1106_TEXT_ALIGN_LEFT);
        } else {
            switch (wifi_last_status) {
            case STATION_IDLE:
                sh1106_display_puts(0, 2, "Not Connected", true, SH1106_TEXT_ALIGN_LEFT);
                break;
            case STATION_WRONG_PASSWORD:
                sh1106_display_puts(0, 2, "Bad WPA PSK", true, SH1106_TEXT_ALIGN_LEFT);
                break;
            case STATION_CONNECTING:
                sh1106_display_puts(0, 2, "Connecting", true, SH1106_TEXT_ALIGN_LEFT);
                break;
            case STATION_NO_AP_FOUND:
                sh1106_display_puts(0, 2, "WiFi Timeout", true, SH1106_TEXT_ALIGN_LEFT);
                break;
            case STATION_CONNECT_FAIL:
                sh1106_display_puts(0, 2, "Unable to Connect", true, SH1106_TEXT_ALIGN_LEFT);
                break;
            default:
                sh1106_display_puts(0, 2, "WiFi Failure", true, SH1106_TEXT_ALIGN_LEFT);
                break;
            }
        }
        wifi_changed = false;
    }

    if (false == no_probe && false == short_vcc && false == short_ground) {
        if (false == temp_showing) {
            sh1106_clear_page(2, false);
            sh1106_clear_page(3, false);
            sh1106_display_puts(2, 2, "Therm 1:", false, SH1106_TEXT_ALIGN_LEFT);
            sh1106_display_puts(3, 2, "Calib:", false, SH1106_TEXT_ALIGN_LEFT);
        }

        os_sprintf(temp_str, "%u.%02u00" "\xb0" "C", probe_temp >> 2, (probe_temp & 0x3) * 25);
        temp_str[15] = '\0';
        sh1106_display_puts(2, 0, temp_str, false, SH1106_TEXT_ALIGN_RIGHT);

        os_sprintf(temp_str, "%u.%04u" "\xb0" "C", int_temp >> 4, (int_temp & 0xf) * 625);
        temp_str[15] = '\0';
        sh1106_display_puts(3, 0, temp_str, false, SH1106_TEXT_ALIGN_RIGHT);
        temp_showing = true;
    } else {
        if (true == temp_showing) {
            sh1106_clear_page(2, false);
            sh1106_clear_page(3, false);
        }

        sh1106_clear_page(3, false);
        if (true == no_probe) {
            sh1106_display_puts(3, 0, "Connect Probe", false, SH1106_TEXT_ALIGN_CENTER);
        } else {
            sh1106_display_puts(3, 0, "Probe Error", false, SH1106_TEXT_ALIGN_CENTER);
        }

        temp_showing = false;
    }
}

static ICACHE_FLASH_ATTR
void check_wifi(void)
{
    int wifi_status = wifi_station_get_connect_status();

    switch (wifi_status) {
    case STATION_IDLE:
        setup_wifi_interface();
        break;
    case STATION_CONNECTING:
        os_printf("WIFI: Still attempting to connect.\r\n");
        break;
    case STATION_WRONG_PASSWORD:
        os_printf("WIFI: Wrong password for wifi, aborting.\r\n");
        break;
    case STATION_NO_AP_FOUND:
        os_printf("WIFI: Could not find specified wifi AP, aborting\r\n");
        break;
    case STATION_CONNECT_FAIL:
        os_printf("WIFI: Connection failed. Retrying.\r\n");
        setup_wifi_interface();
        break;
    case STATION_GOT_IP:
        wifi_connected = true;
        break;
    default:
        os_printf("Warning: unknown wifi network status: %d\r\n", wifi_status);
    }

    if (wifi_last_status != wifi_status) {
        wifi_last_status = wifi_status;
        wifi_changed = true;
    }
}

static ICACHE_FLASH_ATTR
void sample_temperature(void *arg)
{
    /* Check the status of Wifi before we move along */
    check_wifi();

    if (MAX31855_PROBE_FAULT == max31855_read(&probe_temp, &int_temp, &no_probe, &short_ground, &short_vcc)) {
        if (no_probe) {
            os_printf("ERROR: no probe present.\r\n");
        }

        if (short_ground) {
            os_printf("ERROR: short to ground detected.\r\n");
        }

        if (short_vcc) {
            os_printf("ERROR: short to Vcc detected.\r\n");
        }
    }

    redraw_display();
}

ICACHE_FLASH_ATTR
void user_init(void)
{
    SpiAttr spi_attr;

    /* Initialize the UART */
    uart_init(BIT_RATE_115200, BIT_RATE_115200);

    /*
     * Initialize the GPIO subsystem
     */
    gpio_init();

    /*
     * Print a welcome message
     */
    os_printf("Yogurt Monitor is Starting...\r\n");

    /* Set up the GPIOs for the hardware SPI */
    WRITE_PERI_REG(PERIPHS_IO_MUX, 0x105);
    /* HSPI MISO */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, 2);
    /* HSPI MOSI */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 2);
    /* HSPI CLK */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, 2);
    /* MAX31855 Chip Select */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);

    /* Set up the SPI interface */
    spi_attr.bitOrder = SpiBitOrder_MSBFirst;
    spi_attr.speed = SpiSpeed_10MHz;
    spi_attr.mode = SpiMode_Master;
    spi_attr.subMode = SpiSubMode_0;
    SPIInit(SpiNum_HSPI, &spi_attr);

    /* Configure the chip select for the MAX31855 */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
    gpio_output_set(0, 0, (1 << MAX31855_SPI_CSN), 0);
    gpio_output_set((1 << MAX31855_SPI_CSN), 0, 0, 0);

    /* Configure the chip selects for the SH1106 */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);    /* Chip select */
    //PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);    /* A0/Display Write Select */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO2);    /* A0/Display Write Select */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);    /* Display chip reset */

    /* Enable the display control GPIOs and assert them, holding the display in reset */
    gpio_output_set(0, 0, (1 << SH1106_SPI_CSN) | (1 << SH1106_SPI_A0) | (1 << SH1106_SPI_RSTN), 0);
    gpio_output_set((1 << SH1106_SPI_A0),  (1 << SH1106_SPI_RSTN) | (1 << SH1106_SPI_CSN), 0, 0);

    /* Enable the SH1106-based display */
    sh1106_display_init(0x80);
    sh1106_display_set_invert(false);

    /* Arm event timer (500ms, repeating) to sample the temperature probe */
    os_timer_disarm((os_timer_t *)&temp_timer);
    os_timer_setfn((os_timer_t *)&temp_timer, (os_timer_func_t *)sample_temperature, NULL);
    os_timer_arm((os_timer_t *)&temp_timer, 500, 1);
}

