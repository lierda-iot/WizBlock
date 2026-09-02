#include "companion_core_test_cases.h"

#if defined(COMPANION_HOST_NO_CRT)
__declspec(dllimport) void __stdcall ExitProcess(unsigned int exit_code);
int _fltused = 0;
#endif

int main(void)
{
    return companion_core_run_tests();
}

#if defined(COMPANION_HOST_NO_CRT)
void mainCRTStartup(void)
{
    ExitProcess((unsigned int)main());
}
#endif
