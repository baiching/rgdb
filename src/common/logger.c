#include "common/logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef LOG_LINE_MAX

#define LOG_LINE_MAX 1024	/* longest line limit */

#endif // !LOG_LINE_MAX

#if defined(_MSC_VER)

static struct tm *localtime_r(const time_t *t, struct tm *out)
{
	return localtime_s(out, t) == 0 ? out : NULL;
}

#endif

void log_msg(const char* file, int line, const char* fmt, ...) {
    char ts[20] = "0000-00-00 00:00:00";
    time_t now = time(NULL);
    struct tm tm_info;
    if (now != (time_t)-1 && localtime_r(&now, &tm_info) != NULL)
        (void)strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm_info);

    /* Build the whole line in one buffer */
    char buf[LOG_LINE_MAX];
    int n = snprintf(buf, sizeof buf, "[TRACE] %s  %s:%d  ", ts, file, line);

    if (n < 0) return;
    size_t used = 0;
    if (n < sizeof(buf))
    {
        used = n;
    }
    else
    {
        used = sizeof(buf) - 1;
    }

    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(buf + used, sizeof buf - used, fmt, ap);
    va_end(ap);

    if (n < 0) {
        if (used >= sizeof(buf) - 1) used = sizeof(buf) - 2;
        buf[used++] = '\n';
    }
    else if ((size_t)n < sizeof(buf) - used) {
        used += (size_t)n;
        buf[used++] = '\n';
    }
    else {
        if (used > sizeof buf - 4) used = sizeof buf - 4;
        memcpy(buf + used, "...\n", 4);
        used += 4;
    }

    /*  Emit with a single stdio call
     * stdio locks the stream for the duration of fwrite(), so
     * concurrent LOG() calls cannot interleave within a line. */
    (void)fwrite(buf, 1, used, stderr);
}