/* include/linux/usb/mpsse.h */
#ifndef __LINUX_USB_MPSSE_H
#define __LINUX_USB_MPSSE_H

#include <linux/device.h>
#include <linux/types.h>

/* MPSSE Commands (Scavenged from ft232h-intf.h) */
#define MPSSE_TX_BYTES_RE_MSB		0x10
#define MPSSE_TX_BYTES_FE_MSB		0x11
#define MPSSE_RX_BYTES_RE_MSB		0x20
#define MPSSE_RX_BYTES_FE_MSB		0x24
#define MPSSE_TXF_RXR_BYTES_MSB		0x31
#define MPSSE_TXR_RXF_BYTES_MSB		0x34
#define MPSSE_TX_BYTES_RE_LSB		0x18
#define MPSSE_TX_BYTES_FE_LSB		0x19
#define MPSSE_RX_BYTES_RE_LSB		0x28
#define MPSSE_RX_BYTES_FE_LSB		0x2C
#define MPSSE_TXF_RXR_BYTES_LSB		0x39
#define MPSSE_TXR_RXF_BYTES_LSB		0x3C
#define MPSSE_LOOPBACK_ON		0x84
#define MPSSE_LOOPBACK_OFF		0x85
#define MPSSE_TCK_DIVISOR		0x86
#define MPSSE_SEND_IMMEDIATE		0x87
#define MPSSE_DIS_DIV_5			0x8A
#define MPSSE_EN_DIV_5			0x8B
#define MPSSE_DIS_3_PHASE		0x8D
#define MPSSE_DIS_ADAPTIVE		0x97
#define MPSSE_SET_BITS_LOW		0x80
#define MPSSE_GET_BITS_LOW		0x81
#define MPSSE_SET_BITS_HIGH		0x82
#define MPSSE_GET_BITS_HIGH		0x83

/* Opaque handle to the MPSSE provider */
struct mpsse_device;

/* Discovery */
struct mpsse_device *mpsse_get_by_phandle(struct device *consumer,
					  const char *phandle_name);
void mpsse_put(struct mpsse_device *mpsse);

/* Locking */
void mpsse_lock(struct mpsse_device *mpsse);
void mpsse_unlock(struct mpsse_device *mpsse);

/* Protocol Operations */
int mpsse_write_data(struct mpsse_device *mpsse, u8 *data, size_t len);
int mpsse_read_data(struct mpsse_device *mpsse, u8 *data, size_t len);
int mpsse_set_clock(struct mpsse_device *mpsse, u32 freq_hz);
int mpsse_flush(struct mpsse_device *mpsse);

#endif /* __LINUX_USB_MPSSE_H */
