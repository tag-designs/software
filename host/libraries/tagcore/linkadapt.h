#ifndef LINK_ADAPT_H
#define LINK_ADAPT_H
#include <stdint.h>
#include <string>
#include <vector>

#ifndef TAGCORE_ENABLE_INSTRUMENTATION
#define TAGCORE_ENABLE_INSTRUMENTATION 1
#endif

struct libusb_device_handle;
struct libusb_context;


/**
 * @struct  UsbDev
 * @brief   One ST-LINK found on USB by LinkAdapt::Available(), and the
 *          selector LinkAdapt::Attach() matches against.
 *
 * @details An all-zero UsbDev asks Attach() for the first ST-LINK found.
 */
struct UsbDev
{
    UsbDev(): vid(0),pid(0), bus(0), address(0) { } 
    uint16_t vid;     ///< USB vendor ID (0x0483 for ST).
    uint16_t pid;     ///< USB product ID; identifies the ST-LINK generation.
    uint8_t bus;      ///< USB bus number at enumeration.
    uint8_t address;  ///< USB device address on @ref bus; changes on replug.
    /**
     * @brief The ST-LINK's USB serial number (iSerialNumber), as printable
     *        text, or empty when it could not be read.
     *
     * @details The same string STM32_Programmer_CLI lists as `ST-LINK SN`
     *          and accepts as `sn=`, so a base found here can be named to the
     *          programmer. Old ST-LINK/V2 firmware reports a binary serial,
     *          which the CLI shows hex-encoded; that is left empty rather than
     *          guessed. Not used by Attach().
     */
    std::string serial;
};

struct LinkAdaptStats
{
    bool enabled = TAGCORE_ENABLE_INSTRUMENTATION != 0;
    uint64_t usb_in_calls = 0;
    uint64_t usb_out_calls = 0;
    uint64_t usb_in_bytes = 0;
    uint64_t usb_out_bytes = 0;
    uint64_t usb_in_ns = 0;
    uint64_t usb_out_ns = 0;
    uint64_t cmd_calls = 0;
    uint64_t cmd_ns = 0;
    uint64_t read_mem_calls = 0;
    uint64_t read_mem_bytes = 0;
    uint64_t read_mem_ns = 0;
    uint64_t write_mem_calls = 0;
    uint64_t write_mem_bytes = 0;
    uint64_t write_mem_ns = 0;
    uint64_t read_debug_calls = 0;
    uint64_t read_debug_ns = 0;
    uint64_t write_debug_calls = 0;
    uint64_t write_debug_ns = 0;
    uint64_t rw_status_calls = 0;
    uint64_t rw_status_ns = 0;
};

class LinkAdapt
{
public:

    LinkAdapt();
    ~LinkAdapt();

    bool Available(std::vector<UsbDev> &usbdevs);
    bool Attach(UsbDev usbdev = UsbDev());
    bool Attach(bool assertReset, UsbDev usbdev= UsbDev());
    void Detach();
    bool IsAttached();
    void ResetLinkStats();
    LinkAdaptStats GetLinkStats() const;
   
protected:

    bool Voltage(float &voltage);
    bool ReadMem32(unsigned int addr, uint8_t *buf, uint16_t count);
    bool WriteMem32(unsigned int addr, uint8_t *buf, uint16_t count);
    bool ReadDebug32(unsigned int addr, uint32_t *rval);
    bool WriteDebug32(unsigned int addr, uint32_t val);
    bool WriteReset(bool active);
    bool AssertReset(bool high);

private:
   class LinkReq;
    unsigned char ep_out;
    unsigned char ep_in;
    libusb_device_handle *handle = nullptr;
    libusb_context *ctx = nullptr;
    LinkAdaptStats stats;

    bool usb_xfer(bool In, unsigned char *buf, int length);
    bool cmd_eval(LinkReq &req, bool ReadnWrite);
    bool current_mode(uint16_t &mode);
    bool enter_debug_mode();
    bool exit_debug_mode();
    bool exit_dfu_mode();
    bool get_rw_status(uint16_t &status);
};
#endif
