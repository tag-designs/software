/**************************************************************************
*      Copyright 2018  Geoffrey Brown                                     *
*                                                                         *
* Licensed under the Apache License, Version 2.0 (the "License");         *
* you may not use this file except in compliance with the License.        *
* You may obtain a copy of the License at                                 *
*                                                                         *
*     http://www.apache.org/licenses/LICENSE-2.0                          *
*                                                                         *
* Unless required by applicable law or agreed to in writing, software     *
* distributed under the License is distributed on an "AS IS" BASIS,       *
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
* See the License for the specific language governing permissions and     *
* limitations under the License.                                          *
**************************************************************************/

#include "hal.h"
#include "usbcfg.h"
#include "chprintf.h"
#include <stdarg.h>
#include <stddef.h>

SerialUSBDriver SDU1;

#define STLINK_INTERFACE              0
#define CDC_CONTROL_INTERFACE         1
#define CDC_DATA_INTERFACE            2
#define USB_INTERFACE_COUNT           3

#define STLINK_BULK_IN_EP             BULK_IN_EP
#define STLINK_BULK_OUT_EP            BULK_OUT_EP
#define STLINK_TRACE_IN_EP            BULK_IN_TRACE_EP
#define CDC_BULK_EP                   4
#define CDC_INTERRUPT_EP              5

#define CONFIG_DESC_SIZE              105

static const uint8_t vcom_device_descriptor_data[18] = {
    USB_DESC_DEVICE(0x0200,   /* bcdUSB (2.0).                    */
                    0xEF,     /* bDeviceClass (Miscellaneous).    */
                    0x02,     /* bDeviceSubClass (Common Class).  */
                    0x01,     /* bDeviceProtocol (IAD).           */
                    0x40,     /* bMaxPacketSize.                  */
                    USBD_VID, /* idVendor (ST).                   */
                    USBD_PID, /* idProduct.                       */
                    0x0200,   /* bcdDevice.                       */
                    1,        /* iManufacturer.                   */
                    2,        /* iProduct.                        */
                    3,        /* iSerialNumber.                   */
                    1)        /* bNumConfigurations.              */
};

static const USBDescriptor vcom_device_descriptor = {
    sizeof vcom_device_descriptor_data,
    vcom_device_descriptor_data};

static const uint8_t vcom_configuration_descriptor_data[CONFIG_DESC_SIZE] = {
    USB_DESC_CONFIGURATION(CONFIG_DESC_SIZE,
                           USB_INTERFACE_COUNT,
                           0x01,
                           0,
                           0x80,
                           50),

    USB_DESC_INTERFACE(STLINK_INTERFACE,
                       0x00,
                       0x03,
                       0xFF,
                       0xFF,
                       0xFF,
                       4),
    USB_DESC_ENDPOINT(STLINK_BULK_IN_EP | 0x80,
                      0x02,
                      0x0040,
                      0x00),
    USB_DESC_ENDPOINT(STLINK_BULK_OUT_EP,
                      0x02,
                      0x0040,
                      0x00),
    USB_DESC_ENDPOINT(STLINK_TRACE_IN_EP | 0x80,
                      0x02,
                      0x0040,
                      0x00),

    USB_DESC_INTERFACE_ASSOCIATION(CDC_CONTROL_INTERFACE,
                                   0x02,
                                   0x02,
                                   0x02,
                                   0x01,
                                   5),
    USB_DESC_INTERFACE(CDC_CONTROL_INTERFACE,
                       0x00,
                       0x01,
                       0x02,
                       0x02,
                       0x01,
                       5),
    USB_DESC_BYTE(5),
    USB_DESC_BYTE(0x24),
    USB_DESC_BYTE(0x00),
    USB_DESC_BCD(0x0110),
    USB_DESC_BYTE(5),
    USB_DESC_BYTE(0x24),
    USB_DESC_BYTE(0x01),
    USB_DESC_BYTE(0x00),
    USB_DESC_BYTE(CDC_DATA_INTERFACE),
    USB_DESC_BYTE(4),
    USB_DESC_BYTE(0x24),
    USB_DESC_BYTE(0x02),
    USB_DESC_BYTE(0x02),
    USB_DESC_BYTE(5),
    USB_DESC_BYTE(0x24),
    USB_DESC_BYTE(0x06),
    USB_DESC_BYTE(CDC_CONTROL_INTERFACE),
    USB_DESC_BYTE(CDC_DATA_INTERFACE),
    USB_DESC_ENDPOINT(CDC_INTERRUPT_EP | 0x80,
                      0x03,
                      0x0010,
                      0xFF),
    USB_DESC_INTERFACE(CDC_DATA_INTERFACE,
                       0x00,
                       0x02,
                       0x0A,
                       0x00,
                       0x00,
                       5),
    USB_DESC_ENDPOINT(CDC_BULK_EP,
                      0x02,
                      0x0040,
                      0x00),
    USB_DESC_ENDPOINT(CDC_BULK_EP | 0x80,
                      0x02,
                      0x0040,
                      0x00),
};

static const USBDescriptor vcom_configuration_descriptor = {
    sizeof vcom_configuration_descriptor_data,
    vcom_configuration_descriptor_data};

static const uint8_t vcom_string0[] = {
    USB_DESC_BYTE(4),
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING),
    USB_DESC_WORD(0x0409)
};

static const uint8_t vcom_string1[38] = {
    USB_DESC_BYTE(38),
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING),
    'S', 0, 'T', 0, 'M', 0, 'i', 0, 'c', 0, 'r', 0, 'o', 0, 'e', 0,
    'l', 0, 'e', 0, 'c', 0, 't', 0, 'r', 0, 'o', 0,
    'n', 0, 'i', 0, 'c', 0, 's', 0};

static const uint8_t vcom_string2[14] = {
    USB_DESC_BYTE(14),
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING),
    'S', 0, 'T', 0, 'L', 0, 'i', 0, 'n', 0, 'k', 0};

static uint8_t vcom_string3[50] __attribute__((aligned(2))) = {
    USB_DESC_BYTE(50),
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING),
    0};

static const uint8_t vcom_string4[16] = {
    USB_DESC_BYTE(16),
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING),
    'S', 0, 'T', 0, ' ', 0, 'L', 0, 'i', 0, 'n', 0, 'k', 0};

static const uint8_t vcom_string5[22] = {
    USB_DESC_BYTE(22),
    USB_DESC_BYTE(USB_DESCRIPTOR_STRING),
    'D', 0, 'e', 0, 'b', 0, 'u', 0, 'g', 0, ' ', 0,
    'U', 0, 'A', 0, 'R', 0, 'T', 0};

static const USBDescriptor vcom_strings[] = {
    {sizeof vcom_string0, vcom_string0},
    {sizeof vcom_string1, vcom_string1},
    {sizeof vcom_string2, vcom_string2},
    {sizeof vcom_string3, vcom_string3},
    {sizeof vcom_string4, vcom_string4},
    {sizeof vcom_string5, vcom_string5},
};

static const USBDescriptor *get_descriptor(USBDriver *usbp,
                                           uint8_t dtype,
                                           uint8_t dindex,
                                           uint16_t lang)
{
  (void)usbp;
  (void)lang;
  switch (dtype)
  {
  case USB_DESCRIPTOR_DEVICE:
    return &vcom_device_descriptor;
  case USB_DESCRIPTOR_CONFIGURATION:
    return &vcom_configuration_descriptor;
  case USB_DESCRIPTOR_STRING:
    if (dindex == 3)
    {
      static const char *HEX = "0123456789ABCDEF";
      uint8_t *str = (uint8_t *)&vcom_string3[49];
      for (int i = 0; i < 3; i++)
      {
        uint32_t data = ((uint32_t *)UID_BASE)[2 - i];
        for (int j = 0; j < 8; j++)
        {
          *str-- = 0;
          *str-- = HEX[data & 15];
          data = data >> 4;
        }
      }
    }
    if (dindex < 6)
      return &vcom_strings[dindex];
  }
  return NULL;
}

static USBInEndpointState ep1instate;
static const USBEndpointConfig ep1config = {
    USB_EP_MODE_TYPE_BULK,
    NULL,
    NULL,
    NULL,
    0x0040,
    0,
    &ep1instate,
    NULL,
    1,
    NULL};

static USBOutEndpointState ep2outstate;
static const USBEndpointConfig ep2config = {
    USB_EP_MODE_TYPE_BULK,
    NULL,
    NULL,
    NULL,
    0,
    0x0040,
    NULL,
    &ep2outstate,
    1,
    NULL};

static USBInEndpointState ep3instate;
static const USBEndpointConfig ep3config = {
    USB_EP_MODE_TYPE_BULK,
    NULL,
    NULL,
    NULL,
    0x0040,
    0,
    &ep3instate,
    NULL,
    1,
    NULL};

static USBInEndpointState ep4instate;
static USBOutEndpointState ep4outstate;
static const USBEndpointConfig ep4config = {
    USB_EP_MODE_TYPE_BULK,
    NULL,
    sduDataTransmitted,
    sduDataReceived,
    0x0040,
    0x0040,
    &ep4instate,
    &ep4outstate,
    2,
    NULL};

static USBInEndpointState ep5instate;
static const USBEndpointConfig ep5config = {
    USB_EP_MODE_TYPE_INTR,
    NULL,
    sduInterruptTransmitted,
    NULL,
    0x0010,
    0,
    &ep5instate,
    NULL,
    1,
    NULL};

static void usb_event(USBDriver *usbp, usbevent_t event)
{
  switch (event)
  {
  case USB_EVENT_RESET:
    chSysLockFromISR();
    usbDisableEndpointsI(usbp);
    sduSuspendHookI(&SDU1);
    chSysUnlockFromISR();
    return;
  case USB_EVENT_ADDRESS:
    return;
  case USB_EVENT_CONFIGURED:
    chSysLockFromISR();
    usbInitEndpointI(usbp, STLINK_BULK_IN_EP, &ep1config);
    usbInitEndpointI(usbp, STLINK_BULK_OUT_EP, &ep2config);
    usbInitEndpointI(usbp, STLINK_TRACE_IN_EP, &ep3config);
    usbInitEndpointI(usbp, CDC_BULK_EP, &ep4config);
    usbInitEndpointI(usbp, CDC_INTERRUPT_EP, &ep5config);
    sduConfigureHookI(&SDU1);
    chSysUnlockFromISR();
    return;
  case USB_EVENT_UNCONFIGURED:
    chSysLockFromISR();
    usbDisableEndpointsI(usbp);
    sduSuspendHookI(&SDU1);
    chSysUnlockFromISR();
    return;
  case USB_EVENT_SUSPEND:
    chSysLockFromISR();
    sduSuspendHookI(&SDU1);
    chSysUnlockFromISR();
    return;
  case USB_EVENT_WAKEUP:
    chSysLockFromISR();
    sduWakeupHookI(&SDU1);
    chSysUnlockFromISR();
    return;
  case USB_EVENT_STALLED:
    return;
  }
}

static void sof_handler(USBDriver *usbp)
{
  (void)usbp;

  osalSysLockFromISR();
  sduSOFHookI(&SDU1);
  osalSysUnlockFromISR();
}

const USBConfig usbcfg = {
    usb_event,
    get_descriptor,
    sduRequestsHook,
    sof_handler,
};

const SerialUSBConfig serusbcfg = {
    &USBD1,
    CDC_BULK_EP,
    CDC_BULK_EP,
    CDC_INTERRUPT_EP,
};

msg_t BULK_Receive(uint8_t *Buf, uint16_t len)
{
  return usbReceive(&USBD1, BULK_OUT_EP, Buf, len);
}

msg_t BULK_Transmit(uint8_t *Buf, uint16_t len)
{
  if (usbTransmit(&USBD1, BULK_IN_EP, Buf, len))
    return 0;
  else
    return len;
}

msg_t BULK_Trace_Transmit(uint8_t *Buf, uint16_t len)
{
  if (usbTransmit(&USBD1, BULK_IN_TRACE_EP, Buf, len))
    return 0;
  else
    return len;
}

void debug_printf(const char *fmt, ...)
{
  char buf[160];
  va_list ap;
  int n;

  if ((SDU1.state != SDU_READY) || (USBD1.state != USB_ACTIVE))
    return;

  va_start(ap, fmt);
  n = chvsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (n <= 0)
    return;
  if ((size_t)n >= sizeof(buf))
    n = sizeof(buf) - 1;

  (void)chnWriteTimeout(&SDU1, (const uint8_t *)buf, (size_t)n,
                        TIME_IMMEDIATE);
}
