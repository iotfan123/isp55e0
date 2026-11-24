/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ISP55E0_TRANSFER_H
#define ISP55E0_TRANSFER_H

#include <stdbool.h>
#include <stdint.h>

#define TRANSFER_TIMEOUT_MS 5000
#define UART_TIMEOUT_MS 1000
#define UART_DEFAULT_BAUD 115200

struct transport;

/* Backends return 0 on success, a negative errno value on failure.
 * Set error to a useful diagnostic before returning a failure.
 * open stores its private allocation in context; close releases it and sets
 * context to NULL. On open failure, either clean up or leave context valid
 * for close. USB backends may leave baudrate callbacks NULL.
 * exchange receives/sends ISP payloads, without UART framing, and must fill
 * all resp_len bytes on success (short responses must return an error).
 */
struct transport_ops {
	const char *name;
	int (*open)(struct transport *transport, const char *port);
	int (*exchange)(struct transport *transport, const void *req, int req_len,
			void *resp, int resp_len);
	bool (*supports_baudrate)(uint32_t baudrate);
	int (*set_baudrate)(struct transport *transport, uint32_t baudrate);
	void (*close)(struct transport *transport);
};

struct transport {
	const struct transport_ops *ops;
	void *context;
	bool debug;
	char error[256];
};

/* Exactly one platform UART implementation is linked by the Makefile. */
extern const struct transport_ops transfer_uart_ops;
#if !defined(WITHOUT_USB) || WITHOUT_USB == 0
extern const struct transport_ops transfer_usb_libusb_ops;
#endif

int transport_open(struct transport *transport, const char *backend,
		   const char *port, bool debug);
int transport_exchange(struct transport *transport, const void *req, int req_len,
		       void *resp, int resp_len);
bool transport_supports_baudrate(const struct transport *transport, uint32_t baudrate);
int transport_set_baudrate(struct transport *transport, uint32_t baudrate);
void transport_close(struct transport *transport);

#endif
