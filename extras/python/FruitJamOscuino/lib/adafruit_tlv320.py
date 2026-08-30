# SPDX-FileCopyrightText: 2025 Liz Clark for Adafruit Industries
# SPDX-FileCopyrightText: 2025 Sam Blenny
#
# SPDX-License-Identifier: MIT
"""
`adafruit_tlv320`
================================================================================

CircuitPython driver for the TLV320DAC3100 I2S DAC


* Author(s): Liz Clark, Sam Blenny

Implementation Notes
--------------------

**Hardware:**

* `Adafruit TLV320DAC3100 - I2S DAC <https://www.adafruit.com/product/6309>`_

* `Adafruit Fruit Jam <https://www.adafruit.com/product/6200>`_

* The TLV320DAC chip has moderately complex onboard audio filtering, routing,
  and amplification capability. Each of the signal chains for speaker, headphone
  left, and headphone right start with the DAC, then they go through a mixer
  stage, an analog volume (attenuation) stage, and finally an analog amplifier
  stage. Parameters for each stage of each signal chain can be separately set
  with different properties. But, you can ignore most of that if you use
  ``speaker_output = True`` or ``headphone_output = True`` to load defaults.

* To understand how the different audio stages (DAC, volume, amplifier gain)
  relate to each other, it can help to look at the Functional Block Diagram in
  the TLV320DAC3100 datasheet:
  https://learn.adafruit.com/adafruit-tlv320dac3100-i2s-dac/downloads

* **CAUTION**: The TLV320 amplifiers have enough power to easily burn out
  small 1W speakers or drive headphones to levels that could damage your
  hearing. To be safe, start with low volume and gain levels, then increase
  them carefully to find a comfortable listening level. This is why the
  default levels set by speaker_output and headphone_output are relatively low.

**Software and Dependencies:**

* Adafruit CircuitPython firmware for the supported boards:
  https://circuitpython.org/downloads

* Adafruit's Bus Device library: https://github.com/adafruit/Adafruit_CircuitPython_BusDevice

Usage Examples
--------------

Fruit Jam Mini-Speaker
^^^^^^^^^^^^^^^^^^^^^^

This will start you off with a relatively low volume for the Fruit Jam's
bundled 8-Ohm 1 Watt speaker. Your code can adjust the volume by increasing
or decreasing ``dac_volume``. To use a higher wattage speaker that needs
more power, you might want to increase ``speaker_volume``.

::

    dac = TLV320DAC3100(board.I2C())
    dac.speaker_output = True            # set defaults for speaker
    dac.dac_volume = dac.dac_volume + 1  # increase volume by 1 dB
    dac.dac_volume = dac.dac_volume - 1  # decrease volume by 1 dB

Low Impedance Earbuds
^^^^^^^^^^^^^^^^^^^^^

This will start you off with a relatively low volume for low impedance
earbuds (e.g. JVC Gumy) plugged into the Fruit Jam's headphone jack. Your
code can adjust the volume by increasing or decreasing ``dac_volume``. To
use high impedance headphones that need more power, you might want to
increase ``headphone_volume``.

::

    dac = TLV320DAC3100(board.I2C())
    dac.speaker_output = False           # make sure speaker amp is off
    dac.headphone_output = True          # set defaults for headphones
    dac.dac_volume = dac.dac_volume + 1  # increase volume by 1 dB
    dac.dac_volume = dac.dac_volume - 1  # decrease volume by 1 dB

Line Level Output to Mixer
^^^^^^^^^^^^^^^^^^^^^^^^^^

For this one, the default headphone output volume will be way too low for
use with a device that expects consumer line level input (-10 dBV). To fix
that, you can increase ``dac_volume`` or ``headphone_volume``. If you want
to experiment with different ways of setting the levels, check out the
volume test example: `Volume test <./examples.html#volume-test>`_

::

    dac = TLV320DAC3100(board.I2C())
    dac.speaker_output = False    # make sure speaker amp is off
    dac.headphone_output = True   # set defaults for headphones (note: too low!)

    # Make it louder by increasing headphone_volume. We could also use
    # dac_volume, but doing it this way gives a better balance between
    # the speaker signal chain and the headphone jack signal chain. (think
    # of headphone_volume as a mixer channel's pad switch or gain trim knob
    # and dac_volume as the main volume control fader)
    #
    # CAUTION: This will be *way* too loud for earbuds, please be careful!
    dac.headphone_volume = -15.5  # default is -30.1 dB

15 MHz PWM Clock to I2S_MCLK for Better Audio Quality
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

You can get better audio quality (less hiss and distortion) by sending a 15 MHz
clock to the I2S_MCLK pin with pwmio.PWMOut. For example, this lets you get
very good audio quality (at limited bandwidth) using 8 kHz WAV file samples.

::

    sample_rate = 8000  # can also use 11025, 22050, 44100, or 48000

    # 1. Begin sending the MCLK PWM clock signal
    mclk_out = pwmio.PWMOut(board.I2S_MCLK, frequency=15_000_000, duty_cycle=2**15)

    # 2. Initialize DAC (this includes a soft reset and sets minimum volume)
    dac = TLV320DAC3100(board.I2C())

    # 3. Configure headphone/speaker routing and volumes (order matters here)
    dac.speaker_output = False
    dac.headphone_output = True
    dac.dac_volume = -3  # Keep this below 0 to avoid DSP filter clipping
    dac.headphone_volume = 0  # CAUTION! Line level. Too loud for headphones!

    # 4. Configure the right PLL and CODEC settings for our sample rate
    dac.configure_clocks(sample_rate=sample_rate, mclk_freq=15_000_000)

    # 5. Wait for power-on volume ramp-up to finish
    time.sleep(0.35)

    # After this, you can do audio.play(whatever) to play samples, synths, etc.
    audio = audiobusio.I2SOut(bit_clock=board.I2S_BCLK, word_select=board.I2S_WS,
        data=board.I2S_DIN)

API
---
"""

import time

from adafruit_bus_device.i2c_device import I2CDevice
from micropython import const

try:
    from typing import Any, Dict, List, Literal, Optional, Tuple, TypedDict, Union, cast

    from busio import I2C
except ImportError:
    pass

__version__ = "0.0.0+auto.0"
__repo__ = "https://github.com/adafruit/Adafruit_CircuitPython_TLV320.git"

# Register addresses
_PAGE_SELECT = const(0x00)
_RESET = const(0x01)
_OT_FLAG = const(0x03)
_CLOCK_MUX1 = const(0x04)
_PLL_PROG_PR = const(0x05)
_PLL_PROG_J = const(0x06)
_PLL_PROG_D_MSB = const(0x07)
_PLL_PROG_D_LSB = const(0x08)
_NDAC = const(0x0B)
_MDAC = const(0x0C)
_DOSR_MSB = const(0x0D)
_DOSR_LSB = const(0x0E)
_CODEC_IF_CTRL1 = const(0x1B)
_DAC_FLAG = const(0x25)
_DAC_FLAG2 = const(0x26)
_INT1_CTRL = const(0x30)
_INT2_CTRL = const(0x31)
_GPIO1_CTRL = const(0x33)
_DIN_CTRL = const(0x36)
_DAC_DATAPATH = const(0x3F)
_DAC_VOL_CTRL = const(0x40)
_DAC_LVOL = const(0x41)
_DAC_RVOL = const(0x42)
_HEADSET_DETECT = const(0x43)
_VOL_ADC_CTRL = const(0x74)  # VOL/MICDET-Pin SAR ADC Control ister
_VOL_ADC_READ = const(0x75)  # VOL/MICDET-Pin Gain Register

# Page 1 registers
_HP_SPK_ERR_CTL = const(0x1E)
_HP_DRIVERS = const(0x1F)
_SPK_AMP = const(0x20)
_HP_POP = const(0x21)
_PGA_RAMP = const(0x22)
_OUT_ROUTING = const(0x23)
_HPL_VOL = const(0x24)
_HPR_VOL = const(0x25)
_SPK_VOL = const(0x26)
_HPL_DRIVER = const(0x28)
_HPR_DRIVER = const(0x29)
_SPK_DRIVER = const(0x2A)
_HP_DRIVER_CTRL = const(0x2C)
_MICBIAS = const(0x2E)  # MICBIAS Configuration ister
_INPUT_CM = const(0x32)  # Input Common Mode Settings Register

# Page 3 registers
_TIMER_MCLK_DIV = const(0x10)  # Timer Clock MCLK Divider Register

# Default I2C address
I2C_ADDR_DEFAULT = const(0x18)

# Data format for I2S interface
FORMAT_I2S = const(0b00)  # I2S format
FORMAT_DSP = const(0b01)  # DSP format
FORMAT_RJF = const(0b10)  # Right justified format
FORMAT_LJF = const(0b11)  # Left justified format

# Data length for I2S interface
DATA_LEN_16 = const(0b00)  # 16 bits
DATA_LEN_20 = const(0b01)  # 20 bits
DATA_LEN_24 = const(0b10)  # 24 bits
DATA_LEN_32 = const(0b11)  # 32 bits

# GPIO1 pin mode options
#: GPIO1 pin mode options: GPIO1 disabled (input and output buffers powered down)
GPIO1_DISABLED = const(0b0000)
#: GPIO1 pin mode options: Input mode (secondary BCLK/WCLK/DIN input or ClockGen)
GPIO1_INPUT_MODE = const(0b0001)
#: GPIO1 pin mode options: General-purpose input
GPIO1_GPI = const(0b0010)
#: GPIO1 pin mode options: General-purpose output
GPIO1_GPO = const(0b0011)
#: GPIO1 pin mode options: CLKOUT output
GPIO1_CLKOUT = const(0b0100)
#: GPIO1 pin mode options: INT1 output
GPIO1_INT1 = const(0b0101)
#: GPIO1 pin mode options: INT2 output
GPIO1_INT2 = const(0b0110)
#: GPIO1 pin mode options: Secondary BCLK output for codec interface
GPIO1_BCLK_OUT = const(0b1000)
#: GPIO1 pin mode options: Secondary WCLK output for codec interface
GPIO1_WCLK_OUT = const(0b1001)

# DAC channel data path options
#: DAC channel data path option: DAC data path off
DAC_PATH_OFF = const(0b00)
#: DAC channel data path option: Normal path (L->L or R->R)
DAC_PATH_NORMAL = const(0b01)
#: DAC channel data path option: Swapped path (R->L or L->R)
DAC_PATH_SWAPPED = const(0b10)
#: DAC channel data path option: Mixed L+R path
DAC_PATH_MIXED = const(0b11)

# DAC volume control soft stepping options
#: DAC volume control soft stepping option: One step per sample
VOLUME_STEP_1SAMPLE = const(0b00)
#: DAC volume control soft stepping option: One step per two samples
VOLUME_STEP_2SAMPLE = const(0b01)
#: DAC volume control soft stepping option: Soft stepping disabled
VOLUME_STEP_DISABLED = const(0b10)

# DAC volume control configuration options
#: DAC volume control configuration option: Left and right channels independent
VOL_INDEPENDENT = const(0b00)
#: DAC volume control configuration option: Left follows right volume
VOL_LEFT_TO_RIGHT = const(0b01)
#: DAC volume control configuration option: Right follows left volume
VOL_RIGHT_TO_LEFT = const(0b10)

# DAC output routing options
#: DAC output routing option: DAC not routed
DAC_ROUTE_NONE = const(0b00)
#: DAC output routing option: DAC routed to mixer amplifier
DAC_ROUTE_MIXER = const(0b01)
#: DAC output routing option: DAC routed directly to HP driver
DAC_ROUTE_HP = const(0b10)

# Headphone common mode voltage options
#: Headphone common mode voltage option: Common-mode voltage 1.35V
HP_COMMON_1_35V = const(0b00)
#: Headphone common mode voltage option: Common-mode voltage 1.50V
HP_COMMON_1_50V = const(0b01)
#: Headphone common mode voltage option: Common-mode voltage 1.65V
HP_COMMON_1_65V = const(0b10)
#: Headphone common mode voltage option: Common-mode voltage 1.80V
HP_COMMON_1_80V = const(0b11)

# Headset detection debounce time options
#: Headset detection debounce time option: 16ms debounce (2ms clock)
DEBOUNCE_16MS = const(0b000)
#: Headset detection debounce time option: 32ms debounce (4ms clock)
DEBOUNCE_32MS = const(0b001)
#: Headset detection debounce time option: 64ms debounce (8ms clock)
DEBOUNCE_64MS = const(0b010)
#: Headset detection debounce time option: 128ms debounce (16ms clock)
DEBOUNCE_128MS = const(0b011)
#: Headset detection debounce time option: 256ms debounce (32ms clock)
DEBOUNCE_256MS = const(0b100)
#: Headset detection debounce time option: 512ms debounce (64ms clock)
DEBOUNCE_512MS = const(0b101)

# Button press debounce time options
#: Button press debounce time option: No debounce
BTN_DEBOUNCE_0MS = const(0b00)
#: Button press debounce time option: 8ms debounce (1ms clock)
BTN_DEBOUNCE_8MS = const(0b01)
#: Button press debounce time option: 16ms debounce (2ms clock)
BTN_DEBOUNCE_16MS = const(0b10)
#: Button press debounce time option: 32ms debounce (4ms clock)
BTN_DEBOUNCE_32MS = const(0b11)

# ruff: noqa: PLR0904, PLR0912, PLR0913, PLR0915, PLR0917

# Lookup table for speaker_volume and headphone_volume.
# These are from TLV320DAC3100 datasheet Table 6-24.
TABLE_6_24 = (
    0,  #       0  Begin linear segment: round((-1.99 * dB) - 0.2)
    -0.5,  #    1
    -1,  #      2
    -1.5,  #    3
    -2,  #      4
    -2.5,  #    5
    -3,  #      6
    -3.5,  #    7
    -4,  #      8
    -4.5,  #    9
    -5,  #     10
    -5.5,  #   11
    -6,  #     12
    -6.5,  #   13
    -7,  #     14
    -7.5,  #   15
    -8,  #     16
    -8.5,  #   17
    -9,  #     18
    -9.5,  #   19
    -10,  #    20
    -10.5,  #  21
    -11,  #    22
    -11.5,  #  23
    -12,  #    24
    -12.5,  #  25
    -13,  #    26
    -13.5,  #  27
    -14,  #    28
    -14.5,  #  29
    -15,  #    30
    -15.5,  #  31
    -16,  #    32
    -16.5,  #  33
    -17,  #    34
    -17.5,  #  35
    -18.1,  #  36
    -18.6,  #  37
    -19.1,  #  38
    -19.6,  #  39
    -20.1,  #  40
    -20.6,  #  41
    -21.1,  #  42
    -21.6,  #  43
    -22.1,  #  44
    -22.6,  #  45
    -23.1,  #  46
    -23.6,  #  47
    -24.1,  #  48
    -24.6,  #  49
    -25.1,  #  50
    -25.6,  #  51
    -26.1,  #  52
    -26.6,  #  53
    -27.1,  #  54
    -27.6,  #  55
    -28.1,  #  56
    -28.6,  #  57
    -29.1,  #  58
    -29.6,  #  59
    -30.1,  #  60
    -30.6,  #  61
    -31.1,  #  62
    -31.6,  #  63
    -32.1,  #  64
    -32.6,  #  65
    -33.1,  #  66
    -33.6,  #  67
    -34.1,  #  68
    -34.6,  #  69
    -35.2,  #  70
    -35.7,  #  71
    -36.2,  #  72
    -36.7,  #  73
    -37.2,  #  74
    -37.7,  #  75
    -38.2,  #  76
    -38.7,  #  77
    -39.2,  #  78
    -39.7,  #  79
    -40.2,  #  80
    -40.7,  #  81
    -41.2,  #  82
    -41.7,  #  83
    -42.1,  #  84
    -42.7,  #  85
    -43.2,  #  86
    -43.8,  #  87
    -44.3,  #  88
    -44.8,  #  89
    -45.2,  #  90
    -45.8,  #  91
    -46.2,  #  92
    -46.7,  #  93
    -47.4,  #  94
    -47.9,  #  95
    -48.2,  #  96
    -48.7,  #  97
    -49.3,  #  98
    -50,  #    99
    -50.3,  # 100
    -51,  #   101
    -51.4,  # 102
    -51.8,  # 103
    -52.2,  # 104
    -52.7,  # 105  End linear segment: round((-1.99 * dB) - 0.2)
    -53.7,  # 106  Begin curved segment
    -54.2,  # 107
    -55.3,  # 108
    -56.7,  # 109
    -58.3,  # 110
    -60.2,  # 111
    -62.7,  # 112
    -64.3,  # 113
    -66.2,  # 114
    -68.7,  # 115
    -72.2,  # 116  End curved segment
    -78.3,  # 117  Begin constant segment -78.3 dB
    -78.3,  # 118
    -78.3,  # 119
    -78.3,  # 120
    -78.3,  # 121
    -78.3,  # 122
    -78.3,  # 123
    -78.3,  # 124
    -78.3,  # 125
    -78.3,  # 126
    -78.3,  # 127
)


def _table_6_24_db_to_uint7(db: float) -> int:
    """Convert gain dB to 7-bit unsigned int following datasheet Table 6-24.

    :param db: Analog gain in dB; range is 0 dB (loud) to -78.3 dB (soft)
    :return: 7-bit unsigned int value, range is 0 (loud) to 127 (soft)
    """
    # Clip dB argument to fit in the valid range if it's too big or too small
    db = max(-78.3, min(0, db))
    # Loop through the table, looking for the lowest table index where the
    # target dB value is not greater than the table dB value
    result = 0
    for table_u7, table_db in enumerate(TABLE_6_24):
        if db < table_db:
            result = table_u7
        elif db == table_db:
            result = table_u7
            break
        else:
            break
    return result


def _table_6_24_uint7_to_db(u7: int) -> float:
    """Convert 7-bit unsigned int to gain dB following datasheet Table 6-24.

    :param u7: 7-bit unsigned int value, range is 0 (loud) to 127 (soft)
    :return: Analog gain in dB, range is 0 dB (loud) to -78.3 dB (soft)
    """
    return TABLE_6_24[max(0, min(127, int(u7)))]


class _PagedRegisterBase:
    """Base class for paged register access."""

    def __init__(self, i2c_device, page):
        """Initialize the paged register base.

        :param i2c_device: The I2C device
        :param page: The register page number
        """
        self._device = i2c_device
        self._page = page
        self._buffer = bytearray(2)

    def _write_register(self, register, value):
        """Write a value to a register.

        :param register: The register address
        :param value: The value to write
        """
        self._set_page()
        self._buffer[0] = register
        self._buffer[1] = value
        with self._device as i2c:
            i2c.write(self._buffer)

    def _read_register(self, register):
        """Value from a register.

        :param register: The register address
        :return: The register value
        """
        self._set_page()
        self._buffer[0] = register
        with self._device as i2c:
            i2c.write(self._buffer, end=1)
            i2c.readinto(self._buffer, start=0, end=1)
        return self._buffer[0]

    def _set_page(self):
        """The current register page."""
        self._buffer[0] = _PAGE_SELECT
        self._buffer[1] = self._page
        with self._device as i2c:
            i2c.write(self._buffer)

    def _get_bits(self, register, mask, shift):
        """Specific bits from a register.

        :param register: The register address
        :param mask: The bit mask (after shifting)
        :param shift: The bit position (0 = LSB)
        :return: The extracted bits
        """
        value = self._read_register(register)
        return (value >> shift) & mask

    def _set_bits(self, register, mask, shift, value):
        """Specific bits in a register.

        :param register: The register address
        :param mask: The bit mask (after shifting)
        :param shift: The bit position (0 = LSB)
        :param value: The value to set
        """
        reg_value = self._read_register(register)
        reg_value &= ~(mask << shift)
        reg_value |= (value & mask) << shift
        self._write_register(register, reg_value)


class _Page0Registers(_PagedRegisterBase):
    """Page 0 registers containing system configuration, clocking, etc."""

    def __init__(self, i2c_device):
        """Initialize Page 0 registers.

        :param i2c_device: The I2C device
        """
        super().__init__(i2c_device, 0)

    def _reset(self):
        """Perform a software _reset of the chip.

        :return: True if successful, False otherwise
        """
        self._write_register(_RESET, 1)
        time.sleep(0.01)
        return self._read_register(_RESET) == 0

    def _is_overtemperature(self):
        """Check if the chip is in an over-temperature condition.

        :return: True if overtemp condition exists, False if temperature is OK
        """
        return not ((self._read_register(_OT_FLAG) >> 1) & 0x01)

    def _set_int1_source(
        self, headset_detect, button_press, dac_drc, agc_noise, over_current, multiple_pulse
    ):
        """Configure the INT1 interrupt sources."""
        value = 0
        if headset_detect:
            value |= 1 << 7
        if button_press:
            value |= 1 << 6
        if dac_drc:
            value |= 1 << 5
        if over_current:
            value |= 1 << 3
        if agc_noise:
            value |= 1 << 2
        if multiple_pulse:
            value |= 1 << 0
        self._write_register(_INT1_CTRL, value)

    def _set_gpio1_mode(self, mode):
        """The GPIO1 pin mode."""
        return self._set_bits(_GPIO1_CTRL, 0x0F, 2, mode)

    def _set_headset_detect(self, enable, detect_debounce=0, button_debounce=0):
        """Configure headset detection settings."""
        value = (1 if enable else 0) << 7
        value |= (detect_debounce & 0x07) << 2
        value |= button_debounce & 0x03
        self._write_register(_HEADSET_DETECT, value)

    def _set_dac_data_path(
        self,
        left_dac_on,
        right_dac_on,
        left_path=DAC_PATH_NORMAL,
        right_path=DAC_PATH_NORMAL,
        volume_step=VOLUME_STEP_1SAMPLE,
    ):
        """Configure the DAC data path settings."""
        value = 0
        if left_dac_on:
            value |= 1 << 7
        if right_dac_on:
            value |= 1 << 6
        value |= (left_path & 0x03) << 4
        value |= (right_path & 0x03) << 2
        value |= volume_step & 0x03
        self._write_register(_DAC_DATAPATH, value)

    def _set_dac_volume_control(self, left_mute, right_mute, control=VOL_INDEPENDENT):
        """Configure the DAC volume control settings."""
        value = 0
        if left_mute:
            value |= 1 << 3
        if right_mute:
            value |= 1 << 2
        value |= control & 0x03
        self._write_register(_DAC_VOL_CTRL, value)

    def _set_channel_volume(self, right_channel, db):
        """DAC channel volume in dB."""
        db = min(db, 24.0)
        db = max(db, -63.5)
        reg_val = int(db * 2)
        if reg_val == 0x80 or reg_val > 0x30:
            raise ValueError

        if right_channel:
            self._write_register(_DAC_RVOL, reg_val & 0xFF)
        else:
            self._write_register(_DAC_LVOL, reg_val & 0xFF)

    def _get_dac_flags(self):
        """The DAC and output driver status flags.

        :return: Dictionary with status flags for various components
        """
        flag_reg = self._read_register(_DAC_FLAG)
        left_dac_powered = bool(flag_reg & (1 << 7))
        hpl_powered = bool(flag_reg & (1 << 5))
        left_classd_powered = bool(flag_reg & (1 << 4))
        right_dac_powered = bool(flag_reg & (1 << 3))
        hpr_powered = bool(flag_reg & (1 << 1))
        right_classd_powered = bool(flag_reg & (1 << 0))
        flag2_reg = self._read_register(_DAC_FLAG2)
        left_pga_gain_ok = bool(flag2_reg & (1 << 4))
        right_pga_gain_ok = bool(flag2_reg & (1 << 0))

        return {
            "left_dac_powered": left_dac_powered,
            "hpl_powered": hpl_powered,
            "left_classd_powered": left_classd_powered,
            "right_dac_powered": right_dac_powered,
            "hpr_powered": hpr_powered,
            "right_classd_powered": right_classd_powered,
            "left_pga_gain_ok": left_pga_gain_ok,
            "right_pga_gain_ok": right_pga_gain_ok,
        }

    def get_gpio1_input(self):
        """The current GPIO1 input value.

        :return: Current GPIO1 input state (True/False)
        """
        return bool(self._get_bits(_GPIO1_CTRL, 0x01, 1))

    def _get_din_input(self):
        """The current DIN input value.

        :return: Current DIN input state (True/False)
        """
        return bool(self._get_bits(_DIN_CTRL, 0x01, 0))

    def _get_codec_interface(self):
        """The current codec interface settings.

        :return: Dictionary with format, data_len, bclk_out, and wclk_out values
        """
        reg_value = self._read_register(_CODEC_IF_CTRL1)
        format_val = (reg_value >> 6) & 0x03
        data_len = (reg_value >> 4) & 0x03
        bclk_out = bool(reg_value & (1 << 3))
        wclk_out = bool(reg_value & (1 << 2))

        return {
            "format": format_val,
            "data_len": data_len,
            "bclk_out": bclk_out,
            "wclk_out": wclk_out,
        }

    def _get_dac_data_path(self) -> dict:
        """The current DAC data path configuration.

        :return: Dictionary with DAC data path settings
        """
        reg_value = self._read_register(_DAC_DATAPATH)
        left_dac_on = bool(reg_value & (1 << 7))
        right_dac_on = bool(reg_value & (1 << 6))
        left_path = (reg_value >> 4) & 0x03
        right_path = (reg_value >> 2) & 0x03
        volume_step = reg_value & 0x03

        return {
            "left_dac_on": left_dac_on,
            "right_dac_on": right_dac_on,
            "left_path": left_path,
            "right_path": right_path,
            "volume_step": volume_step,
        }

    def _get_dac_volume_control(self) -> dict:
        """The current DAC volume control configuration.

        :return: Dictionary with volume control settings
        """
        reg_value = self._read_register(_DAC_VOL_CTRL)
        left_mute = bool(reg_value & (1 << 3))
        right_mute = bool(reg_value & (1 << 2))
        control = reg_value & 0x03
        return {"left_mute": left_mute, "right_mute": right_mute, "control": control}

    def _get_channel_volume(self, right_channel):
        """DAC channel volume in dB.

        :param right_channel: True for right channel, False for left channel
        :return: Current volume in dB
        """
        reg = _DAC_RVOL if right_channel else _DAC_LVOL
        reg_val = self._read_register(reg)
        if reg_val & 0x80:
            steps = reg_val - 256
        else:
            steps = reg_val
        return steps * 0.5

    def _get_headset_status(self):
        """Current headset detection status.

        :return: Integer value representing headset status (0=none, 1=without mic, 3=with mic)
        """
        status_bits = self._get_bits(_HEADSET_DETECT, 0x03, 5)
        return status_bits

    def _config_vol_adc(self, pin_control=False, use_mclk=False, hysteresis=0, rate=0):
        """The Volume/MicDet pin ADC.

        :param pin_control: Enable pin control of DAC volume
        :param use_mclk: Use MCLK instead of internal RC oscillator
        :param hysteresis: ADC hysteresis setting (0-2)
        :param rate: ADC sampling rate (0-7)
        """
        value = (1 if pin_control else 0) << 7
        value |= (1 if use_mclk else 0) << 6
        value |= (hysteresis & 0x03) << 4
        value |= rate & 0x07
        self._write_register(_VOL_ADC_CTRL, value)

    def _read_vol_adc_db(self):
        """The current volume from the Volume ADC in dB.

        :return: Current volume in dB (+18 to -63 dB)
        """
        raw_val = self._read_register(_VOL_ADC_READ) & 0x7F
        if raw_val == 0x7F:
            return 0.0
        if raw_val <= 0x24:
            return 18.0 - (raw_val * 0.5)
        else:
            return -((raw_val - 0x24) * 0.5)

    def _set_int2_source(
        self,
        headset_detect=False,
        button_press=False,
        dac_drc=False,
        agc_noise=False,
        over_current=False,
        multiple_pulse=False,
    ):
        """Configure the INT2 interrupt sources.

        :param headset_detect: Enable headset detection interrupt
        :param button_press: Enable button press detection interrupt
        :param dac_drc: Enable DAC DRC signal power interrupt
        :param agc_noise: Enable DAC data overflow interrupt
        :param over_current: Enable short circuit interrupt
        :param multiple_pulse: If true, INT2 generates multiple pulses until flag read
        """
        value = 0
        if headset_detect:
            value |= 1 << 7
        if button_press:
            value |= 1 << 6
        if dac_drc:
            value |= 1 << 5
        if over_current:
            value |= 1 << 3
        if agc_noise:
            value |= 1 << 2
        if multiple_pulse:
            value |= 1 << 0

        self._write_register(_INT2_CTRL, value)

    def _set_codec_interface(self, format, data_len, bclk_out=False, wclk_out=False):
        """The codec interface parameters."""
        value = (format & 0x03) << 6
        value |= (data_len & 0x03) << 4
        value |= (1 if bclk_out else 0) << 3
        value |= (1 if wclk_out else 0) << 2

        self._write_register(_CODEC_IF_CTRL1, value)

    def _configure_clocks_for_sample_rate(self, mclk_freq: int, sample_rate: int, bit_depth: int):
        # For sphinx docs, see configure_clocks() which wraps this function.

        # CircuitPython always uses 16-bit stereo for I2S
        if bit_depth == 16:
            data_len = DATA_LEN_16
        else:
            raise ValueError("CircuitPython I2S only supports 16-bit stereo")

        # The P, R, J, D, NDAC, and MDAC constants below come from running a
        # brute force solver to satisfy the constraints in the datasheet while
        # exactly converting the PLL input clock to the necessary clock for
        # oversampling (sample rate * DOSR). By configuring the PLL properly,
        # it's possible to _dramatically_ reduce the noise floor and harmonic
        # distortion of the DAC output. When the PLL doesn't lock due to messy
        # input clock signal or unsuitable tuning parameters, you get broadband
        # hiss (extends above Nyquist frequency) and significant harmonic
        # distortion.
        #
        # DOSR controls the oversampling rate. Slower clock rates need higher
        # oversampling to shift the delta-sigma modulator quantization noise up
        # out of the audible frequency range. See datasheet section 6.3.8.

        if mclk_freq == 0:
            # Use BCLK as the PLL input (PLL_CLKIN) and multiply it up to the
            # necessary DAC_MOD_CLK for oversampling. Bit clock (BCLK) is
            # sample rate * 32 because CircuitPython I2S uses 16-bit stereo.
            #
            # Constraint formulas for PLL output and dividers (when D = 0):
            # 1. Oversample clock: DAC_MOD_CLK = CODEC_CLKIN / (NDAC * MDAC)
            # 2. Sample rate clock: DAC_fS = CODEC_CLKIN / (NDAC * MDAC * DOSR)
            # 3. 512 kHz <= (PLL_CLKIN / P) <= 20 MHz
            # 4. 80 MHz <= (PLL_CLKIN * J.D * R / P) <= 110 MHz
            #
            # Because of the 512 kHz minimum PLL_CLKIN constraint, BCLK as
            # PLL_CLKIN will never work for 8000 or 11025 kHz (256 kHz and
            # 352.8 kHz bit clocks), no matter how clean the BCLK timing is.
            #
            # CAUTION: Although these PLL tunings satisfy the datasheet
            # constraints, in practice on Fruit Jam, the PLL never locks for
            # these. So, you get hiss and significant harmonic distortion. It's
            # not too bad at 44100 and 48000, but 22050 and below sound very
            # distorted. The problem is that the BCLK timing is jittery and a
            # bit off frequency. Using MCLK with stable PWM sounds way better.
            #
            clock_mux1_source = 0b01  # BCLK
            if sample_rate == 22050:
                p, r, j, d, ndac, mdac, dosr = 1, 4, 38, 0, 19, 1, 256
            elif sample_rate == 44100:
                p, r, j, d, ndac, mdac, dosr = 1, 2, 38, 0, 19, 1, 128
            elif sample_rate == 48000:
                p, r, j, d, ndac, mdac, dosr = 1, 2, 34, 0, 17, 1, 128
            elif sample_rate in {8000, 11025}:
                # These PLL tuning values don't satisfy the datasheet
                # constraints. They are from the old PLL config before MCLK
                # support was added. The PLL won't lock and it will sound
                # extremely distorted, but we'll do it anyway for backward
                # compatibility with existing Fruit Jam code that uses 8000
                # kHz, etc.
                p, r, j, d, ndac, mdac, dosr = 1, 3, 20, 0, 5, 3, 128
            else:
                raise ValueError(
                    "Need a valid BCLK sample rate: 8000, 11025, 22050, 44100, or 48000"
                )

        elif mclk_freq == 15_000_000:
            # Use MCLK as the PLL input (PLL_CLK_IN). To make this work, you
            # need to drive I2S_MCLK with a 15 MHz clock (pwmio.PWMOut with 50%
            # duty cycle works fine on RP2350).
            #
            clock_mux1_source = 0b00  # MCLK
            if sample_rate == 8000:
                p, r, j, d, ndac, mdac, dosr = 1, 1, 6, 9632, 17, 1, 768
            elif sample_rate == 11025:
                p, r, j, d, ndac, mdac, dosr = 5, 1, 35, 7504, 19, 1, 512
            elif sample_rate == 22050:
                p, r, j, d, ndac, mdac, dosr = 5, 1, 35, 7504, 19, 1, 256
            elif sample_rate == 44100:
                p, r, j, d, ndac, mdac, dosr = 5, 1, 35, 7504, 19, 1, 128
            elif sample_rate == 48000:
                p, r, j, d, ndac, mdac, dosr = 1, 1, 6, 9632, 17, 1, 128
            else:
                raise ValueError(
                    "Need a valid MCLK sample rate: 8000, 11025, 22050, 44100, or 48000"
                )

        else:
            raise ValueError("Need a valid MCLK frequency: 15MHz or 0 for BCLK")

        # CAUTION: The datasheet specifies sequencing constraints around
        # changing the PLL and CODEC config. Specific ordering matters here.

        # 1. Ensure DAC and PLL are powered down
        self._set_bits(_DAC_DATAPATH, 0x03, 6, 0b00)
        self._set_bits(_PLL_PROG_PR, 0x01, 7, 0b0)
        time.sleep(0.001)

        # 2. Set PLL clock scaling registers
        pr_value = ((p & 0x07) << 4) | (r & 0x0F)
        self._write_register(_PLL_PROG_PR, pr_value & 0x7F)
        self._write_register(_PLL_PROG_J, j & 0x3F)
        self._write_register(_PLL_PROG_D_MSB, (d >> 8) & 0xFF)
        self._write_register(_PLL_PROG_D_LSB, d & 0xFF)

        # 3. Set mux for PLL input clock source (PLL_CLKIN)
        self._set_bits(_CLOCK_MUX1, 0x0C, 2, clock_mux1_source)

        # 4. Power up  PLL and wait briefly for PLL lock
        self._set_bits(_PLL_PROG_PR, 0x01, 7, 0b1)
        time.sleep(0.01)

        # 5. Set mux to route PLL output (PLL_CLK) to CODEC_CLKIN
        self._set_bits(_CLOCK_MUX1, 0x03, 0, 0b11)

        # 6. Set the data format
        self._set_codec_interface(FORMAT_I2S, data_len)

        # 7. Configure codec clock dividers for oversampling and DSP
        self._write_register(_NDAC, 0x80 | (ndac & 0x7F))
        self._write_register(_MDAC, 0x80 | (mdac & 0x7F))
        self._write_register(_DOSR_MSB, (dosr >> 8) & 0xFF)
        self._write_register(_DOSR_LSB, dosr & 0xFF)

        # 8. Power up DAC
        self._set_bits(_DAC_DATAPATH, 0x03, 6, 0b11)


class _Page1Registers(_PagedRegisterBase):
    """Page 1 registers containing analog output settings, HP/SPK controls, etc."""

    def __init__(self, i2c_device):
        """Initialize Page 1 registers.

        :param i2c_device: The I2C device
        """
        super().__init__(i2c_device, 1)

    def _get_speaker_enabled(self):
        """Check if speaker is enabled."""
        return bool(self._get_bits(_SPK_AMP, 0x01, 7))

    def _set_speaker_enabled(self, enable):
        """Enable or disable the Class-D speaker amplifier."""
        return self._set_bits(_SPK_AMP, 0x01, 7, 1 if enable else 0)

    def _configure_headphone_driver(
        self, left_powered, right_powered, common=HP_COMMON_1_35V, power_down_on_scd=False
    ):
        """Headphone driver settings."""
        value = 0x04
        if left_powered:
            value |= 1 << 7
        if right_powered:
            value |= 1 << 6
        value |= (common & 0x03) << 3
        if power_down_on_scd:
            value |= 1 << 1
        self._write_register(_HP_DRIVERS, value)

    def _configure_analog_inputs(
        self,
        left_dac=DAC_ROUTE_NONE,
        right_dac=DAC_ROUTE_NONE,
        left_ain1=False,
        left_ain2=False,
        right_ain2=False,
        hpl_routed_to_hpr=False,
    ):
        """DAC and analog input routing."""
        value = 0
        value |= (left_dac & 0x03) << 6
        if left_ain1:
            value |= 1 << 5
        if left_ain2:
            value |= 1 << 4
        value |= (right_dac & 0x03) << 2
        if right_ain2:
            value |= 1 << 1
        if hpl_routed_to_hpr:
            value |= 1

        self._write_register(_OUT_ROUTING, value)

    def _set_hpl_volume(self, route_enabled, gain=0x7F):
        """HPL analog volume control."""
        gain = min(gain, 0x7F)
        value = ((1 if route_enabled else 0) << 7) | (gain & 0x7F)
        self._write_register(_HPL_VOL, value)

    def _set_hpr_volume(self, route_enabled, gain=0x7F):
        """HPR analog volume control."""
        gain = min(gain, 0x7F)
        value = ((1 if route_enabled else 0) << 7) | (gain & 0x7F)
        self._write_register(_HPR_VOL, value)

    def _set_spk_volume(self, route_enabled, gain=0x7F):
        """Speaker analog volume control."""
        gain = min(gain, 0x7F)
        value = ((1 if route_enabled else 0) << 7) | (gain & 0x7F)
        self._write_register(_SPK_VOL, value)

    def _configure_hpl_pga(self, gain_db=0, unmute=True):
        """HPL driver PGA settings.
        :raises ValueError: If set to anything outside of range 0 to 9
        """
        if not (0 <= gain_db <= 9):
            raise ValueError("HPL gain must be in range 0 to 9")
        value = (gain_db & 0x0F) << 3
        if unmute:
            value |= 1 << 2
        self._write_register(_HPL_DRIVER, value)

    def _configure_hpr_pga(self, gain_db=0, unmute=True):
        """HPR driver PGA settings.
        :raises ValueError: If set to anything outside of range 0 to 9
        """
        if not (0 <= gain_db <= 9):
            raise ValueError("HPR gain must be in range 0 to 9")
        value = (gain_db & 0x0F) << 3
        if unmute:
            value |= 1 << 2
        self._write_register(_HPR_DRIVER, value)

    def _configure_spk_pga(self, gain_db=6, unmute=True):
        """Speaker driver settings.
        :raises ValueError: If set to anything other than 6, 12, 18, or 24
        """
        if gain_db not in set((6, 12, 18, 24)):
            raise ValueError(f"Invalid speaker gain: {gain_db}. Must be 6, 12, 18, or 24.")
        uint2_val = int((gain_db / 6) - 1)
        value = (uint2_val & 0x03) << 3
        if unmute:
            value |= 1 << 2
        self._write_register(_SPK_DRIVER, value)

    def _is_speaker_shorted(self):
        """Check if speaker short circuit is detected.

        :return: True if short circuit detected, False if not
        """
        return bool(self._get_bits(_SPK_AMP, 0x01, 0))

    def _is_hpl_gain_applied(self):
        """Check if all programmed gains have been applied to HPL.

        :return: True if gains applied, False if still ramping
        """
        return bool(self._get_bits(_HPL_DRIVER, 0x01, 0))

    def _is_hpr_gain_applied(self):
        """Check if all programmed gains have been applied to HPR.

        :return: True if gains applied, False if still ramping
        """
        return bool(self._get_bits(_HPR_DRIVER, 0x01, 0))

    def _is_spk_gain_applied(self):
        """Check if all programmed gains have been applied to Speaker.

        :return: True if gains applied, False if still ramping
        """
        return bool(self._get_bits(_SPK_DRIVER, 0x01, 0))

    def _reset_speaker_on_scd(self, reset):
        """Configure speaker reset behavior on short circuit detection.

        :param reset: True to reset speaker on short circuit, False to remain unchanged
        :return: True if successful
        """
        return self._set_bits(_HP_SPK_ERR_CTL, 0x01, 1, 0 if reset else 1)

    def _reset_headphone_on_scd(self, reset):
        """Configure headphone reset behavior on short circuit detection.

        :param reset: True to reset headphone on short circuit, False to remain unchanged
        :return: True if successful
        """
        # Register is inverse of parameter (0 = reset, 1 = no reset)
        return self._set_bits(_HP_SPK_ERR_CTL, 0x01, 0, 0 if reset else 1)

    def _configure_headphone_pop(self, wait_for_powerdown=True, powerup_time=0x07, ramp_time=0x03):
        """Configure headphone pop removal settings.

        :param wait_for_powerdown: Wait for amp powerdown before DAC powerdown
        :param powerup_time: Driver power-on time (0-11)
        :param ramp_time: Driver ramp-up step time (0-3)
        :return: True if successful
        """
        value = (1 if wait_for_powerdown else 0) << 7
        value |= (powerup_time & 0x0F) << 3
        value |= (ramp_time & 0x03) << 1
        self._write_register(_HP_POP, value)

    def _set_speaker_wait_time(self, wait_time=0):
        """Speaker power-up wait time.

        :param wait_time: Speaker power-up wait duration (0-7)
        :return: True if successful
        """
        return self._set_bits(_PGA_RAMP, 0x07, 4, wait_time)

    def _headphone_lineout(self, left, right):
        """Configure headphone outputs as line-out.

        :param left: Configure left channel as line-out
        :param right: Configure right channel as line-out
        :return: True if successful
        """
        value = 0
        if left:
            value |= 1 << 2
        if right:
            value |= 1 << 1
        self._write_register(_HP_DRIVER_CTRL, value)

    def _config_mic_bias(self, power_down=False, always_on=False, voltage=0):
        """Configure MICBIAS settings."""
        value = (1 if power_down else 0) << 7
        value |= (1 if always_on else 0) << 3
        value |= voltage & 0x03
        self._write_register(_MICBIAS, value)

    def _set_input_common_mode(self, ain1_cm, ain2_cm):
        """Analog input common mode connections."""
        value = 0
        if ain1_cm:
            value |= 1 << 7
        if ain2_cm:
            value |= 1 << 6
        self._write_register(_INPUT_CM, value)


class _Page3Registers(_PagedRegisterBase):
    """Page 3 registers containing timer settings."""

    def __init__(self, i2c_device):
        """Page 3 registers.

        :param i2c_device: The I2C device
        """
        super().__init__(i2c_device, 3)

    def _config_delay_divider(self, use_mclk=True, divider=1):
        """Configure programmable delay timer clock source and divider."""
        value = (1 if use_mclk else 0) << 7
        value |= divider & 0x7F
        self._write_register(_TIMER_MCLK_DIV, value)


class TLV320DAC3100:
    """Driver for the TI TLV320DAC3100 Stereo DAC with Headphone Amplifier."""

    def __init__(self, i2c: I2C, address: int = 0x18) -> None:
        """Initialize the TLV320DAC3100.

        :param i2c: The I2C bus the device is connected to
        :param address: The I2C device address (default is 0x18)
        """
        self._device: I2CDevice = I2CDevice(i2c, address)

        # Initialize register page classes
        self._page0: _Page0Registers = _Page0Registers(self._device)
        self._page1: _Page1Registers = _Page1Registers(self._device)
        self._page3: _Page3Registers = _Page3Registers(self._device)
        self._sample_rate: int = 44100
        self._bit_depth: int = 16
        self._mclk_freq: int = 0  # Default to BCLK
        if not self.reset():
            raise RuntimeError("Failed to reset TLV320DAC3100")
        time.sleep(0.01)
        # Start with very low volumes to reduce popping when we set up the
        # clock configuration.
        self._page0._set_channel_volume(False, -63.5)  # Left volume
        self._page0._set_channel_volume(True, -63.5)  # Right volume

        # Both DACs on with normal path by default
        self._page0._set_dac_data_path(
            left_dac_on=True,
            right_dac_on=True,
            left_path=DAC_PATH_NORMAL,
            right_path=DAC_PATH_NORMAL,
        )
        self._page0._set_dac_volume_control(False, False, VOL_INDEPENDENT)

        # Configure the PLL and CODEC, then turn on the DACs
        self._page0._configure_clocks_for_sample_rate(
            self._mclk_freq, self._sample_rate, self.bit_depth
        )

    # Basic properties and methods

    def reset(self) -> bool:
        """Reset the device.

        :return: True if reset successful, False otherwise
        """
        return self._page0._reset()

    @property
    def overtemperature(self) -> bool:
        """Check if the chip is overheating.

        :getter: Return True if overtemperature condition exists, False
            otherwise
        """
        return self._page0._is_overtemperature()

    def set_headset_detect(
        self, enable: bool, detect_debounce: int = 0, button_debounce: int = 0
    ) -> bool:
        """Headset detection settings.

        :param enable: Boolean to enable/disable headset detection
        :param detect_debounce: One of the DEBOUNCE_* constants for headset detect
        :param button_debounce: One of the BTN_DEBOUNCE_* constants for button press
        :raises ValueError: If debounce values are not valid constants
        :return: True if successful, False otherwise
        """
        valid_detect_debounce: List[int] = [
            DEBOUNCE_16MS,
            DEBOUNCE_32MS,
            DEBOUNCE_64MS,
            DEBOUNCE_128MS,
            DEBOUNCE_256MS,
            DEBOUNCE_512MS,
        ]
        valid_button_debounce: List[int] = [
            BTN_DEBOUNCE_0MS,
            BTN_DEBOUNCE_8MS,
            BTN_DEBOUNCE_16MS,
            BTN_DEBOUNCE_32MS,
        ]

        if detect_debounce not in valid_detect_debounce:
            raise ValueError(
                f"Invalid detect_debounce value: {detect_debounce}."
                + "Must be one of the DEBOUNCE_* constants."
            )

        if button_debounce not in valid_button_debounce:
            raise ValueError(
                f"Invalid button_debounce value: {button_debounce}."
                + "Must be one of the BTN_DEBOUNCE_* constants."
            )

        return self._page0._set_headset_detect(enable, detect_debounce, button_debounce)

    def int1_source(
        self,
        headset_detect: bool,
        button_press: bool,
        dac_drc: bool,
        agc_noise: bool,
        over_current: bool,
        multiple_pulse: bool,
    ) -> bool:
        """The INT1 interrupt sources.

        :param headset_detect: Enable headset detection interrupt
        :param button_press: Enable button press detection interrupt
        :param dac_drc: Enable DAC DRC signal power interrupt
        :param agc_noise: Enable DAC data overflow interrupt
        :param over_current: Enable short circuit interrupt
        :param multiple_pulse: If true, INT1 generates multiple pulses until flag read
        :return: True if successful, False otherwise
        """
        return self._page0._set_int1_source(
            headset_detect, button_press, dac_drc, agc_noise, over_current, multiple_pulse
        )

    @property
    def left_dac(self) -> bool:
        """The left DAC enabled status.

        True if left DAC is enabled, False otherwise

        :getter: Return status
        :setter: Set status
        """
        return self._page0._get_dac_data_path()["left_dac_on"]

    @left_dac.setter
    def left_dac(self, enabled: bool) -> None:
        current: dict = self._page0._get_dac_data_path()
        self._page0._set_dac_data_path(
            enabled,
            current["right_dac_on"],
            current["left_path"],
            current["right_path"],
            current["volume_step"],
        )

    @property
    def right_dac(self) -> bool:
        """The right DAC enabled status.

        True if right DAC is enabled, False otherwise.

        :getter: Return status
        :setter: Set status
        """
        return self._page0._get_dac_data_path()["right_dac_on"]

    @right_dac.setter
    def right_dac(self, enabled: bool) -> None:
        current: dict = self._page0._get_dac_data_path()
        self._page0._set_dac_data_path(
            current["left_dac_on"],
            enabled,
            current["left_path"],
            current["right_path"],
            current["volume_step"],
        )

    @property
    def left_dac_path(self) -> int:
        """The left DAC path setting.

        One of the DAC_PATH_* constants

        :getter: Return left DAC path
        :setter: Set left DAC path
        :raises ValueError: If set to something that's not a DAC_PATH_* constant
        """
        return self._page0._get_dac_data_path()["left_path"]

    @left_dac_path.setter
    def left_dac_path(self, path: int) -> None:
        valid_paths: List[int] = [DAC_PATH_OFF, DAC_PATH_NORMAL, DAC_PATH_SWAPPED, DAC_PATH_MIXED]

        if path not in valid_paths:
            raise ValueError(
                f"Invalid DAC path value: {path}. Must be one of the DAC_PATH_* constants."
            )

        current: dict = self._page0._get_dac_data_path()
        self._page0._set_dac_data_path(
            current["left_dac_on"],
            current["right_dac_on"],
            path,
            current["right_path"],
            current["volume_step"],
        )

    @property
    def right_dac_path(self) -> int:
        """The right DAC path setting.

        One of the DAC_PATH_* constants

        :getter: Return right DAC path
        :setter: Set right DAC path
        :raises ValueError: If set to something that's not a DAC_PATH_* constant
        """
        return self._page0._get_dac_data_path()["right_path"]

    @right_dac_path.setter
    def right_dac_path(self, path: int) -> None:
        valid_paths: List[int] = [DAC_PATH_OFF, DAC_PATH_NORMAL, DAC_PATH_SWAPPED, DAC_PATH_MIXED]

        if path not in valid_paths:
            raise ValueError(
                f"Invalid DAC path value: {path}. Must be one of the DAC_PATH_* constants."
            )

        current: dict = self._page0._get_dac_data_path()
        self._page0._set_dac_data_path(
            current["left_dac_on"],
            current["right_dac_on"],
            current["left_path"],
            path,
            current["volume_step"],
        )

    @property
    def dac_volume_step(self) -> int:
        """The DAC volume step setting.

        One of the VOLUME_STEP_* constants.

        :getter: Return current volume step
        :setter: Set volume step
        :raises ValueError: If step is not a valid VOLUME_STEP_* constant
        """
        return self._page0._get_dac_data_path()["volume_step"]

    @dac_volume_step.setter
    def dac_volume_step(self, step: int) -> None:
        valid_steps: List[int] = [VOLUME_STEP_1SAMPLE, VOLUME_STEP_2SAMPLE, VOLUME_STEP_DISABLED]

        if step not in valid_steps:
            raise ValueError(
                f"Invalid volume step value: {step}. Must be one of the VOLUME_STEP_* constants."
            )

        current: dict = self._page0._get_dac_data_path()
        self._page0._set_dac_data_path(
            current["left_dac_on"],
            current["right_dac_on"],
            current["left_path"],
            current["right_path"],
            step,
        )

    def configure_analog_inputs(
        self,
        left_dac: int = 0,
        right_dac: int = 0,
        left_ain1: bool = False,
        left_ain2: bool = False,
        right_ain2: bool = False,
        hpl_routed_to_hpr: bool = False,
    ) -> bool:
        """DAC and analog input routing.

        :param left_dac: One of the DAC_ROUTE_* constants for left DAC routing
        :param right_dac: One of the DAC_ROUTE_* constants for right DAC routing
        :param left_ain1: Boolean to route left AIN1 to output
        :param left_ain2: Boolean to route left AIN2 to output
        :param right_ain2: Boolean to route right AIN2 to output
        :param hpl_routed_to_hpr: Boolean to route HPL to HPR
        :raises ValueError: If DAC route values are not valid constants
        :return: True if successful, False otherwise
        """
        valid_dac_routes: List[int] = [DAC_ROUTE_NONE, DAC_ROUTE_MIXER, DAC_ROUTE_HP]

        if left_dac not in valid_dac_routes:
            raise ValueError(
                f"Invalid left_dac value: {left_dac}. Must be one of the DAC_ROUTE_* constants."
            )

        if right_dac not in valid_dac_routes:
            raise ValueError(
                f"Invalid right_dac value: {right_dac}. Must be one of the DAC_ROUTE_* constants."
            )

        return self._page1._configure_analog_inputs(
            left_dac, right_dac, left_ain1, left_ain2, right_ain2, hpl_routed_to_hpr
        )

    @property
    def left_dac_mute(self) -> bool:
        """The left DAC mute status.

        True if left DAC is muted, False otherwise.

        :getter: Return status
        :setter: Set status
        """
        return self._page0._get_dac_volume_control()["left_mute"]

    @left_dac_mute.setter
    def left_dac_mute(self, mute: bool) -> None:
        current: dict = self._page0._get_dac_volume_control()
        self._page0._set_dac_volume_control(mute, current["right_mute"], current["control"])

    @property
    def right_dac_mute(self) -> bool:
        """The right DAC mute status.

        True if right DAC is muted, False otherwise.

        :getter: Return status
        :setter: Set status
        """
        return self._page0._get_dac_volume_control()["right_mute"]

    @right_dac_mute.setter
    def right_dac_mute(self, mute: bool) -> None:
        current: dict = self._page0._get_dac_volume_control()
        self._page0._set_dac_volume_control(current["left_mute"], mute, current["control"])

    @property
    def dac_volume_control_mode(self) -> int:
        """The DAC volume control mode.

        One of the VOL_* constants.

        :getter: Return mode
        :setter: Set mode
        :raises ValueError: If mode is not a valid VOL_* constant
        """
        return self._page0._get_dac_volume_control()["control"]

    @dac_volume_control_mode.setter
    def dac_volume_control_mode(self, mode: int) -> None:
        valid_modes: List[int] = [VOL_INDEPENDENT, VOL_LEFT_TO_RIGHT, VOL_RIGHT_TO_LEFT]
        if mode not in valid_modes:
            raise ValueError(
                f"Invalid volume control mode: {mode}. Must be one of the VOL_* constants."
            )
        current: dict = self._page0._get_dac_volume_control()
        self._page0._set_dac_volume_control(current["left_mute"], current["right_mute"], mode)

    @property
    def left_dac_channel_volume(self) -> float:
        """Left DAC channel volume in dB.

        :getter: Return volume
        :setter: Set volume
        """
        return self._page0._get_channel_volume(False)

    @left_dac_channel_volume.setter
    def left_dac_channel_volume(self, db: float) -> None:
        self._page0._set_channel_volume(False, db)

    @property
    def right_dac_channel_volume(self) -> float:
        """Right DAC channel volume in dB.

        :getter: Return volume
        :setter: Set volume
        """
        return self._page0._get_channel_volume(True)

    @right_dac_channel_volume.setter
    def right_dac_channel_volume(self, db: float) -> None:
        self._page0._set_channel_volume(True, db)

    @staticmethod
    def _convert_reg_to_db(reg_val: int) -> float:
        """
        Convert a register value to decibel volume.

        :param reg_val: 8-bit register value
        :return: Volume in dB
        """
        if reg_val & 0x80:
            reg_val -= 256

        return reg_val * 0.5

    @staticmethod
    def _convert_db_to_reg(db: float) -> int:
        """
        Convert decibel volume to register value.

        :param db: Volume in dB (-63.5 to 24 dB)
        :return: 8-bit register value
        """
        reg_val = int(db * 2)
        if reg_val > 0x30:
            reg_val = 0x30
        elif reg_val < -0x80:
            reg_val = 0x80

        if reg_val < 0:
            reg_val += 256

        return reg_val & 0xFF

    @property
    def dac_volume(self) -> float:
        """Current DAC digital volume in dB.

        Range is -63.5 dB (soft) to 24 dB (loud).

        This acts on two registers at once. In the datasheet, they are:

        * Page 0 / Register 65 (0x41): DAC Left Volume Control

        * Page 0 / Register 66 (0x42): DAC Right Volume Control

        Changing the DAC volume will change the signal level feeding into the
        analog signal chains of the speaker and both headphone channels. You
        should also be aware of ``speaker_volume``, ``speaker_gain``,
        ``speaker_mute``, ``headphone_volume``, ``headphone_left_gain``,
        ``headphone_right_gain``, ``headphone_left_mute``, and
        ``headphone_right_mute``.

        :getter: Return volume
        :setter: Set volume
        """
        left_vol = self._page0._read_register(_DAC_LVOL)
        right_vol = self._page0._read_register(_DAC_RVOL)

        left_db = self._convert_reg_to_db(left_vol)
        right_db = self._convert_reg_to_db(right_vol)

        return (left_db + right_db) / 2

    @dac_volume.setter
    def dac_volume(self, db: float) -> None:
        db = max(-63.5, min(24, db))
        reg_val = self._convert_db_to_reg(db)
        self._page0._set_page()
        self._page0._write_register(_DAC_LVOL, reg_val)
        self._page0._write_register(_DAC_RVOL, reg_val)

        self.left_dac_mute = False
        self.right_dac_mute = False

    def manual_headphone_driver(
        self,
        left_powered: bool,
        right_powered: bool,
        common: int = 0,
        power_down_on_scd: bool = False,
    ) -> bool:
        """Headphone driver settings.

        :param left_powered: Boolean to power left headphone driver
        :param right_powered: Boolean to power right headphone driver
        :param common: One of the HP_COMMON_* constants for common mode voltage
        :param power_down_on_scd: Boolean to power down on short circuit detection
        :raises ValueError: If common is not a valid HP_COMMON_* constant
        :return: True if successful, False otherwise
        """
        valid_common_modes: List[int] = [
            HP_COMMON_1_35V,
            HP_COMMON_1_50V,
            HP_COMMON_1_65V,
            HP_COMMON_1_80V,
        ]

        if common not in valid_common_modes:
            raise ValueError(
                f"Invalid common mode value: {common}. Must be one of the HP_COMMON_* constants."
            )

        return self._page1._configure_headphone_driver(
            left_powered, right_powered, common, power_down_on_scd
        )

    def manual_headphone_left_volume(self, route_enabled: bool, gain: int = 0x7F) -> bool:
        """HPL analog volume control.

        :param route_enabled: Enable routing to HPL
        :param gain: Analog volume control value (0-127)
        :return: True if successful, False otherwise
        """
        return self._page1._set_hpl_volume(route_enabled, gain)

    def manual_headphone_right_volume(self, route_enabled: bool, gain: int = 0x7F) -> bool:
        """HPR analog volume control.

        :param route_enabled: Enable routing to HPR
        :param gain: Analog volume control value (0-127)
        :return: True if successful, False otherwise
        """
        return self._page1._set_hpr_volume(route_enabled, gain)

    @property
    def headphone_left_gain(self) -> int:
        """Left headphone amplifier gain in dB.

        Range is 0 dB (soft) to 9 dB (loud) in steps of 1 dB.

        In the datasheet, this is Page 1 / Register 40 (0x28): HPL Driver.

        Note that the headphone left channel volume is also affected by
        ``dac_volume``, ``headphone_volume``, and ``headphone_left_mute``.

        :getter: Return gain
        :setter: Set gain
        :raises ValueError: If set to a value outside the range of 0 to 9
        """
        reg_value = self._page1._read_register(_HPL_DRIVER)
        return (reg_value >> 3) & 0x0F

    @headphone_left_gain.setter
    def headphone_left_gain(self, gain_db: int) -> None:
        unmute = not self.headphone_left_mute
        # This call can raise ValueError
        self._page1._configure_hpl_pga(int(gain_db), unmute)

    @property
    def headphone_left_mute(self) -> bool:
        """Left headphone mute status.

        True means left headphone is muted, False means not muted.

        :getter: Return status
        :setter: Set status
        """
        reg_value = self._page1._read_register(_HPL_DRIVER)
        return not bool(reg_value & (1 << 2))

    @headphone_left_mute.setter
    def headphone_left_mute(self, mute: bool) -> None:
        gain = self.headphone_left_gain
        # This could in theory raise ValueError, but that's very unlikely
        self._page1._configure_hpl_pga(gain, not mute)

    @property
    def headphone_right_gain(self) -> int:
        """Right headphone amplifier gain in dB.

        Range is 0 dB (soft) to 9 dB (loud) in steps of 1 dB.

        In the datasheet, this is Page 1 / Register 41 (0x29): HPR Driver.

        Note that the headphone right channel volume is also affected by
        ``dac_volume``, ``headphone_volume``, and ``headphone_right_mute``.

        :getter: Return gain
        :setter: Set gain
        :raises ValueError: If set to a value outside the range of 0 to 9
        """
        reg_value = self._page1._read_register(_HPR_DRIVER)
        return (reg_value >> 3) & 0x0F

    @headphone_right_gain.setter
    def headphone_right_gain(self, gain_db: int) -> None:
        unmute = not self.headphone_right_mute
        # This call can raise ValueError
        self._page1._configure_hpr_pga(int(gain_db), unmute)

    @property
    def headphone_right_mute(self) -> bool:
        """Right headphone mute status.

        True means right headphone is muted, False means not muted.

        :getter: Return status
        :setter: Set status
        """
        reg_value = self._page1._read_register(_HPR_DRIVER)
        return not bool(reg_value & (1 << 2))

    @headphone_right_mute.setter
    def headphone_right_mute(self, mute: bool) -> None:
        gain = self.headphone_right_gain
        self._page1._configure_hpr_pga(gain, not mute)

    @property
    def speaker_gain(self) -> int:
        """Speaker amplifier gain setting in dB.

        Range is 6 dB (soft) to 24 dB (loud) in steps of 6 dB.

        In the datasheet, this is Page 1 / Register 42 (0x2A): Class-D Speaker
        (SPK) Driver.

        Note that ``dac_volume``, ``speaker_volume``, and ``speaker_mute``
        also affect the speaker output level.

        :getter: Return gain
        :setter: Set gain
        :raises ValueError: If set to anything other than 6, 12, 18, or 24
        """
        # This gives us a 2-bit unsigned integer where 0 means 6 dB, 1 is 12 dB,
        # 2 is 18 dB, and 3 is 24 dB
        reg_value = self._page1._read_register(_SPK_DRIVER)
        return (((reg_value >> 3) & 0x03) + 1) * 6

    @speaker_gain.setter
    def speaker_gain(self, gain_db: int) -> None:
        unmute = not self.speaker_mute
        # This relies on _configure_spk_pga() to raise ValueError if the gain
        # value is out of range
        self._page1._configure_spk_pga(gain_db, unmute)

    @property
    def speaker_mute(self) -> bool:
        """The speaker mute status.

        True means speaker is muted, False means unmuted.

        :getter: Return status
        :setter: Set status
        """
        reg_value = self._page1._read_register(_SPK_DRIVER)
        return not bool(reg_value & (1 << 2))

    @speaker_mute.setter
    def speaker_mute(self, mute: bool) -> None:
        gain = self.speaker_gain
        # Unmute is inverse of mute
        self._page1._configure_spk_pga(gain, not mute)

    @property
    def dac_flags(self) -> Dict[str, Any]:
        """The DAC and output driver status flags.

        :getter: Return dictionary with status flags
        """
        return self._page0._get_dac_flags()

    @property
    def gpio1_mode(self) -> int:
        """The current GPIO1 pin mode.

        One of the GPIO1_* mode constants.

        :getter: Return mode
        :setter: Set mode
        :raises ValueError: If mode is not a valid GPIO1_* constant
        """
        value = self._page0._read_register(_GPIO1_CTRL)
        return (value >> 2) & 0x0F

    @gpio1_mode.setter
    def gpio1_mode(self, mode: int) -> None:
        valid_modes: List[int] = [
            GPIO1_DISABLED,
            GPIO1_INPUT_MODE,
            GPIO1_GPI,
            GPIO1_GPO,
            GPIO1_CLKOUT,
            GPIO1_INT1,
            GPIO1_INT2,
            GPIO1_BCLK_OUT,
            GPIO1_WCLK_OUT,
        ]

        if mode not in valid_modes:
            raise ValueError(f"Invalid GPIO1 mode: {mode}. Must be one of the GPIO1_* constants.")

        self._page0._set_gpio1_mode(mode)

    @property
    def din_input(self) -> int:
        """The current DIN input value.

        :getter: Return the DIN input value
        """
        return self._page0._get_din_input()

    @property
    def codec_interface(self) -> Dict[str, Any]:
        """The current codec interface settings.

        :getter: Return dictionary with codec interface settings
        """
        return self._page0._get_codec_interface()

    @property
    def headphone_shorted(self) -> bool:
        """Check if headphone short circuit is detected.

        :getter: Return True if headphone is shorted, False otherwise
        """
        return self._page1._is_headphone_shorted()

    @property
    def speaker_shorted(self) -> bool:
        """Check if speaker short circuit is detected.

        :getter: Return True if speaker is shorted, False otherwise
        """
        return self._page1._is_speaker_shorted()

    @property
    def hpl_gain_applied(self) -> bool:
        """Check if all programmed gains have been applied to HPL.

        :getter: Return True if gains are applied, False otherwise
        """
        return self._page1._is_hpl_gain_applied()

    @property
    def hpr_gain_applied(self) -> bool:
        """Check if all programmed gains have been applied to HPR.

        :getter: Return True if gains are applied, False otherwise
        """
        return self._page1._is_hpr_gain_applied()

    @property
    def speaker_gain_applied(self) -> bool:
        """Check if all programmed gains have been applied to Speaker.

        :getter: Return True if gains are applied, False otherwise
        """
        return self._page1._is_spk_gain_applied()

    @property
    def headset_status(self) -> int:
        """Current headset detection status.

        :getter: Return Integer value representing headset status (0=none,
            1=without mic, 3=with mic)
        """
        return self._page0._get_headset_status()

    @property
    def reset_speaker_on_scd(self) -> bool:
        """The speaker reset mode for short circuit detection.

        True if speaker resets on short circuit, False otherwise.

        :getter: Return mode
        :setter: Set mode
        """
        value = self._page1._read_register(_HP_SPK_ERR_CTL)
        return not bool((value >> 1) & 0x01)

    @reset_speaker_on_scd.setter
    def reset_speaker_on_scd(self, reset: bool) -> None:
        self._page1._reset_speaker_on_scd(reset)

    @property
    def reset_headphone_on_scd(self) -> bool:
        """The headphone reset mode for short circuit detection.

        True if headphone resets on short circuit, False otherwise.

        :getter: Return mode
        :setter: Set mode
        """
        value = self._page1._read_register(_HP_SPK_ERR_CTL)
        return not bool(value & 0x01)

    @reset_headphone_on_scd.setter
    def reset_headphone_on_scd(self, reset: bool) -> None:
        self._page1._reset_headphone_on_scd(reset)

    def configure_headphone_pop(
        self, wait_for_powerdown: bool = True, powerup_time: int = 0x07, ramp_time: int = 0x03
    ) -> bool:
        """Headphone pop removal settings.

        :param wait_for_powerdown: Wait for amp powerdown before DAC powerdown
        :param powerup_time: Driver power-on time (0-11)
        :param ramp_time: Driver ramp-up step time (0-3)
        :return: True if successful, False otherwise
        """
        return self._page1._configure_headphone_pop(wait_for_powerdown, powerup_time, ramp_time)

    @property
    def speaker_wait_time(self) -> int:
        """The current speaker power-up wait time.

        Speaker power-up wait duration (0-7).

        :getter: Return wait time
        :setter: Set wait time
        """
        value = self._page1._read_register(_PGA_RAMP)
        return (value >> 4) & 0x07

    @speaker_wait_time.setter
    def speaker_wait_time(self, wait_time: int) -> None:
        self._page1._set_speaker_wait_time(wait_time)

    @property
    def headphone_lineout(self) -> bool:
        """The current headphone line-out configuration.

        :getter: Return True if both channels are configured as line-out, False
            otherwise
        :setter: True to configure both channels as line-out, False otherwise
        """
        value = self._page1._read_register(_HP_DRIVER_CTRL)
        left = bool(value & (1 << 2))
        right = bool(value & (1 << 1))
        return left and right

    @headphone_lineout.setter
    def headphone_lineout(self, enabled: bool) -> None:
        self._page1._headphone_lineout(enabled, enabled)

    def config_mic_bias(
        self, power_down: bool = False, always_on: bool = False, voltage: int = 0
    ) -> bool:
        """MICBIAS settings.

        :param power_down: Enable software power down
        :param always_on: Keep MICBIAS on even without headset
        :param voltage: MICBIAS voltage setting (0-3)
        :return: True if successful, False otherwise
        """
        return self._page1._config_mic_bias(power_down, always_on, voltage)

    def set_input_common_mode(self, ain1_cm: bool, ain2_cm: bool) -> bool:
        """Analog input common mode connections.

        :param ain1_cm: Connect AIN1 to common mode when unused
        :param ain2_cm: Connect AIN2 to common mode when unused
        :return: True if successful, False otherwise
        """
        return self._page1._set_input_common_mode(ain1_cm, ain2_cm)

    def config_delay_divider(self, use_mclk: bool = True, divider: int = 1) -> bool:
        """Programmable delay timer clock source and divider.

        :param use_mclk: True to use external MCLK, False for internal oscillator
        :param divider: Clock divider (1-127, or 0 for 128)
        :return: True if successful, False otherwise
        """
        return self._page3._config_delay_divider(use_mclk, divider)

    @property
    def vol_adc_pin_control(self) -> bool:
        """The volume ADC pin control status.

        True if volume ADC pin control is enabled, False otherwise.

        This is for using an analog input pin, probably connected to a
        potentiometer, to control the volume. You can ignore this if you want
        to control volume from software over I2C.

        :getter: Return status
        :setter: Set status
        """
        reg_value = self._page0._read_register(_VOL_ADC_CTRL)
        return bool(reg_value & (1 << 7))

    @vol_adc_pin_control.setter
    def vol_adc_pin_control(self, enabled: bool) -> None:
        current_config = self._get_vol_adc_config()
        self._page0._config_vol_adc(
            enabled,
            current_config["use_mclk"],
            current_config["hysteresis"],
            current_config["rate"],
        )

    @property
    def vol_adc_use_mclk(self) -> bool:
        """The volume ADC use MCLK status.

        True means volume ADC uses MCLK, False means internal oscillator.

        :getter: Return status
        :setter: Set status
        """
        reg_value = self._page0._read_register(_VOL_ADC_CTRL)
        return bool(reg_value & (1 << 6))

    @vol_adc_use_mclk.setter
    def vol_adc_use_mclk(self, use_mclk: bool) -> None:
        current_config = self._get_vol_adc_config()
        self._page0._config_vol_adc(
            current_config["pin_control"],
            use_mclk,
            current_config["hysteresis"],
            current_config["rate"],
        )

    @property
    def vol_adc_hysteresis(self) -> int:
        """The volume ADC hysteresis setting.

        Hysteresis value (0-3).

        :getter: Return value
        :setter: Set value
        """
        reg_value = self._page0._read_register(_VOL_ADC_CTRL)
        return (reg_value >> 4) & 0x03

    @vol_adc_hysteresis.setter
    def vol_adc_hysteresis(self, hysteresis: int) -> None:
        current_config = self._get_vol_adc_config()
        self._page0._config_vol_adc(
            current_config["pin_control"],
            current_config["use_mclk"],
            hysteresis,
            current_config["rate"],
        )

    @property
    def vol_adc_rate(self) -> int:
        """The volume ADC sampling rate.

        Rate value (0-7).

        :getter: Return value
        :setter: Set value
        """
        reg_value = self._page0._read_register(_VOL_ADC_CTRL)
        return reg_value & 0x07

    @vol_adc_rate.setter
    def vol_adc_rate(self, rate: int) -> None:
        current_config = self._get_vol_adc_config()
        self._page0._config_vol_adc(
            current_config["pin_control"],
            current_config["use_mclk"],
            current_config["hysteresis"],
            rate,
        )

    def _get_vol_adc_config(self) -> Dict[str, Any]:
        """Helper method for the current volume ADC configuration.

        :return: Dictionary with current volume ADC configuration
        """
        reg_value = self._page0._read_register(_VOL_ADC_CTRL)
        return {
            "pin_control": bool(reg_value & (1 << 7)),
            "use_mclk": bool(reg_value & (1 << 6)),
            "hysteresis": (reg_value >> 4) & 0x03,
            "rate": reg_value & 0x07,
        }

    @property
    def vol_adc_db(self) -> float:
        """The current volume from the Volume ADC in dB.

        :getter: Return Volume in dB
        """
        return self._page0._read_vol_adc_db()

    def int2_sources(
        self,
        headset_detect: bool = False,
        button_press: bool = False,
        dac_drc: bool = False,
        agc_noise: bool = False,
        over_current: bool = False,
        multiple_pulse: bool = False,
    ) -> bool:
        """Configure the INT2 interrupt sources.

        :param headset_detect: Enable headset detection interrupt
        :param button_press: Enable button press detection interrupt
        :param dac_drc: Enable DAC DRC signal power interrupt
        :param agc_noise: Enable DAC data overflow interrupt
        :param over_current: Enable short circuit interrupt
        :param multiple_pulse: If true, INT2 generates multiple pulses until flag read
        :return: True if successful, False otherwise
        """
        return self._page0._set_int2_source(
            headset_detect, button_press, dac_drc, agc_noise, over_current, multiple_pulse
        )

    def configure_clocks(
        self, sample_rate: int, bit_depth: int = 16, mclk_freq: Optional[int] = None
    ):
        """Configure the TLV320DAC3100 clock settings.

        This function configures all necessary clock settings including PLL,
        dividers, and interface settings to achieve the requested sample rate.

        :param sample_rate: The desired sample rate in Hz. Supported sample
            rates are 8000, 11025, 22050, 44100, and 48000. But, to get good
            quality at low sample rates, you need to use MCLK instead of BCLK.
        :param bit_depth: The bit depth (16). CircuitPython I2S always sends
            16-bit stereo, so set this to 16.
        :param mclk_freq: The main clock (MCLK) frequency (None or 15_000_000).
            This controls how the DAC uses its PLL to generate the delta-sigma
            modulator's oversampling clock. For None (the default), the PLL
            uses the bit clock pin (BCLK) as its input clock. Sound quality for
            BCLK has a higher noise floor and lots of harmonic distortion at
            lower sample rates. For better audio quality, set mclk_freq to
            15_000_000 and supply a 15 MHz clock signal to the MCLK pin. You
            can use pwmio.PWMOut to generate the 15 MHz clock.
        :return: True if successful, False otherwise
        """
        self._sample_rate = sample_rate
        self._bit_depth = bit_depth
        if mclk_freq is not None:
            self._mclk_freq = mclk_freq
        else:
            self._mclk_freq = 0  # Internally use 0 to indicate BCLK mode

        return self._page0._configure_clocks_for_sample_rate(
            self._mclk_freq, sample_rate, bit_depth
        )

    @property
    def headphone_output(self) -> bool:
        """Headphone output helper with quickstart default settings.

        If you set this property to True, the setter will set defaults that
        are intended for listening at a quiet-ish level with sensitive low
        impedance earbuds:

        * dac_volume = -20
        * headphone_volume = -30.1
        * headphone_left_gain = headphone_right_gain = 0

        If you set this to False, the setter turns off the headphone amp.

        :getter: Return headphone output state: True if either left or right
            headphone amplifier is powered, False otherwise.
        :setter: **This sets several properties to prepare for headphone use**.
            Changed properties include DAC channel enable/volume/mute, DAC
            path, headphone gain, headphone common mode voltage, and headphone
            mute.
        """
        hp_drivers = self._page1._read_register(_HP_DRIVERS)
        left_powered = bool(hp_drivers & (1 << 7))
        right_powered = bool(hp_drivers & (1 << 6))
        return left_powered or right_powered

    @headphone_output.setter
    def headphone_output(self, enabled: bool) -> None:
        if enabled:
            self.left_dac = True
            self.right_dac = True
            self.left_dac_channel_volume = -20
            self.right_dac_channel_volume = -20
            self.left_dac_mute = False
            self.right_dac_mute = False
            self.left_dac_path = DAC_PATH_NORMAL
            self.right_dac_path = DAC_PATH_NORMAL
            self.headphone_left_gain = 6
            self.headphone_right_gain = 6
            self._page1._configure_headphone_driver(
                left_powered=True, right_powered=True, common=HP_COMMON_1_65V
            )
            self.headphone_volume = -20.1
            # NOTE: If you use DAC_ROUTE_HP here instead of DAC_ROUTE_MIXER,
            # the DAC output will bypass the headphone analog volume
            # attenuation stage and go straight into the headphone amp. That
            # might possibly be useful to save power, but it reduces your gain
            # adjustment options. For low impedance headphones, it's helpful to
            # have a lot of attenuation between the DAC and the headphone amp.
            # Otherwise, you may have to operate the DAC volume setting down
            # near the bottom of its usable range.
            self._page1._configure_analog_inputs(
                left_dac=DAC_ROUTE_MIXER, right_dac=DAC_ROUTE_MIXER
            )
            self.headphone_left_mute = False
            self.headphone_right_mute = False
        else:
            self._page1._configure_headphone_driver(left_powered=False, right_powered=False)

    @property
    def speaker_output(self) -> bool:
        """Speaker output helper with quickstart default settings.

        If you set this property to True, the setter will set defaults intended
        for a relatively quiet listening level using the 8Ω 1W mini speaker
        that comes bundled with the Fruit Jam:

        * dac_volume = -20
        * speaker_volume = -20.1
        * speaker_gain = 6

        If you set this to False, the setter turns off the speaker amp.

        :getter: Return speaker output state: True if speaker amplifier is
            powered, False otherwise.
        :setter: **This sets several properties to prepare for speaker use**.
            Changed properties include DAC channel enable/volume/mute, DAC
            path, speaker volume, speaker amplifier gain, and speaker mute.
        """
        return self._page1._get_speaker_enabled()

    @speaker_output.setter
    def speaker_output(self, enabled: bool) -> None:
        if enabled:
            self.left_dac = True
            self.right_dac = True
            self.left_dac_channel_volume = -20
            self.right_dac_channel_volume = -20
            self.left_dac_mute = False
            self.right_dac_mute = False
            self.left_dac_path = DAC_PATH_NORMAL
            self.right_dac_path = DAC_PATH_NORMAL
            self.speaker_gain = 12
            self._page1._set_speaker_enabled(True)
            self._page1._configure_analog_inputs(
                left_dac=DAC_ROUTE_MIXER, right_dac=DAC_ROUTE_MIXER
            )
            self.speaker_volume = -10
            self.speaker_mute = False
        else:
            self._page1._set_speaker_enabled(False)

    @property
    def headphone_volume(self) -> float:
        """Current headphone analog volume in dB.

        Range is 0 (loud) to -78.3 (very soft).

        This acts on two registers at once. In the datasheet they are:

        * Page 1 / Register 36 (0x24): Left Analog Volume to HPL
        * Page 1 / Register 37 (0x25) Right Analog Volume to HPR

        Note that headphone output is also affected by ``dac_volume``,
        ``headphone_left_gain``, ``headphone_right_gain``,
        ``headphone_left_mute``, and ``headphone_right_mute``.

        :getter: Return volume
        :setter: Set volume
        """
        left_gain_u7 = self._page1._read_register(_HPL_VOL) & 0x7F
        right_gain_u7 = self._page1._read_register(_HPR_VOL) & 0x7F
        left_db = _table_6_24_uint7_to_db(left_gain_u7)
        right_db = _table_6_24_uint7_to_db(right_gain_u7)
        if left_db == right_db:
            return left_db
        else:
            return (left_db + right_db) / 2

    @headphone_volume.setter
    def headphone_volume(self, db: float) -> None:
        # The table 6-24 lookup function includes min/max range clipping
        gain_u7 = _table_6_24_db_to_uint7(db)
        self._page1._set_hpl_volume(route_enabled=True, gain=gain_u7)
        self._page1._set_hpr_volume(route_enabled=True, gain=gain_u7)

    @property
    def speaker_volume(self) -> float:
        """Current speaker analog volume in dB.

        Range is 0 (loud) to -78.3 (very soft).

        In the datasheet, this is Page 1 / Register 38 (0x26): Left Analog
        Volume to SPK.

        Note that ``dac_volume``, ``speaker_gain``, and ``speaker_mute`` also
        affect the speaker output level.

        :getter: Return volume
        :setter: Set volume
        """
        gain_u7 = self._page1._read_register(_SPK_VOL) & 0x7F
        return _table_6_24_uint7_to_db(gain_u7)

    @speaker_volume.setter
    def speaker_volume(self, db: float) -> None:
        # The table 6-24 lookup function includes min/max range clipping
        gain_u7 = _table_6_24_db_to_uint7(db)
        self._page1._set_spk_volume(route_enabled=True, gain=gain_u7)

    @property
    def sample_rate(self) -> int:
        """Configured sample rate in Hz.

        :getter: Return The sample rate in Hz
        """
        return self._sample_rate

    @property
    def bit_depth(self) -> int:
        """Configured bit depth.

        :getter: Return The bit depth
        """
        return self._bit_depth

    @property
    def mclk_freq(self) -> int:
        """Configured MCLK frequency in Hz.

        :getter: Return The MCLK frequency in Hz
        """
        return self._mclk_freq
