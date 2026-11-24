/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "transfer.h"

/* Add future backends (for example transfer_usb_wch_ops) here. */
static const struct transport_ops *const backends[] = {
	&transfer_uart_ops,
#if !defined(WITHOUT_USB) || WITHOUT_USB == 0
	&transfer_usb_libusb_ops,
#endif
};

static void trace(const char *name, const void *data, int len)
{
	const unsigned char *bytes = data;
	int i;

	printf("Dump - %s\n", name);
	for (i = 0; i < len; i++) {
		if (i && i % 16 == 0)
			printf("\n");
		printf("%02x ", bytes[i]);
	}
	printf("\n");
}

int transport_open(struct transport *t, const char *backend,
		   const char *port, bool debug)
{
	size_t i;
	int ret;

	memset(t, 0, sizeof(*t));
	t->debug = debug;
	for (i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
		if (strcmp(backend, backends[i]->name))
			continue;
		t->ops = backends[i];
		ret = t->ops->open(t, port);
		if (ret) {
			if (!t->error[0])
				snprintf(t->error, sizeof(t->error), "%s: %s",
					backend, strerror(-ret));
			transport_close(t);
			return ret;
		}
		return 0;
	}
	snprintf(t->error, sizeof(t->error), "Backend '%s' is not built in", backend);
	return -ENODEV;
}

int transport_exchange(struct transport *t, const void *req, int req_len,
		       void *resp, int resp_len)
{
	int ret;

	if (!t->ops || !t->context)
		return -ENODEV;
	if (!req || !resp || req_len <= 0 || resp_len <= 0)
		return -EINVAL;
	t->error[0] = '\0';
	if (t->debug)
		trace("request", req, req_len);
	ret = t->ops->exchange(t, req, req_len, resp, resp_len);
	if (!ret && t->debug)
		trace("response", resp, resp_len);
	return ret;
}

bool transport_supports_baudrate(const struct transport *t, uint32_t baudrate)
{
	return t->ops && t->ops->supports_baudrate &&
		t->ops->supports_baudrate(baudrate);
}

int transport_set_baudrate(struct transport *t, uint32_t baudrate)
{
	if (!t->ops || !t->ops->set_baudrate)
		return -ENOTSUP;
	return t->ops->set_baudrate(t, baudrate);
}

void transport_close(struct transport *t)
{
	if (t->ops && t->context)
		t->ops->close(t);
	t->context = NULL;
	t->ops = NULL;
}
