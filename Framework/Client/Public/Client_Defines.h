#pragma once

#include <windows.h>
using namespace std;

namespace Client
{
	static constexpr unsigned int g_iWinSizeX = { 1280 };
	static constexpr unsigned int g_iWinSizeY = { 720 };
}

extern HWND g_hWnd;
extern HINSTANCE g_hInstance;