#include <driver/spi_interface.h>
#include <driver/uart.h>

#include <osapi.h>
#include <gpio.h>
#include <os_type.h>
#include <user_interface.h>

#include "c99_fixups.h"
#include "max31855.h"
#include "sh1106.h"
#include "http_client.h"

#include <stdint.h>

#define ARRAY_LEN(x) (sizeof((x))/sizeof((x[0])))
#define ALIGN(x)        __attribute__((aligned((x))))

struct thermo_probe {
    struct max31855_dev dev ALIGN(8);
    bool changed;
    bool enabled;
    bool temp_showing;
    int line;
} ALIGN(4);

static volatile
os_timer_t temp_timer;

static
struct http_client http_cl = { .state = HTTP_CLIENT_IDLE };

static
struct thermo_probe thermo_devs[2] ALIGN(4);

static
bool wifi_connected = false;

static
bool wifi_changed = false;

static
int wifi_last_status = STATION_IDLE;

/* Backoff for HTTP server TCP connection */
static
int backoff = 0;

#define MAX_BACKOFF     20

static
char ssid[32] = "SiprExtend",
     psk[64] = "lolnsaownsyou";

/**
 * Setup the wifi interface parameters, and initiate a connection to the STA.
 */
static ICACHE_FLASH_ATTR
void setup_wifi_interface(void)
{
    struct station_config wifi_sta_cfg;

    os_memcpy(&wifi_sta_cfg.ssid, ssid, 32);
    os_memcpy(&wifi_sta_cfg.password, psk, 64);
    wifi_sta_cfg.bssid_set = 0;
    wifi_station_set_config(&wifi_sta_cfg);

    os_printf("WIFI: SSID=%s PSK=%s\r\n", wifi_sta_cfg.ssid, wifi_sta_cfg.password);

    if (true == wifi_station_connect()) {
        os_printf("WIFI: Connecting.\r\n");
    }
}

/**
 * Set up the HTTP client connection, and periodically check its status.
 *
 * Attempt to automatically reconnect if the connection to the service failed.
 */
static ICACHE_FLASH_ATTR
void check_http_client_conn(void)
{
    ip_addr_t addr;

    if (HTTP_CLIENT_CONNECTED == http_cl.state || HTTP_CLIENT_CONNECTING == http_cl.state) {
        /* Nothing to do here */
        return;
    }

    if (HTTP_CLIENT_IDLE == http_cl.state || MAX_BACKOFF == backoff) {
        IP4_ADDR(&addr, 172, 16, 1, 1);
        if (0 != http_client_connect(&http_cl, addr.addr, 24666)) {
            os_printf("Network connection failure, skipping.\r\n");
        }
        backoff = 0;
    } else {
        if (0 == backoff) {
            os_printf("Waiting 10s before retrying connection...\r\n");
        }
        backoff++;
    }
}

static
char message[256];

/**
 * Send a temperature update to the remote service
 */
static ICACHE_FLASH_ATTR
void update_service(void)
{
}

/**
 * Update the information displayed on the OLED.
 */
static ICACHE_FLASH_ATTR
void redraw_display(void)
{
    char temp_str[32];

    /* Check if we need to redraw the Wifi network status */
    if (true == wifi_changed) {
        /* Draw the top line, inverted */
        sh1106_clear_page(0, true, 0);
        sh1106_display_puts(0, 102, "WiFi", true, SH1106_TEXT_ALIGN_RIGHT);

        if (STATION_GOT_IP == wifi_last_status) {
            sh1106_display_puts(0, 0, ssid, true, SH1106_TEXT_ALIGN_LEFT);
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

    for (int i = 0; i < ARRAY_LEN(thermo_devs); i++) {
        struct thermo_probe *probe = &thermo_devs[i];

        /* Probe attributes changed */
        if (true == probe->changed) {
            /* Display message indicating probe is not active */
            os_sprintf(temp_str, "Probe %d: ", i + 1);
            temp_str[31] = '\0';
            sh1106_display_puts(probe->line, 0, temp_str, false, SH1106_TEXT_ALIGN_LEFT);
            if (false == probe->enabled) {
                sh1106_display_puts(probe->line, 0, "Inactive", false, SH1106_TEXT_ALIGN_RIGHT);
                probe->changed = false;
            }
        }

        if (true == probe->enabled) {
            struct max31855_dev *dev = &probe->dev;

            if (0 == dev->flags) {
                if (false == probe->temp_showing) {
                    sh1106_clear_page(probe->line, false, 64);
                }

                os_sprintf(temp_str, "%u.%02u" "\xb0" "C", dev->probe_temp >> 2, (dev->probe_temp & 0x3) * 25);
                temp_str[31] = '\0';
                sh1106_display_puts(probe->line, 0, temp_str, false, SH1106_TEXT_ALIGN_RIGHT);

                probe->temp_showing = true;
            } else {
                if (true == probe->temp_showing) {
                    sh1106_clear_page(probe->line, false, 64);
                }

                if (dev->flags & MAX31855_FLAG_NO_PROBE) {
                    sh1106_display_puts(probe->line, 0, "Disconnected", false, SH1106_TEXT_ALIGN_RIGHT);
                } else {
                    os_sprintf(temp_str, "%s Short", dev->flags & MAX31855_FLAG_SHORT_GND ? "Ground" : "Vcc");
                    temp_str[31] = '\0';
                    sh1106_display_puts(probe->line, 0, temp_str, false, SH1106_TEXT_ALIGN_RIGHT);
                }

                probe->temp_showing = false;
            }
        }
    }

}

/**
 * Check and update the status of the Wifi connection. If the status indicates we're not connected, attempt to force a reconnect.
 */
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

/**
 * The meat of our polling loop. Check the wifi, sample the temperature from the thermocouples, and send
 * a message out.
 */
static ICACHE_FLASH_ATTR
void sample_temperature(void *arg)
{
    /* Check the status of Wifi before we move along */
    check_wifi();

    for (int i = 0; i < ARRAY_LEN(thermo_devs); i++) {
        struct thermo_probe *dev = &thermo_devs[i];

        if (true == dev->enabled) {
            max31855_read(&dev->dev);
        }
    }

    /* Check our HTTP connection status if WiFi is up */
    if (STATION_GOT_IP == wifi_last_status) {
        check_http_client_conn();
    }

    update_service();
    redraw_display();
}

/**
 * Set up the specified temperature probe, so we can talk to the MAX31855.
 *
 * \param id The internal ID used for this temperature probe
 * \param enable Whether or not to mark this temperature probe as enabled
 * \param csn_id The ID of the GPIO to be used as a chip select.
 *
 * \return 0 on success, a non-zero error code otherwise.
 */
static ICACHE_FLASH_ATTR
int setup_temp_probe(int id, bool enable, int csn_id)
{
    int status = 0;
    struct thermo_probe *probe = NULL;

    if (id >= ARRAY_LEN(thermo_devs)) {
        status = -1;
        os_printf("ERROR: Probe %d is not configured to exist.\r\n", id);
        goto done;
    }

    probe = &thermo_devs[id];
    probe->enabled = enable;
    probe->line = 2 + id;
    probe->temp_showing = false;
    probe->changed = true;

    if (true == enable) {
        if (0 != max31855_init(&probe->dev, MAX31855_SPI_IFACE, csn_id)) {
            os_printf("ERROR: Failed to initialize probe %d state\r\n", id);
            status = -1;
            goto done;
        }
    }

done:
    return status;
}

/**
 * Initialization entry point.
 */
ICACHE_FLASH_ATTR
void user_init(void)
{
    SpiAttr spi_attr;

    /* Initialize the UART */
    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    os_delay_us(100);

    /*
     * Initialize the GPIO subsystem
     */
    gpio_init();

    /*
     * Print a welcome message
     */
    os_printf("Yogurt Monitor is Starting...\r\n");

    /* Fire up the wifi interface */
    ETS_UART_INTR_DISABLE();
    wifi_set_opmode(STATION_MODE);
    ETS_UART_INTR_ENABLE();

    /* Set up the GPIOs for the hardware SPI */
    WRITE_PERI_REG(PERIPHS_IO_MUX, 0x105);
    /* HSPI MISO */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, 2);
    /* HSPI MOSI */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 2);
    /* HSPI CLK */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, 2);

    /* Set up the SPI interface */
    spi_attr.bitOrder = SpiBitOrder_MSBFirst;
    spi_attr.speed = SpiSpeed_10MHz;
    spi_attr.mode = SpiMode_Master;
    spi_attr.subMode = SpiSubMode_0;
    SPIInit(SpiNum_HSPI, &spi_attr);

    /* Configure the chip select for the MAX31855 */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
    gpio_output_set(0, 0, (1 << MAX31855_SPI_CSN), 0);
    gpio_output_set((1 << MAX31855_SPI_CSN), 0, 0, 0);

    setup_temp_probe(0, true, MAX31855_SPI_CSN);
    setup_temp_probe(1, false, 0);

    /* Configure the chip selects for the SH1106 */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);    /* Chip select */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);    /* A0/Display Write Select */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);    /* Display chip reset */

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

