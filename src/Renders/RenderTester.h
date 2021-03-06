#pragma once

#include "resource.h"

#define SCREEN_WIDTH    640
#define SCREEN_HEIGHT   288

/// \brief Initialize the render.
/// @param width Source bitmap width
/// @param height Source bitmap height
/// @return Initialization result
typedef BOOL (CALLBACK* RENDER_INIT_CALLBACK)(int width, int height, HWND hwndTarget);

/// \brief Finalize the render.
typedef void (CALLBACK* RENDER_DONE_CALLBACK)();

/// \brief Definition for render mode enumeration procedure.
typedef void (CALLBACK* RENDER_MODE_ENUM_PROC)(int modeNum, LPCTSTR modeDesc, int modeWidth, int modeHeight);

/// \brief Enumerate available render modes.
typedef void (CALLBACK* RENDER_ENUM_MODES_CALLBACK)(RENDER_MODE_ENUM_PROC);

/// \brief Select current render mode.
typedef BOOL (CALLBACK* RENDER_SELECT_MODE_CALLBACK)(int mode);

/// \brief Draw the source bitmap to the target window.
/// @param pixels Source bitmap
typedef void (CALLBACK* RENDER_DRAW_CALLBACK)(const void * pixels, HDC hdcTarget);
