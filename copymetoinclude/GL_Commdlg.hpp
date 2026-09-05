/*
  GL_Commdlg
  Copyright (C) 2025 Gao Li

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/**
 *  \file GL_Commdlg.hpp
 *
 *  本头文件包装了一些Windows Commdlg还有Windows Shell中和用户对话框有关的API，使其易用。而且对其没有的功能进行了拓展。
 */


#ifndef __INC_GL_COMMDLG_
#define __INC_GL_COMMDLG_

// 链接对话框库，若使用非MSCV编译器，请添加编译参数-lcomdlg32 -lshell32 -lgdi32 -lole32 -luuid -ldwmapi
#ifdef _MSC_VER
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

#include "GL_Commdlg_Native.hpp"
#include "GL_Commdlg_Extended.hpp"

#endif
