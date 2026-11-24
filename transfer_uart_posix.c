/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/ioctl.h>
#endif

#include "transfer.h"
#include "transfer_uart.h"

/* Arbitrary (non-standard) baud rates on Linux use the BOTHER extension
 * with the termios2 ioctls. The kernel UAPI <asm/termbits.h> cannot be
 * mixed with the libc <termios.h> (duplicate 'struct termios'), so the
 * needed UAPI pieces are declared here. Layout and ioctl numbers follow
 * the asm-generic encoding used by the architectures listed below; on
 * other architectures only the fixed rate table above is available.
 * Kernel NCCS is 19 there (the libc one is 32) and struct termios2 is
 * 44 bytes, hence the TCGETS2/TCSETS2 values. */
#if defined(__linux__) && \
	(defined(__i386__) || defined(__x86_64__) || defined(__arm__) || \
	 defined(__aarch64__) || defined(__riscv) || defined(__s390x__))
#define ISP_UART_CBAUD     0x100fu
#define ISP_UART_BOTHER    0x1000u
#define ISP_UART_TCGETS2   0x802c542au
#define ISP_UART_TCSETS2   0x402c542bu

struct uart_termios2 {
	uint32_t c_iflag;
	uint32_t c_oflag;
	uint32_t c_cflag;
	uint32_t c_lflag;
	uint8_t c_line;
	uint8_t c_cc[19];
	uint32_t c_ispeed;
	uint32_t c_ospeed;
};
#endif

struct uart_posix_context {
	int fd;
};

static int uart_error(struct transport *t, int error, const char *operation)
{
	snprintf(t->error, sizeof(t->error), "%s: %s", operation,
		 strerror(error));
	return -error;
}

static bool uart_baudrate(uint32_t baudrate, speed_t *speed)
{
	switch (baudrate) {
#ifdef B115200
	case 115200: *speed = B115200; return true;
#endif
#ifdef B230400
	case 230400: *speed = B230400; return true;
#endif
#ifdef B460800
	case 460800: *speed = B460800; return true;
#endif
#ifdef B500000
	case 500000: *speed = B500000; return true;
#endif
#ifdef B921600
	case 921600: *speed = B921600; return true;
#endif
#ifdef B1000000
	case 1000000: *speed = B1000000; return true;
#endif
#ifdef B2000000
	case 2000000: *speed = B2000000; return true;
#endif
	default:
		return false;
	}
}

static bool uart_supports_baudrate(uint32_t baudrate)
{
	if (!baudrate)
		return false;
	/* Non-table rates: BOTHER on Linux, plain numeric speeds on Darwin
	 * (its speed_t values are the rates themselves). */
#if defined(ISP_UART_BOTHER) || defined(__APPLE__)
	return true;
#else
	{
		speed_t speed;

		return uart_baudrate(baudrate, &speed);
	}
#endif
}

static int uart_open(struct transport *t, const char *port)
{
	struct uart_posix_context *ctx;
	struct termios options;
	const char *operation;
	speed_t speed;
	int error;

	if (t->context)
		return uart_error(t, EBUSY, "UART already open");
	if (!port || !*port)
		return uart_error(t, EINVAL, "UART port is required");
	if (!uart_baudrate(UART_DEFAULT_BAUD, &speed))
		return uart_error(t, EINVAL, "UART default baudrate unsupported");

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return uart_error(t, ENOMEM, "Allocate UART context");

	ctx->fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (ctx->fd < 0) {
		error = errno;
		free(ctx);
		snprintf(t->error, sizeof(t->error), "Open UART '%s': %s",
			 port, strerror(error));
		return -error;
	}

	operation = "UART tcgetattr";
	if (tcgetattr(ctx->fd, &options) < 0)
		goto fail;

	/* Raw 8N1, with both software and hardware flow control disabled. */
	options.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
			     IGNCR | ICRNL | INPCK | IXON | IXOFF | IXANY);
	options.c_oflag &= ~OPOST;
	options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
	options.c_cflag |= CLOCAL | CREAD | CS8;
#ifdef CRTSCTS
	options.c_cflag &= ~CRTSCTS;
#endif
#ifdef CNEW_RTSCTS
	options.c_cflag &= ~CNEW_RTSCTS;
#endif
#ifdef CCTS_OFLOW
	options.c_cflag &= ~CCTS_OFLOW;
#endif
#ifdef CRTS_IFLOW
	options.c_cflag &= ~CRTS_IFLOW;
#endif
#ifdef CDTR_IFLOW
	options.c_cflag &= ~CDTR_IFLOW;
#endif
#ifdef CDSR_OFLOW
	options.c_cflag &= ~CDSR_OFLOW;
#endif
#ifdef CCAR_OFLOW
	options.c_cflag &= ~CCAR_OFLOW;
#endif
#ifdef MDMBUF
	options.c_cflag &= ~MDMBUF;
#endif
	/* poll supplies the timeout; neither read nor write may block. */
	options.c_cc[VMIN] = 0;
	options.c_cc[VTIME] = 0;

	operation = "UART cfsetispeed";
	if (cfsetispeed(&options, speed) < 0)
		goto fail;
	operation = "UART cfsetospeed";
	if (cfsetospeed(&options, speed) < 0)
		goto fail;
	operation = "UART tcsetattr";
	if (tcsetattr(ctx->fd, TCSANOW, &options) < 0)
		goto fail;

	t->context = ctx;
	return 0;

fail:
	error = errno;
	close(ctx->fd);
	free(ctx);
	return uart_error(t, error, operation);
}

static int uart_set_baudrate(struct transport *t, uint32_t baudrate)
{
	struct uart_posix_context *ctx = t->context;
	struct termios options;
	speed_t speed;

	if (!ctx)
		return uart_error(t, EBADF, "UART is not open");
	if (!baudrate) {
		snprintf(t->error, sizeof(t->error), "UART baudrate must be nonzero");
		return -EINVAL;
	}
	if (uart_baudrate(baudrate, &speed)) {
		if (tcgetattr(ctx->fd, &options) < 0)
			return uart_error(t, errno, "UART tcgetattr");
		if (cfsetispeed(&options, speed) < 0)
			return uart_error(t, errno, "UART cfsetispeed");
		if (cfsetospeed(&options, speed) < 0)
			return uart_error(t, errno, "UART cfsetospeed");
		if (tcsetattr(ctx->fd, TCSANOW, &options) < 0)
			return uart_error(t, errno, "UART tcsetattr");
		return 0;
	}
#ifdef ISP_UART_BOTHER
	{
		struct uart_termios2 tio;

		if (ioctl(ctx->fd, ISP_UART_TCGETS2, &tio) < 0)
			return uart_error(t, errno, "UART TCGETS2");
		tio.c_cflag &= ~ISP_UART_CBAUD;
		tio.c_cflag |= ISP_UART_BOTHER;
		tio.c_ispeed = baudrate;
		tio.c_ospeed = baudrate;
		if (ioctl(ctx->fd, ISP_UART_TCSETS2, &tio) < 0)
			return uart_error(t, errno, "UART TCSETS2 (BOTHER)");
		return 0;
	}
#elif defined(__APPLE__)
	/* Darwin speed_t values are the numeric rates themselves. */
	if (tcgetattr(ctx->fd, &options) < 0)
		return uart_error(t, errno, "UART tcgetattr");
	if (cfsetispeed(&options, (speed_t)baudrate) < 0)
		return uart_error(t, errno, "UART cfsetispeed");
	if (cfsetospeed(&options, (speed_t)baudrate) < 0)
		return uart_error(t, errno, "UART cfsetospeed");
	if (tcsetattr(ctx->fd, TCSANOW, &options) < 0)
		return uart_error(t, errno, "UART tcsetattr");
	return 0;
#else
	snprintf(t->error, sizeof(t->error),
		 "Unsupported UART baudrate on this platform: %lu",
		 (unsigned long)baudrate);
	return -EINVAL;
#endif
}

static int uart_io_all(struct transport *t, void *read_buffer,
		       const void *write_buffer, size_t length, bool writing)
{
	struct uart_posix_context *ctx = t->context;
	const char *operation = writing ? "UART write" : "UART read";
	struct timespec deadline, now;
	struct pollfd pfd;
	size_t done = 0;

	if (!ctx)
		return uart_error(t, EBADF, "UART is not open");
	if (length && (writing ? !write_buffer : !read_buffer))
		return uart_error(t, EINVAL, operation);
	if (!length)
		return 0;
	if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0)
		return uart_error(t, errno, "UART clock_gettime");
	deadline.tv_sec += UART_TIMEOUT_MS / 1000;
	deadline.tv_nsec += (UART_TIMEOUT_MS % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}

	pfd.fd = ctx->fd;
	pfd.events = writing ? POLLOUT : POLLIN;
	while (done < length) {
		int64_t remaining_ns;
		int timeout, ret;
		ssize_t count;

		if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
			return uart_error(t, errno, "UART clock_gettime");
		remaining_ns = ((int64_t)deadline.tv_sec - (int64_t)now.tv_sec)
			       * 1000000000LL + deadline.tv_nsec - now.tv_nsec;
		if (remaining_ns <= 0)
			return uart_error(t, ETIMEDOUT, operation);
		/* Round up so sub-millisecond time left does not become a spin. */
		timeout = (int)((remaining_ns + 999999) / 1000000);
		pfd.revents = 0;
		ret = poll(&pfd, 1, timeout);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return uart_error(t, errno, "UART poll");
		}
		if (!ret)
			return uart_error(t, ETIMEDOUT, operation);
		if (pfd.revents & POLLNVAL)
			return uart_error(t, EBADF, operation);
		/* Consume buffered input even when the peer also hung up. */
		if (!(pfd.revents & pfd.events)) {
			if (pfd.revents & POLLHUP)
				return uart_error(t, EPIPE, "UART disconnected (EOF)");
			if (pfd.revents & POLLERR)
				return uart_error(t, EIO, operation);
			continue;
		}
		if (writing)
			count = write(ctx->fd,
				      (const unsigned char *)write_buffer + done,
				      length - done);
		else
			count = read(ctx->fd, (unsigned char *)read_buffer + done,
				     length - done);
		if (count < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return uart_error(t, errno, operation);
		}
		if (!count)
			return uart_error(t, writing ? EIO : EPIPE,
					  writing ? operation : "UART read EOF");
		done += (size_t)count;
	}
	return 0;
}

static int uart_read_all(struct transport *t, void *buffer, size_t length)
{
	return uart_io_all(t, buffer, NULL, length, false);
}

static int uart_write_all(struct transport *t, const void *buffer, size_t length)
{
	return uart_io_all(t, NULL, buffer, length, true);
}

static int uart_exchange_posix(struct transport *t, const void *req, int req_len,
			       void *resp, int resp_len)
{
	static const struct uart_io io = {
		.read_all = uart_read_all,
		.write_all = uart_write_all,
	};
	int ret;

	if (!t->context)
		return uart_error(t, EBADF, "UART is not open");
	if (!req || !resp || req_len <= 0 || resp_len <= 0)
		return uart_error(t, EINVAL, "Invalid UART exchange buffers");
	t->error[0] = '\0';
	ret = uart_exchange(t, &io, req, req_len, resp, resp_len);
	if (ret < 0 && !t->error[0])
		return uart_error(t, -ret, "UART exchange");
	return ret;
}

static void uart_close(struct transport *t)
{
	struct uart_posix_context *ctx = t->context;

	if (!ctx)
		return;
	/* Do not retry close after EINTR: the fd may already have been closed. */
	close(ctx->fd);
	free(ctx);
	t->context = NULL;
}

const struct transport_ops transfer_uart_ops = {
	.name = "uart",
	.open = uart_open,
	.exchange = uart_exchange_posix,
	.supports_baudrate = uart_supports_baudrate,
	.set_baudrate = uart_set_baudrate,
	.close = uart_close,
};
