#pragma once

#if defined(_WIN32) || defined(_WIN64)
#ifdef TRIKDSP_LIBRARY
#define TRIKDSP_EXPORT __declspec(dllexport)
#else
#define TRIKDSP_EXPORT __declspec(dllimport)
#endif
#else
#define TRIKDSP_EXPORT
#endif
