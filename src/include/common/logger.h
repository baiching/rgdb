#ifndef LOGGER_H
#define LOGGER_H

/*
* It logs  and prints states
*/
void log_msg(const char *file, int line, const char *fmt, ...);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)));
#endif // defined(__GNUC__) || defined(__clang__)

#define LOG(...) log_msg(__FILE__, __LINE__, __VA_ARGS__)

#endif // !LOGGER_H
