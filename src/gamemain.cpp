
#if defined(WIN32)

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>

int I_GameMain(HINSTANCE hInstance, HINSTANCE nothing, LPWSTR cmdline, int nCmdShow);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE nothing, LPWSTR cmdline, int nCmdShow)
{
	return I_GameMain(hInstance, nothing, cmdline, nCmdShow);
}

#else

int I_GameMain(int argc, char** argv);

int main(int argc, char** argv)
{
	return I_GameMain(argc, argv);
}

#endif
