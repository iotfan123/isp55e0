/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#include "transfer.h"

#define EP_OUT 0x02
#define EP_IN 0x82
#define USB_PACKET_SIZE 64

struct usb_transport {
	libusb_context *library;
	libusb_device_handle *handle;
	bool claimed;
};

static int usb_error(struct transport *t, const char *operation, int code)
{
	snprintf(t->error, sizeof(t->error), "%s: %s", operation,
		libusb_error_name(code));
	return -EIO;
}

static void close_usb(struct transport *t)
{
	struct usb_transport *usb = t->context;

	if (usb->handle) {
		if (usb->claimed)
			libusb_release_interface(usb->handle, 0);
		libusb_close(usb->handle);
	}
	if (usb->library)
		libusb_exit(usb->library);
	free(usb);
	t->context = NULL;
}

static int open_usb(struct transport *t, const char *port)
{
	struct usb_transport *usb = calloc(1, sizeof(*usb));
	int ret;

	(void)port;
	if (!usb)
		return -ENOMEM;
	t->context = usb;
	ret = libusb_init(&usb->library);
	if (ret) {
		/* libusb_init does not provide a usable context on failure. */
		free(usb);
		t->context = NULL;
		return usb_error(t, "Initialize libusb", ret);
	}
	usb->handle = libusb_open_device_with_vid_pid(usb->library, 0x4348, 0x55e0);
	if (!usb->handle)
		usb->handle = libusb_open_device_with_vid_pid(usb->library, 0x1a86, 0x55e0);
	if (!usb->handle) {
		snprintf(t->error, sizeof(t->error), "No CH5xx devices found in ISP mode (check driver/permissions)");
		close_usb(t);
		return -ENODEV;
	}
#if !defined(_WIN32)
	ret = libusb_set_auto_detach_kernel_driver(usb->handle, 1);
	if (ret && ret != LIBUSB_ERROR_NOT_SUPPORTED) {
		usb_error(t, "Enable kernel driver detach", ret);
		close_usb(t);
		return -EIO;
	}
#endif
	ret = libusb_claim_interface(usb->handle, 0);
	if (ret) {
		usb_error(t, "Claim USB interface", ret);
		close_usb(t);
		return -EIO;
	}
	usb->claimed = true;
	return 0;
}

static int exchange(struct transport *t, const void *req, int req_len,
		    void *resp, int resp_len)
{
	struct usb_transport *usb = t->context;
	unsigned char packet[USB_PACKET_SIZE];
	int len, ret;

	if (resp_len <= 0 || (size_t)resp_len > sizeof(packet))
		return -EINVAL;
	ret = libusb_bulk_transfer(usb->handle, EP_OUT, (unsigned char *)req,
		req_len, &len, TRANSFER_TIMEOUT_MS);
	if (ret)
		return usb_error(t, "USB write", ret);
	if (len != req_len) {
		snprintf(t->error, sizeof(t->error), "Short USB write: %d/%d", len, req_len);
		return -EIO;
	}
	/* Read a complete USB packet even for a short logical ISP response. */
	ret = libusb_bulk_transfer(usb->handle, EP_IN, packet, sizeof(packet),
		&len, TRANSFER_TIMEOUT_MS);
	if (ret)
		return usb_error(t, "USB read", ret);
	if (len < resp_len) {
		snprintf(t->error, sizeof(t->error), "Short USB response: %d/%d", len, resp_len);
		return -EIO;
	}
	memcpy(resp, packet, resp_len);
	return 0;
}

const struct transport_ops transfer_usb_libusb_ops = {
	.name = "libusb",
	.open = open_usb,
	.exchange = exchange,
	.close = close_usb,
};
