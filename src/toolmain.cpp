
#if defined(WIN32)

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>

int I_ToolMain(HINSTANCE hInstance, HINSTANCE nothing, LPWSTR cmdline, int nCmdShow);

int wmain()
{
	return I_ToolMain(GetModuleHandle(0), 0, GetCommandLineW(), SW_SHOW);
}

#else

int I_ToolMain(int argc, char** argv);

int main(int argc, char** argv)
{
	return I_ToolMain(argc, argv);
}

#endif
