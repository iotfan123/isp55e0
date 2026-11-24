/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transfer_uart.h"

static unsigned char checksum(const void *data, int len)
{
	const unsigned char *bytes = data;
	unsigned char sum = 0;
	int i;

	for (i = 0; i < len; i++)
		sum += bytes[i];
	return sum;
}

int uart_exchange(struct transport *t, const struct uart_io *io,
		  const void *req, int req_len, void *resp, int resp_len)
{
	unsigned char prefix[2], crc;
	unsigned char *frame;
	int ret;

	if (req_len <= 0 || resp_len <= 0)
		return -EINVAL;
	frame = malloc((size_t)req_len + 3);
	if (!frame)
		return -ENOMEM;
	frame[0] = 0x57;
	frame[1] = 0xab;
	memcpy(frame + 2, req, req_len);
	frame[req_len + 2] = checksum(req, req_len);
	ret = io->write_all(t, frame, (size_t)req_len + 3);
	free(frame);
	if (ret)
		return ret;
	ret = io->read_all(t, prefix, sizeof(prefix));
	if (ret)
		return ret;
	if (prefix[0] != 0x55 || prefix[1] != 0xaa) {
		snprintf(t->error, sizeof(t->error), "Invalid UART response prefix");
		return -EIO;
	}
	ret = io->read_all(t, resp, resp_len);
	if (ret)
		return ret;
	ret = io->read_all(t, &crc, sizeof(crc));
	if (ret)
		return ret;
	if (crc != checksum(resp, resp_len)) {
		snprintf(t->error, sizeof(t->error), "Invalid UART response checksum");
		return -EIO;
	}
	return 0;
}
