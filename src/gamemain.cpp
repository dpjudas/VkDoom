
#if defined(WIN32)

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>

int I_GameMain(HINSTANCE hInstance, HINSTANCE nothing, LPWSTR cmdline, int nCmdShow);

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#include <cstdint>
#pragma comment(linker,"/ENTRY:mainStartup")

#ifdef __AVX2__
#error "This file must be compiled with /arch:SSE2"
#endif

extern "C"
{
	int WINAPI wWinMainCRTStartup();

	void cpuCheck()
	{
		int cpui[4] = { -1 };
		__cpuid(cpui, 0);

		int data[1000][4];

		int nIds = cpui[0];
		if (nIds > 1000) nIds = 1000;
		for (int i = 0; i <= nIds; ++i)
		{
			__cpuidex(cpui, i, 0);
			for (int j = 0; j < 4; j++) data[i][j] = cpui[j];
		}

		uint32_t f_7_EBX = 0;
		uint32_t f_7_ECX = 0;

		// load bitset with flags for function 0x00000007
		if (nIds >= 7)
		{
			f_7_EBX = data[7][1];
			f_7_ECX = data[7][2];
		}

		bool avx2 = ((f_7_EBX >> 5) & 1) == 1;
		if (!avx2)
		{
			MessageBoxW(0, L"This application requires a CPU with the AVX2 instruction set to run.", L"CPU Not Supported", MB_OK | MB_ICONERROR);
			ExitProcess(0);
		}
	}

	int WINAPI mainStartup()
	{
		cpuCheck();
		return wWinMainCRTStartup();
	}
}
#endif

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
