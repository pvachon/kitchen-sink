#pragma once

/**
 * Set the LSBs of the start column address
 */
#define SH1106_CMD_SET_LOW_COL_ADDR(_addr)  (0x00 | ((_addr) & 0xf))

/**
 * Set the MSBs of the start column address
 */
#define SH1106_CMD_SET_HIGH_COL_ADDR(_addr) (0x10 | (((_addr) >> 4) & 0xf))

/**
 * Set the start line for the display
 */
#define SH1106_CMD_SET_START_LINE(_line)    (0x40 | ((_line) & 0x3f))

/**
 * Set segment remapping (0-63 -> 63-0) for OLED
 */
#define SH1106_CMD_SET_SEG_REMAP(_remapped) (0xa0 | (!!(_remapped)))

/**
 * Set the display on/off. If _is_on == false, then the display is disabled.
 */
#define SH1106_CMD_FORCE_DISPLAY_ON(_is_on) (0xa4 | !(_is_on))

/**
 * Invert the display
 */
#define SH1106_CMD_INVERT_DISPLAY(_inv)     (0xa6 | !!(_inv))

/**
 * Following command byte is destined for the DC-DC controller
 */
#define SH1106_CMD_DC_DC_CONTROL_MODE       0xad

/**
 * Control the on-board DC-DC. Sent after a DC-DC control mode set byte.
 */
#define SH1106_CMD_SET_DC_DC_ON(_is_on)     (0x8a | !!(_is_on))

/**
 * Turn on the display nicely, or turn it off into sleep mode.
 */
#define SH1106_CMD_DISPLAY_ON(_is_on)       (0xae | !!(_is_on))

/**
 * Set the specified page address as the target for the memory write
 */
#define SH1106_CMD_SET_PAGE_ADDR(_n)        (0xb0 | ((_n) & 0x7))

/**
 * Enable contrast mode (2 byte command). Second byte is the value to set
 */
#define SH1106_CMD_SET_CONTRAST_MODE        0x81

/**
 * Default contrast level, at POR
 */
#define SH1106_CMD_DEFAULT_CONTRAST         0x80
