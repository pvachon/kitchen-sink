/** \file sh1106.c OLED Display Driver
 * Library for a 4-wire SPI variant of an OLED controlled by the SH1106 OLED
 * display controller.
 */

#include <sh1106.h>
#include <sh1106_cmds.h>

#include "font_5x7.h"

#include <driver/spi_interface.h>
#include <gpio.h>
#include <osapi.h>
#include <c99_fixups.h>

#define OLED_WIDTH  128
#define OLED_HEIGHT 64

#define OLED_SPI_NUM        SpiNum_HSPI

static ICACHE_FLASH_ATTR
void _spi_write(uint32_t *data, size_t nr_bytes)
{
    SpiData data_tx;

    data_tx.cmd = MASTER_WRITE_DATA_TO_SLAVE_CMD;
    data_tx.cmdLen = 0;
    data_tx.addr = NULL;
    data_tx.addrLen = 0;
    data_tx.data = data;
    data_tx.dataLen = nr_bytes;

    if (0 > SPIMasterSendData(OLED_SPI_NUM, &data_tx)) {
        os_printf("SH1106: Could not send data via SPI\r\n");
    }

    /* WORKAROUND: The upstream SPI driver doesn't actually wait until the transaction has
     * finished, even though it will purport that it has.
     */
    while (READ_PERI_REG(SPI_CMD(OLED_SPI_NUM)) & SPI_USR);
}

static ICACHE_FLASH_ATTR
void _spi_write_display(uint32_t *buffer, size_t nr_bytes)
{
    /* Set the CS GPIO */
    gpio_output_set((1 << SH1106_SPI_A0), (1 << SH1106_SPI_CSN), 0, 0);

    /* Write out the data */
    _spi_write(buffer, nr_bytes);

    /* De-assert the chip select */
    gpio_output_set((1 << SH1106_SPI_CSN), 0, 0, 0);
}

static ICACHE_FLASH_ATTR
void _spi_write_command(uint32_t *cmd, size_t nr_bytes)
{
    /* Set the CS GPIO. Make sure A0 is de-asserted as well, so this gets treated as a command. */
    gpio_output_set(0, (1 << SH1106_SPI_CSN) | ( 1 << SH1106_SPI_A0), 0, 0);

    /* Write the command out via SPI */
    _spi_write(cmd, nr_bytes);

    /* Clear the CS GPIO */
    gpio_output_set((1 << SH1106_SPI_CSN), 0 , 0, 0);
}

static inline ICACHE_FLASH_ATTR
void _sh1107_set_start_line(uint8_t line)
{
    uint32_t cmd = SH1106_CMD_SET_START_LINE(line);
    _spi_write_command(&cmd, 1);
}

static inline ICACHE_FLASH_ATTR
void _sh1106_set_start_column(uint8_t col)
{
    uint32_t cmd = (SH1106_CMD_SET_HIGH_COL_ADDR(col) << 8) | SH1106_CMD_SET_LOW_COL_ADDR(col);
    _spi_write_command(&cmd, 2);
}


static ICACHE_FLASH_ATTR
void _sh1106_checkerboard_test(void)
{
    uint32_t display_line[(OLED_WIDTH+31)/32],
             line_val = 0;
    int page = 0;

    for (size_t i = 0; i < sizeof(display_line)/sizeof(display_line[0]); i++) {
        if (0 == i % 2) {
            line_val = ~line_val;
        }
        display_line[i] = line_val;
    }


    for (size_t i = 0; i < OLED_HEIGHT; i++) {
        if (0 == i % 8) {
            uint32_t page_cmd = SH1106_CMD_SET_PAGE_ADDR(page);

            for (size_t j = 0; j < sizeof(display_line)/sizeof(display_line[0]); j++) {
                display_line[j] = ~display_line[j];
            }

            /* Open the next page */
            _spi_write_command(&page_cmd, 1);
            _sh1106_set_start_column(2);
            page++;
        }
        _spi_write_display(display_line, sizeof(display_line));
    }
}

ICACHE_FLASH_ATTR
void sh1106_display_set_invert(bool invert)
{
    uint32_t cmd_invert = SH1106_CMD_INVERT_DISPLAY(invert);
    _spi_write_command(&cmd_invert, 1);
}

ICACHE_FLASH_ATTR
void sh1106_clear_page(int page, bool invert, int start_col)
{
    uint32_t data[(OLED_WIDTH + 31)/32],
             page_cmd = SH1106_CMD_SET_PAGE_ADDR(page);

    memset(data, invert ? 0xff : 0x0, sizeof(data));

    _spi_write_command(&page_cmd, 1);
    _sh1106_set_start_column(start_col + 2);

    for (int i = 0; i < OLED_HEIGHT/8; i++) {
        _spi_write_display(data, (OLED_WIDTH - start_col)/8);
    }
}

ICACHE_FLASH_ATTR
void sh1106_display_clear(void)
{
    for (int i = 0; i < 8; i++) {
        sh1106_clear_page(i, false, 0);
    }
}

static ICACHE_FLASH_ATTR
void _sh1106_display_putc(int c, bool invert)
{
    uint32_t character;
    character = (uint32_t)font_data[(c * 5) + 0] |
        ((uint32_t)font_data[(c * 5) + 1] << 8)     |
        ((uint32_t)font_data[(c * 5) + 2] << 16)    |
        ((uint32_t)font_data[(c * 5) + 3] << 24);

    if (true == invert) {
        character = ~character;
    }

    _spi_write_display(&character, 4);

    character =
        ((uint32_t)font_data[(c * 5) + 4] << 0);

    if (true == invert) {
        character = ~character;
    }
    _spi_write_display(&character, 2);
}

ICACHE_FLASH_ATTR
void sh1106_display_puts(unsigned line, unsigned x_offs, const char *str, bool invert, enum sh1106_text_align align)
{
    uint32_t page_cmd = SH1106_CMD_SET_PAGE_ADDR(line),
             x_start = x_offs;
    const char *pstr = str;
    int len = 0;

    len = os_strlen(str);
    if (0 == len) {
        goto done;
    }

    if (len * 6 >= 124) {
        x_start = 2;
    } else {
        switch (align) {
        case SH1106_TEXT_ALIGN_RIGHT:
            x_start = 128 - (len * 6);
            break;
        case SH1106_TEXT_ALIGN_CENTER:
            x_start = 64 - ((len * 6)/2);
            break;
        case SH1106_TEXT_ALIGN_LEFT:
            x_start = 2;
            break;
        case SH1106_TEXT_ALIGN_USER:
        default:
            break;
        }
    }

    _spi_write_command(&page_cmd, 1);
    _sh1106_set_start_column(x_start + 2);

    while ('\0' != *pstr) {
        _sh1106_display_putc(*pstr++, invert);
    }

done:
    return;
}

ICACHE_FLASH_ATTR
int sh1106_display_init(uint8_t contrast)
{
    int status = 0;
    uint32_t cmd_enable_disp = SH1106_CMD_DC_DC_CONTROL_MODE | (SH1106_CMD_SET_DC_DC_ON(true) << 8),
             cmd_display_on = SH1106_CMD_DISPLAY_ON(true),
             cmd_invert = SH1106_CMD_INVERT_DISPLAY(true),
             cmd_remap_disp = SH1106_CMD_SET_SEG_REMAP(true);

    /* Release the display from reset */
    gpio_output_set(1 << SH1106_SPI_RSTN, 0, 0, 0);

    /* TODO: after releasing reset, do we need to wait? */

    /* Enable the DC-DC converter */
    _spi_write_command(&cmd_enable_disp, 2);

    /* Flip the display direction */
    //_spi_write_command(&cmd_remap_disp, 1);

    sh1106_display_clear();

    /* Turn the display on */
    _spi_write_command(&cmd_display_on, 1);

    /* Write out some display data */
    //_sh1106_checkerboard_test();

    /* We are now ready to accept commands... maybe */

    return status;
}

