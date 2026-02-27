/*
 * Platform abstraction layer - Terminal operations
 *
 * Provides unified terminal detection API across Windows and POSIX systems.
 * Wraps GetConsoleScreenBufferInfo/ioctl for terminal width detection.
 *
 * This header is included by platform.h and should not be included directly.
 */

#ifndef PLATFORM_TERMINAL_H
#define PLATFORM_TERMINAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get terminal width in columns.
 * @return Terminal width in columns, or 80 if detection fails.
 *
 * Windows: Wraps GetConsoleScreenBufferInfo to query console dimensions.
 * POSIX: Wraps ioctl(STDOUT_FILENO, TIOCGWINSZ) to query terminal size.
 *
 * Returns 80 as a safe default if:
 *   - Terminal size detection fails
 *   - Output is redirected to a pipe or file
 *   - Running in a non-interactive environment
 *
 * The returned value is suitable for formatting output to fit within
 * the terminal viewport. Typical usage:
 *   int width = platform_terminal_width();
 *   printf("%-*s\n", width - 4, text);  // Leave 4 chars margin
 */
int platform_terminal_width(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_TERMINAL_H */
