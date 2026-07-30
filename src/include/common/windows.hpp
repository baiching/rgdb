#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif // _WINSOCKAPI_

#include <Windows.h>

/*

// add or remove as we need
#undef CreateDirectory
#undef MoveFile
#undef RemoveDirectory
*/
#endif // _WIN32
