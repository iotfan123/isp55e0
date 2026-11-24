/* SPDX-License-Identifier: GPL-3.0-or-later */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transfer_uart.h"

struct windows_uart {
	HANDLE handle;
};

static int windows_error(struct transport *t, const char *operation)
{
	DWORD code = GetLastError();
	char message[160] = "";

	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, code, 0, message, sizeof(message), NULL);
	snprintf(t->error, sizeof(t->error), "%s (Windows error %lu): %s",
		operation, (unsigned long)code, message);
	return -EIO;
}

static bool supports_baudrate(uint32_t baudrate)
{
	/* The Win32 COM API takes any rate; the driver is the validator.
	 * Rates such as 6000000 work when the adapter driver supports them. */
	return baudrate != 0;
}

static int set_baudrate(struct transport *t, uint32_t baudrate)
{
	struct windows_uart *uart = t->context;
	DCB dcb = {0};

	if (!baudrate) {
		snprintf(t->error, sizeof(t->error), "UART baudrate must be nonzero");
		return -EINVAL;
	}
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(uart->handle, &dcb))
		return windows_error(t, "GetCommState");
	dcb.BaudRate = baudrate;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fBinary = TRUE;
	dcb.fParity = FALSE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fTXContinueOnXoff = TRUE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;
	dcb.fErrorChar = FALSE;
	dcb.fNull = FALSE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.fAbortOnError = FALSE;
	if (!SetCommState(uart->handle, &dcb))
		return windows_error(t, "SetCommState");
	return 0;
}

static void close_uart(struct transport *t)
{
	struct windows_uart *uart = t->context;

	CloseHandle(uart->handle);
	free(uart);
	t->context = NULL;
}

static int open_uart(struct transport *t, const char *port)
{
	struct windows_uart *uart;
	char *path;
	int ret;

	if (!port || !*port) {
		snprintf(t->error, sizeof(t->error), "A COM port is required");
		return -EINVAL;
	}
	path = malloc(strlen(port) + 5);
	uart = malloc(sizeof(*uart));
	if (!path || !uart) {
		free(path);
		free(uart);
		return -ENOMEM;
	}
	/* The device namespace also handles COM10 and higher. */
	if (!strncmp(port, "\\\\.\\", 4))
		strcpy(path, port);
	else
		sprintf(path, "\\\\.\\%s", port);
	uart->handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0,
		NULL, OPEN_EXISTING, 0, NULL);
	free(path);
	if (uart->handle == INVALID_HANDLE_VALUE) {
		ret = windows_error(t, "Open serial port");
		free(uart);
		return ret;
	}
	t->context = uart;
	ret = set_baudrate(t, UART_DEFAULT_BAUD);
	if (!ret && !PurgeComm(uart->handle, PURGE_RXCLEAR | PURGE_TXCLEAR))
		ret = windows_error(t, "PurgeComm");
	if (ret)
		close_uart(t);
	return ret;
}

static int io_all(struct transport *t, void *buffer, size_t len, bool writing)
{
	struct windows_uart *uart = t->context;
	unsigned char *bytes = buffer;
	ULONGLONG deadline = GetTickCount64() + UART_TIMEOUT_MS;

	while (len) {
		COMMTIMEOUTS timeouts = {0};
		ULONGLONG now = GetTickCount64();
		DWORD done, remaining;
		BOOL ok;

		if (now >= deadline) {
			snprintf(t->error, sizeof(t->error), "UART %s timed out",
				writing ? "write" : "read");
			return -ETIMEDOUT;
		}
		remaining = (DWORD)(deadline - now);
		timeouts.ReadTotalTimeoutConstant = remaining;
		timeouts.WriteTotalTimeoutConstant = remaining;
		if (!SetCommTimeouts(uart->handle, &timeouts))
			return windows_error(t, "SetCommTimeouts");
		if (writing)
			ok = WriteFile(uart->handle, bytes, (DWORD)len, &done, NULL);
		else
			ok = ReadFile(uart->handle, bytes, (DWORD)len, &done, NULL);
		if (!ok)
			return windows_error(t, writing ? "WriteFile" : "ReadFile");
		if (!done) {
			snprintf(t->error, sizeof(t->error), "UART %s timed out",
				writing ? "write" : "read");
			return -ETIMEDOUT;
		}
		bytes += done;
		len -= done;
	}
	return 0;
}

static int read_all(struct transport *t, void *buffer, size_t len)
{
	return io_all(t, buffer, len, false);
}

static int write_all(struct transport *t, const void *buffer, size_t len)
{
	return io_all(t, (void *)buffer, len, true);
}

static int exchange(struct transport *t, const void *req, int req_len,
		    void *resp, int resp_len)
{
	static const struct uart_io io = { read_all, write_all };

	return uart_exchange(t, &io, req, req_len, resp, resp_len);
}

const struct transport_ops transfer_uart_ops = {
	.name = "uart",
	.open = open_uart,
	.exchange = exchange,
	.supports_baudrate = supports_baudrate,
	.set_baudrate = set_baudrate,
	.close = close_uart,
};
