#include <stdint.h>
#include <cstring>
#include <string>
#include <vector>

extern "C"
{
#include <libusb.h>
#include "log.h"
}

#include "linkadapt.h"
#include "commands.h"

#define USB_TRANSFER_TIMEOUT 1500

// Stlink Status commands

#define STLINK_GET_VERSION 0xF1
#define STLINK_GET_CURRENT_MODE 0xF5
#define STLINK_GET_TARGET_VOLTAGE 0xF7

#define STLINK_DEV_DFU_MODE 0x00
#define STLINK_DEV_MASS_MODE 0x01
#define STLINK_DEV_DEBUG_MODE 0x02
#define STLINK_DEV_UNKNOWN_MODE -1

// Stlink Control

#define STLINK_JTAG_DRIVE_NRST 0x3C
#define STLINK_DEBUG_COMMAND 0xF2
#define STLINK_DFU_COMMAND 0xF3
#define STLINK_DFU_EXIT 0x07
#define STLINK_DEBUG_ERR_OK 0x80

// USB Pids for various Stlink devices

#define STLINK_USB_VID_ST 0x0483
#define STLINK_USB_PID_STLINK_32L 0x3748
#define STLINK_USB_PID_STLINK_32L_AUDIO 0x374a
#define STLINK_USB_PID_STLINK_NUCLEO 0x374b
#define STLINK_USB_PID_STLINK_V2_1 0x3752
#define STLINK_USB_PID_STLINK_V3_USBLOADER 0x374d
#define STLINK_USB_PID_STLINK_V3E_PID 0x374e
#define STLINK_USB_PID_STLINK_V3S_PID 0x374f
#define STLINK_USB_PID_STLINK_V3_2VCP_PID 0x3753
#define STLINK_USB_PID_STLINK_V3_PWR 0x3757

static inline uint8_t *PACK16(uint8_t *buf, uint16_t val)
{
    buf[0] = val;
    buf[1] = val >> 8;
    return buf + 2;
}

static inline uint8_t *PACK32(uint8_t *buf, uint32_t val)
{
    buf[0] = val;
    buf[1] = val >> 8;
    buf[2] = val >> 16;
    buf[3] = val >> 24;
    return buf + 4;
}

static inline uint32_t UNPACK32(uint8_t *buf)
{
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

static inline uint16_t UNPACK16(uint8_t *buf)
{
    return buf[0] | (buf[1] << 8);
}

// Helper class for stlink commands

class LinkAdapt::LinkReq
{
public:
    uint8_t cmd[16] = {0};
    uint8_t *buf = nullptr;
    uint16_t len = 0;
};

bool LinkAdapt::usb_xfer(bool ReadnWrite, unsigned char *buf, int length)
{
    int err;
    int res;
    unsigned char ep = ReadnWrite ? ep_in : ep_out;

    if ((err = libusb_bulk_transfer(handle, ep, buf, length, &res, USB_TRANSFER_TIMEOUT)))
    {
        log_error("%s", libusb_error_name(err));
        return false;
    }
    if (res != length)
    {
        log_error("Transferred %d bytes instead of %d", res, length);
        return false;
    }
    return true;
}

bool LinkAdapt::cmd_eval(LinkReq &req, bool ReadnWrite)
{
    if (!usb_xfer(false, req.cmd, sizeof(req.cmd)))
    {
        log_error("cmd_eval request failed");
        return false;
    }
    if (req.len && !usb_xfer(ReadnWrite, req.buf, req.len))
    {
        log_error("cmd_eval response failed");
        return false;
    }
    return true;
}

bool LinkAdapt::current_mode(uint16_t &mode)
{
    uint8_t result[2];
    LinkReq req;
    req.cmd[0] = STLINK_GET_CURRENT_MODE;
    req.len = sizeof(result);
    req.buf = (uint8_t *)&result;
    if (cmd_eval(req, true))
    {
        mode = UNPACK16(result);
        return true;
    }
    return false;
}

bool LinkAdapt::exit_debug_mode()
{
    LinkReq req;
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_DEBUG_EXIT;
    req.len = 0;
    return cmd_eval(req, true);
}

bool LinkAdapt::enter_debug_mode()
{
    uint8_t result[2];
    LinkReq req;
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_DEBUG_APIV2_ENTER;
    req.cmd[2] = STLINK_DEBUG_ENTER_SWD;
    req.len = sizeof(result);
    req.buf = result;
    if (cmd_eval(req, true))
    {
        uint16_t status = UNPACK16(result);
        if (status != STLINK_DEBUG_ERR_OK)
        {
            log_error("unexpected error %u", status);
            return false;
        }
        return true;
    }
    return false;
}

bool LinkAdapt::exit_dfu_mode()
{
    LinkReq req = {0};
    req.cmd[0] = STLINK_DFU_COMMAND;
    req.cmd[1] = STLINK_DFU_EXIT;
    req.len = 0;
    return cmd_eval(req, true);
}

LinkAdapt::LinkAdapt()
{
    int err = libusb_init(&ctx);
    if (err < 0)
    {
        log_error("USB Init error %s", libusb_error_name(err));
    }
}

LinkAdapt::~LinkAdapt()
{
    Detach();
    if (ctx)
    {
        libusb_exit(ctx);
    }
}

static bool isSTlink(uint16_t vid, uint16_t pid)
{
    if (vid != STLINK_USB_VID_ST)
        return false;
    switch (pid)
    {
    case STLINK_USB_PID_STLINK_32L:
    case STLINK_USB_PID_STLINK_32L_AUDIO:
    case STLINK_USB_PID_STLINK_NUCLEO:
    case STLINK_USB_PID_STLINK_V2_1:
    case STLINK_USB_PID_STLINK_V3E_PID:
    case STLINK_USB_PID_STLINK_V3S_PID:
    case STLINK_USB_PID_STLINK_V3_PWR:
        return true;
    default:
        return false;
    }
}

bool LinkAdapt::Available(std::vector<UsbDev> &usbdevs)
{
    std::vector<UsbDev> stlinks;
    libusb_device **devs = nullptr;
    int err;
    ssize_t cnt;

    do
    {
        // get a list of devices

        cnt = libusb_get_device_list(ctx, &devs);

        if (cnt < 0)
        {
            log_error("USB Init error %s", libusb_error_name(cnt));
            break;
        }

        // search for valid device

        for (ssize_t i = 0; i < cnt; i++)
        {
            libusb_device_descriptor desc;
            err = libusb_get_device_descriptor(devs[i], &desc);
            if (err < 0)
            {
                log_error("USB Init error %s", libusb_error_name(cnt));
                break;
            }

            if (!isSTlink(desc.idVendor, desc.idProduct))
                continue;

            UsbDev stl;
            stl.vid = desc.idVendor;
            stl.pid = desc.idProduct;
            stl.bus = libusb_get_bus_number(devs[i]);
            stl.address = libusb_get_device_address(devs[i]);
            stlinks.push_back(stl);
        }

        libusb_free_device_list(devs, 1);
    } while (0);
    usbdevs = stlinks;
    return true;
}

bool LinkAdapt::Attach(UsbDev usbdev)
{

    libusb_device **devs = nullptr;
    int err;
    ssize_t cnt;
    handle = nullptr;

    do
    {
        // initialize a library session

        // get a list of devices

        cnt = libusb_get_device_list(ctx, &devs);

        if (cnt < 0)
        {
            log_error("USB Init error %s", libusb_error_name(cnt));
            break;
        }

        // search for valid device

        for (ssize_t i = 0; i < cnt; i++)
        {
            libusb_device_descriptor desc;
            err = libusb_get_device_descriptor(devs[i], &desc);
            if (err < 0)
            {
                log_error("USB Init error %s", libusb_error_name(cnt));
                break;
            }

            // here we check if the rules match
            //    vid/pid specified
            //    serial number specified

            if (usbdev.vid && usbdev.pid)
            {
                if ((desc.idVendor != usbdev.vid) || (desc.idProduct != usbdev.pid))
                {
                    continue;
                }
                if ((libusb_get_bus_number(devs[i]) != usbdev.bus) ||
                    (libusb_get_device_address(devs[i]) != usbdev.address))
                {
                    continue;
                }
            }
            else if (!isSTlink(desc.idVendor, desc.idProduct))
            {
                continue;
            }

            // open device

            err = libusb_open(devs[i], &handle);
            if (err)
            {
                log_error("USB Open error %s", libusb_error_name(err));
                handle = nullptr;
                continue;
            }

            unsigned char serial_str[64];
            libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, serial_str, 64);
            /* if (serial && strncmp(serial, (char *)serial_str, sizeof(serial_str)))
            {
                libusb_close(handle);
                handle = nullptr;
                continue;
            } */

            // set endpoints based upon pid

            ep_in = 1 | LIBUSB_ENDPOINT_IN;
            switch (desc.idProduct)
            {
            case STLINK_USB_PID_STLINK_NUCLEO:
            case STLINK_USB_PID_STLINK_32L_AUDIO:
            case STLINK_USB_PID_STLINK_V2_1:
            case STLINK_USB_PID_STLINK_V3E_PID:
            case STLINK_USB_PID_STLINK_V3S_PID:
            case STLINK_USB_PID_STLINK_V3_2VCP_PID:
                ep_out = 1 | LIBUSB_ENDPOINT_OUT;
                break;
            default:
                ep_out = 2 | LIBUSB_ENDPOINT_OUT;
            }

            log_debug("Device Serial: %s", serial_str);
            libusb_get_device_descriptor(devs[i], &desc);
            log_debug("Device Manufacturer: %s", serial_str);
            libusb_get_string_descriptor_ascii(handle, desc.iProduct, serial_str, 64);
            log_debug("Device Product: %s", serial_str);
            libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, serial_str, 64);

            log_debug("Device Bus %03u Device %03u",
                      libusb_get_bus_number(devs[i]),
                      libusb_get_device_address(devs[i]));

            break;
        }

        libusb_free_device_list(devs, 1);

        if (handle)
        {
            int config;

            //  Configure usb handle

            err = libusb_get_configuration(handle, &config);
            {
                if (err)
                {
                    log_error("couldn't get config: %s", libusb_error_name(err));
                    break;
                }
            }

            if (config != 1)
            {
                err = libusb_set_configuration(handle, 1);
                if (err)
                {
                    log_error("setting config failed: %s", libusb_error_name(err));
                    break;
                }
            }

            // auto detach kernel driver on relevant platforms.  Ignore
            // return code because this isn't supported on windows.

            libusb_set_auto_detach_kernel_driver(handle, 1);

            err = libusb_claim_interface(handle, 0);
            if (err)
            {
                log_error("claim interface failed %s", libusb_error_name(err));
                break;
            }

            log_debug("Interface claimed !");

            uint16_t mode;
            if (current_mode(mode))
            {
                // log_debug("current mode: %d", mode);
                if ((mode == STLINK_DEV_DFU_MODE) && !exit_dfu_mode())
                {
                    log_error("exit dfu mode failed");
                    break;
                }
                AssertReset(false);
                enter_debug_mode();
                // is this the right test ??  perhaps our stlink implementation
                // is wrong.  != 4 left to work with existing bases
                if (!current_mode(mode) || (mode != 4 && mode != STLINK_DEV_DEBUG_MODE))
                {
                    log_error("enter swd mode failed: %d", mode);
                    break;
                }
            }
            return true;
        }
        else
        {
            log_debug("No matching devices found");
        }
    } while (0);

    Detach();
    return false;
}

void LinkAdapt::Detach()
{
    if (handle)
    {
        exit_debug_mode();
        libusb_close(handle);
    }
    handle = nullptr;
}

bool LinkAdapt::IsAttached()
{
    return handle;
}

bool LinkAdapt::Voltage(float &voltage)
{
    uint8_t result[8];
    LinkReq req;
    req.cmd[0] = STLINK_GET_TARGET_VOLTAGE;
    req.len = sizeof(result);
    req.buf = result;
    if (cmd_eval(req, true))
    {
        float factor = UNPACK32(&result[0]);
        float reading = UNPACK32(&result[4]);
        voltage = 2.4 * reading / factor;
        return true;
    }

    log_error("Voltage failed");
    return false;
}

bool LinkAdapt::get_rw_status(uint16_t &status)
{
    LinkReq req;
    uint8_t result[2];
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_DEBUG_APIV2_GETLASTRWSTATUS;
    req.len = sizeof(result);
    req.buf = result;
    if (cmd_eval(req, true))
    {
        status = UNPACK16(result);
        return true;
    }
    return false;
}

bool LinkAdapt::ReadMem32(unsigned int addr, uint8_t *buf, uint16_t count)
{
    LinkReq req;
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_DEBUG_READMEM_32BIT;
    PACK32(&req.cmd[2], addr);
    PACK16(&req.cmd[6], count);
    req.len = count;
    req.buf = buf;
    if (cmd_eval(req, true))
    {
        uint16_t status;
        if (!get_rw_status(status) || (status != STLINK_DEBUG_ERR_OK))
        {
            log_error("error return %x", status);
            return false;
        }
        return true;
    }
    return false;
}

bool LinkAdapt::WriteMem32(unsigned int addr, uint8_t *buf, uint16_t count)
{
    LinkReq req;
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_DEBUG_WRITEMEM_32BIT;
    PACK32(&req.cmd[2], addr);
    PACK16(&req.cmd[6], count);
    req.len = count;
    req.buf = buf;
    if (cmd_eval(req, false))
    {
        uint16_t status;
        if (!get_rw_status(status) || (status != STLINK_DEBUG_ERR_OK))
        {
            log_error("error return %x", status);
            return false;
        }
        return true;
    }
    return false;
}

bool LinkAdapt::ReadDebug32(unsigned int addr, uint32_t *rval)
{
    LinkReq req;
    uint8_t result[8];
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_DEBUG_APIV2_READDEBUGREG;
    PACK32(&req.cmd[2], addr);
    req.len = sizeof(result);
    req.buf = result;
    if (cmd_eval(req, true))
    {
        uint16_t status = UNPACK16(result);
        if (status != STLINK_DEBUG_ERR_OK)
        {
            log_error("error return %x", status);
            return false;
        }
        else
        {
            *rval = UNPACK32(&result[4]);
            return true;
        }
    }
    return false;
}

bool LinkAdapt::WriteDebug32(unsigned int addr, uint32_t val)
{
    LinkReq req;
    uint8_t result[2];
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_DEBUG_APIV2_WRITEDEBUGREG;
    PACK32(&req.cmd[2], addr);
    PACK32(&req.cmd[6], val);
    req.len = 2;
    req.buf = result;
    if (cmd_eval(req, true))
    {
        uint16_t status = UNPACK16(result);
        if (status != STLINK_DEBUG_ERR_OK)
        {
            log_error("error %x", status);
            return false;
        }
        return true;
    }
    return false;
}

bool LinkAdapt::AssertReset(bool high)
{
    LinkReq req;
    uint8_t result[2];
    req.cmd[0] = STLINK_DEBUG_COMMAND;
    req.cmd[1] = STLINK_JTAG_DRIVE_NRST;
    req.cmd[2] = high ? 1 : 0;
    req.buf = result;
    req.len = 2;
    if (cmd_eval(req, true))
    {
        uint16_t status = UNPACK16(result);
        if (status != STLINK_DEBUG_ERR_OK)
        {
            log_error("assert reset failed: %x", status);
            return false;
        }
        return true;
    }
    return false;
}