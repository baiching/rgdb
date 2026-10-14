#ifndef RGDB_H
#define RGDB_H

/*
 * rgdb.h : project-wide entry point header (C11 port of rgdb.h).
 *
 * The C++ version included <iostream> here, which every translation unit that
 * included it inherited. That is gone: this header now pulls in the project's
 * common configuration and the standard integer/bool types only, so it stays
 * cheap to include.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/windows.h"

/* Project version, useful for logging and for the CLI's --version output. */
#define RGDB_VERSION_MAJOR 0
#define RGDB_VERSION_MINOR 1
#define RGDB_VERSION_PATCH 0
#define RGDB_VERSION_STRING "0.1.0"

#endif /* RGDB_H */
