/*
 * Platform abstraction layer - Terminal operations implementation
 *
 * Cross-platform terminal width detection for responsive output formatting.
 * Provides unified API wrapping Windows GetConsoleScreenBufferInfo and POSIX ioctl.
 */

#include "platform_terminal.h"
#include "platform_types.h"

#if PLATFORM_WINDOWS
    #include <windows.h>
    #include <io.h>
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

/**
 * Get terminal width in columns.
 *
 * Windows implementation:
 *   - Uses GetConsoleScreenBufferInfo to query console buffer dimensions
 *   - Checks if stdout is a console (not redirected)
 *   - Calculates width from Right - Left + 1
 *   - Returns 80 if not a console or query fails
 *
 * POSIX implementation:
 *   - Uses ioctl(STDOUT_FILENO, TIOCGWINSZ) to query window size
 *   - Checks if stdout is a terminal using isatty()
 *   - Returns ws_col from winsize structure
 *   - Returns 80 if not a terminal or query fails
 *
 * The 80-column default is chosen for compatibility with:
 *   - Legacy terminal standards
 *   - Redirected output (pipes, files)
 *   - Non-interactive environments (CI, cron jobs)
 */
int platform_terminal_width(void)
{
#if PLATFORM_WINDOWS
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Check if stdout is a console (not redirected to file/pipe) */
    if (hConsole == INVALID_HANDLE_VALUE) {
        return 80;  /* Fallback for invalid handle */
    }

    /* Get console screen buffer info */
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        /* Calculate width from window coordinates */
        int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (width > 0) {
            return width;
        }
    }

    /* Fallback if query failed or width is invalid */
    return 80;

#else
    struct winsize w;

    /* Check if stdout is a terminal (not redirected to file/pipe) */
    if (!isatty(STDOUT_FILENO)) {
        return 80;  /* Not a terminal, use default */
    }

    /* Query terminal window size */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }

    /* Fallback if ioctl failed or returned invalid width */
    return 80;
#endif
}
