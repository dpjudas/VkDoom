#pragma once

#include "zstring.h"
#include "printf.h"

#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <zwidget/core/widget.h>
#include <zwidget/window/win32nativehandle.h>

#if 1

class MainWindow : public Widget
{
public:
	MainWindow(const FString& title, int x, int y, int width, int height);

	void ShowGameView();
	void RestoreConView();

	void SetWindowTitle(const char* caption);

	HWND GetHandle();
};

extern MainWindow* mainwindow;

void PrintStr(const char* cp);
void GetLog(std::function<bool(const void* data, uint32_t size, uint32_t& written)> writeFile);

void ShowErrorPane(const char* text);
bool CheckForRestart();

#else

// The WndProc used when the game view is active
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

class MainWindow
{
public:
	void Create(const FString& title, int x, int y, int width, int height);

	void ShowGameView();
	void RestoreConView();

	void ShowErrorPane(const char* text);
	bool CheckForRestart();

	void PrintStr(const char* cp);
	void GetLog(std::function<bool(const void* data, uint32_t size, uint32_t& written)> writeFile);

	void SetWindowTitle(const char* caption);

	HWND GetHandle() { return Window; }

private:
	static LRESULT CALLBACK LConProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND Window = 0;
	bool restartrequest = false;
	TArray<FString> bufferedConsoleStuff;
};

extern MainWindow mainwindow;

#endif
