# libsigrok-ad

Trying to port the Analog Discovery into Sigrok.

Task list / roadmap
* Documentation of the many modules involved [WIP]
* Program the FPGA with the Digilent bitstream via JTAG [done]
* Reverse engineer the bitstream protocol [WIP]
* Implement a prototype to use the FIFO buffers of the FTDI to read/write data to the FPGA firmware. [TODO]
* Understand and parse the data we get from the FPGA firmware [TODO]
* Assemble a working software (within the Sigork project) to display the data [TODO]
* Try to merge our code into the Sigork project [TODO]

## Discovering the Analog Discovery

The communication with the Analog Discovery is based on a FTDI chip.

The chip contains an EEPROM that can store data about the device behind the
chip. Digilent mostly use this to see what protocol the device can speak.

The format is the following:

### EEPROM values

````
offset,size - description

0x22,4  - OEM id
    Valid OEM id
    * 0x44696769
    * 0x356A92C7
    * 0x586F656D
0x26,4  - Product id
    Product : product_id >> 20 & 0xFFF
    Variant : product_id >> 8 & 0xFF
    Fwid    : product_id & 0xFF
0x30,16 - Username
0x3b,?  - Product name
0x58,4  - Firmware id
0x5c,4  - Device capability
    Capability list
    * JTAG      = 0x00000001
    * dcapPio   = 0x00000002
    * dcapEpp   = 0x00000004
    * dcapStm   = 0x00000008
    * SPI       = 0x00000010 (Serial Protocl Interface)
    * TWI       = 0x00000020 (Two-Wire Interface)
    * dcapAci   = 0x00000040
    * dcapAio   = 0x00000080
    * dcapEmc   = 0x00000100
    * dcapDci   = 0x00000200
    * dcapGio   = 0x00000400
    * PTI       = 0x00000800 (Parallel Trace Interface?)
    * all       = 0xFFFFFFFF
```

You can fetch the EEPROM data using libftdi.

### Speaking with the Analog Discovery

There is 2 way to speak with the Analog Discovery, through the MPSSE engine
of the FTDI chip:

* JTAG - used to push the bitstream to the FPGA
* PTI (Parallel Trace Interface?) - use to read / write values into the FPGA

(flag 0x01080000 in the device field in the EEPROM)

### Digilent Adept

Waveforms is based on Digilent Adept, a library doing a *lot* of things.
We're mainly interested in the MPSSE implementation over libft2232.

Essentially, they ship with, what is called, a `firmware` library (starts with
`FTDIFW_`) , which implements the last layer of the JTAG / PTI protocol to talk
with the FPGA.

Over this library, they run a custom JTAG / PTI protocol. They are useless
for a final implementation in Sigrok, but nonetheless interesting to see from
higher level how everything works.

Those two custom protocols are implemented in libdpti and libdjtg (every other
protocol in the EEPROM are implemented in their respective library).

Still in Adept, a particular library will make use of those libraries: libjtsc.
This library is used to implement the FPGA programming logic, using libjtg.

### Digilent Waveforms

Waveform is a small software that runs over Digilent Adept. Its main library
is libdwf, and dynamically load Adept libraries, making everything a pain to
follow.

libdwf makes uses of a lot of libraries:
* libdmgr - meh?
* libdmgt - meh?
* libdstm - meh?
* libdjtsc - scan the jtag chain to find the fpga and load the bitstream
* libdpti - read/write data to the FPGA (over custom protocol over PTI)
* libdjtg - speak JTAG with the FPGA (over a custom protocol over JTAG)

## JTAG protocol

JTAG is used to push the bitstream to the FPGA. You can play with the FPGA
using the urjtag patch i've made (see the urjtag-ad repository).

When you install Waveforms, a bunch of bitstream comes with it (files ending
with `.bit`).

There are a bunch of `firmware` that implement different functionnalities, for
two kinds of FPGA:
* Spartan-3E xc3s1600e:fg320 (?) - http://www.xilinx.com/support/documentation/data_sheets/ds312.pdf
* Spartan-6 xc6slx16l:cpg196 - http://www.xilinx.com/support/documentation/data_sheets/ds160.pdf

The Analog Discovery uses a Spartan-6 FPGA, so you shoudl be able to  use any
bitstream beginning with `DCFG_02_`.

(again, see the urjtag-ad repository for more information).

Their custom protocol is defined as below:
```
# HEADER (8 bytes)
* [00] zero
* [01] zero
* [02] zero
* [03] zero
* [04] packet length - 1
* [05] 0x02 (protocol type: jtag)
* [06] command
    0x00 - enable
    0x01 - disable
    0x02 - ???
    0x03 - set speed
    0x04 - get speed
    0x05 - set tms-tdi-tck
    0x06 - get tms-tdi-tdo-tck
    0x07 - clock tck
    0x08 - put tdi bits
    0x09 - get tdo
    0x0A - put tms-tdi bits
    0x0B - put tms bits
    0x0C - enable trans buffering
    0x0D - wait
    0x0E - sync buffer
    0x0F - set scan format
    0x10 - get scan format
    0x11 - set ready count
    0x12 - get ready count
    0x13 - set delay count
    0x14 - get delay count
    0x15 - escape
    0x16 - check packet
    0x17 - set batch properties
    0x18 - get batch properties
    0x19 - set aux reset
* [07] port (if device support multiple port)
# PAYLOAD (340 bytes)
* [08] payload
* [xx] payload
```

This packet is send via libdjtg to the firmware (FTDIFW_XYZ) which parses the custom protocol
to implement the real JTAG protocol over MPSSE.

Everything that concerns the FPGA scanning / programming goes through libjtsc
(at least..). I'm still not sure what they do over JTAG.

## PTI protocol

This protocol is used to read/write to the FPGA firmware (e.g. reading datastream
from the pins)

The protocol is obviously simpler than the JTAG one, and is defined as below:
```
# HEADER (8 bytes)
* [00] zero
* [01] zero
* [02] zero
* [03] zero
* [04] packet length - 1
* [05] 0x0B (protocol type: pti)
* [06] command
    0x00 - enable
    0x01 - disable
    0x02 - ???
    0x03 - input / output
    0x04 - set chunk size
    0x05 - get chunk size
* [07] port (if device support multiple port)
# PAYLOAD (340 bytes)
* [08] payload
* [xx] payload
```

This packet is send via libdpti to the firmware (FTDIFW_XYZ) which parses the custom
protocol to implement the real one.

I think this is simply a wrapper for the FIFO mode in FTDI. There is a sub-protocol
to talk with the configuration part (e.g. capture frequency) or to get data sampled
by the FPGA. This sub-protocol must be reverse engineered since this is exactly
what we need.
