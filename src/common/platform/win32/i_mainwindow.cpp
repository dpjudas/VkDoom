
#include "i_mainwindow.h"
#include "resource.h"
#include "startupinfo.h"
#include "gstrings.h"
#include "palentry.h"
#include "st_start.h"
#include "i_input.h"
#include "version.h"
#include "utf8.h"
#include "v_font.h"
#include "i_net.h"
#include "engineerrors.h"
#include "common/widgets/errorwindow.h"
#include "common/widgets/netstartwindow.h"
#include <richedit.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

MainWindow* mainwindow;

MainWindow::MainWindow(const FString& title, int x, int y, int width, int height) : Widget(nullptr, WidgetType::Window, RenderAPI::Vulkan)
{
	SetWindowTitle(title.GetChars());
	SetFrameGeometry(x, y, width, height);

	SetWindowLongPtr(GetHandle(), GWLP_USERDATA, 0);
}

void MainWindow::SetWindowTitle(const char* caption)
{
	if (!caption)
	{
		FStringf default_caption("" GAMENAME " %s (%s)", GetVersionString(), GetGitTime());
		Widget::SetWindowTitle(default_caption.GetChars());
	}
	else
	{
		Widget::SetWindowTitle(caption);
	}
}

void MainWindow::ShowGameView()
{
	HWND hwnd = GetHandle();
	if (GetWindowLongPtr(hwnd, GWLP_USERDATA) == 0)
	{
		SetWindowLongPtr(hwnd, GWLP_USERDATA, 1);
		SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
		I_InitInput(hwnd);
	}
}

void MainWindow::RestoreConView()
{
	I_ShutdownInput();
	Hide();

	DeleteStartupScreen();
}

HWND MainWindow::GetHandle()
{
	return static_cast<Win32NativeHandle*>(GetNativeHandle())->hwnd;
}

/////////////////////////////////////////////////////////////////////////////

bool IsZWidgetAvailable();

static TArray<FString> bufferedConsoleStuff;
static bool restartrequest = false;

void PrintStr(const char* cp)
{
	bufferedConsoleStuff.Push(cp);
}

void GetLog(std::function<bool(const void* data, uint32_t size, uint32_t& written)> writeData)
{
	for (const FString& line : bufferedConsoleStuff)
	{
		size_t pos = 0;
		size_t len = line.Len();
		while (pos < len)
		{
			uint32_t size = (uint32_t)std::min(len - pos, 0x0fffffffULL);
			uint32_t written = 0;
			if (!writeData(&line[pos], size, written))
				return;
			pos += written;
		}
	}
}

void ShowErrorPane(const char* text)
{
	if (StartWindow)	// Ensure that the network pane is hidden.
	{
		I_NetDone();
	}

	size_t totalsize = 0;
	for (const FString& line : bufferedConsoleStuff)
		totalsize += line.Len();

	std::string alltext;
	alltext.reserve(totalsize);
	for (const FString& line : bufferedConsoleStuff)
		alltext.append(line.GetChars(), line.Len());

	if (IsZWidgetAvailable())
	{
		restartrequest = ErrorWindow::ExecModal(text, alltext);
	}
	else // We are aborting before we even got to load zdoom.pk3
	{
		MessageBoxA(0, text, "Fatal Error", MB_OK | MB_ICONERROR);
		restartrequest = false;
	}
}

bool CheckForRestart()
{
	bool result = restartrequest;
	restartrequest = false;
	return result;
}
