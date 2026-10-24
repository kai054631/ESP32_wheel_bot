#pragma once

// Wi-Fi (station) + one-client TCP server on port 3333.
// None of these functions block the 20 ms control loop.

// Start Wi-Fi and the TCP server task. Call once, early in app_main.
void net_init(void);

// printf to the connected TCP client. Returns 0 if nobody is connected or the
// queue is full (the line is dropped on purpose, never waits).
int net_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// One byte received from the client, or -1 if nothing is waiting.
int net_getc(void);

// 1 if a TCP client is connected, else 0 (use it as a fail-safe).
int net_connected(void);
