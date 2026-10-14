#ifndef RGDB_COMMON_WINDOWS_H
#define RGDB_COMMON_WINDOWS_H

/*
 * Central Windows include gateway (C11).
 *
 * This used to be windows.hpp. It exists so that <Windows.h> is pulled in with
 * the right feature macros set beforehand, and so that the handful of
 * macro-collision workarounds the codebase needs live in exactly one place.
 */

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif /* NOMINMAX */

/*
 * winsock2.h must precede windows.h, and windows.h must not pull in the old
 * winsock.h; defining _WINSOCKAPI_ suppresses that legacy include.
 */
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif /* _WINSOCKAPI_ */

#include <Windows.h>

/*
 * Portable name for the read/write lock type used by rgdb_safe_map and the
 * page allocator.
 *
 * C11 has no threads library on MSVC (<threads.h> is not implemented and
 * does not provide a rwlock in any case), so the platform primitive is used
 * directly. The typedef keeps that decision in one file: a POSIX port only
 * has to change the definitions below.
 */
typedef SRWLOCK rgdb_rwlock_t;

/*
 * Two valid ways to initialise one of these:
 *
 *   rgdb_rwlock_t l = RGDB_RWLOCK_INIT;   // static / whole-object initialiser
 *   InitializeSRWLock(&l);                // for a member of a heap object
 *
 * The codebase uses InitializeSRWLock, because every lock here lives inside a
 * struct that is memset to zero and then explicitly initialised. Both are
 * correct; the macro is kept for the static case.
 */
#define RGDB_RWLOCK_INIT SRWLOCK_INIT

#else /* !_WIN32 */

#error "rgdb currently supports Windows only; see common/windows.h for the rwlock/mutex typedefs to port."

#endif /* _WIN32 */

#endif /* RGDB_COMMON_WINDOWS_H */
