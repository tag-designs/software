#ifndef LINK_ADAPT_H
#define LINK_ADAPT_H
#include <stdint.h>
#include <vector>
struct libusb_device_handle;
struct libusb_context;


struct UsbDev
{
    UsbDev(): vid(0),pid(0), bus(0), address(0) { } 
    uint16_t vid;
    uint16_t pid;
    uint8_t bus;
    uint8_t address; 
};

class LinkAdapt
{
public:

    LinkAdapt();
    ~LinkAdapt();

    bool Available(std::vector<UsbDev> &usbdevs);
    bool Attach(UsbDev usbdev = UsbDev());
    void Detach();
    bool IsAttached();
   
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

    bool usb_xfer(bool In, unsigned char *buf, int length);
    bool cmd_eval(LinkReq &req, bool ReadnWrite);
    bool current_mode(uint16_t &mode);
    bool enter_debug_mode();
    bool exit_debug_mode();
    bool exit_dfu_mode();
    bool get_rw_status(uint16_t &status);
};
#endif