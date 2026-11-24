/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ISP55E0_TRANSFER_UART_H
#define ISP55E0_TRANSFER_UART_H

#include <stddef.h>
#include "transfer.h"

struct uart_io {
	int (*read_all)(struct transport *, void *, size_t);
	int (*write_all)(struct transport *, const void *, size_t);
};

int uart_exchange(struct transport *transport, const struct uart_io *io,
		  const void *req, int req_len, void *resp, int resp_len);

#endif
