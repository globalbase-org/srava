/*
 * win_sigbreak — Windows で「グレースフル Ctrl+C 終了」を回帰テストするための launcher。
 *
 * Windows には POSIX signal が無く、tinyState は SetConsoleCtrlHandler でコンソール制御
 * イベントを捕まえる(CTRL_BREAK_EVENT → SIGINT にマップ)。テストが `raise(SIGINT)` で
 * 自己発火しても console handler には載らないため、代わりに本 launcher が:
 *   1. srava を CREATE_NEW_PROCESS_GROUP で起動(自分のグループのリーダにする)
 *   2. in-flight agent を持って評価中になった頃に GenerateConsoleCtrlEvent(CTRL_BREAK, group)
 *      を **srava のグループにだけ** 送る(group 限定なので ctest/親を巻き込まない)
 *   3. srava のグレースフル終了(exit 130 = 128+SIGINT)を待つ。ハングなら失敗
 * env(SRAVA_SOURCE / SRAVA_AGENT / SRAVA_CACHE_DIR / PIG_TEST_SLOW)は継承して srava へ渡す。
 *
 * 使い方: win_sigbreak <srava.exe> [pre_break_ms(既定 600)]
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: win_sigbreak <srava.exe> [ms]\n"); return 2; }
    DWORD ms = (argc >= 3) ? (DWORD)atoi(argv[2]) : 600;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;

    char cmd[1024];
    _snprintf(cmd, sizeof cmd, "\"%s\"", argv[1]);

    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                        CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "win_sigbreak: CreateProcess failed %lu\n", GetLastError());
        return 1;
    }

    Sleep(ms);   /* srava が評価中(in-flight agent 有)になるまで待つ */
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId))
        fprintf(stderr, "win_sigbreak: GenerateConsoleCtrlEvent failed %lu\n", GetLastError());

    DWORD w = WaitForSingleObject(pi.hProcess, 15000);
    if (w == WAIT_TIMEOUT) {
        fprintf(stderr, "win_sigbreak: srava HUNG (no graceful shutdown)\n");
        TerminateProcess(pi.hProcess, 99);
        return 3;
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    fprintf(stderr, "win_sigbreak: srava exit code=%lu\n", code);
    /* srava が "interrupted by SIGINT" を出していれば PASS_REGULAR_EXPRESSION が拾う。
     * ここでは終了コードでなく出力で判定するので、常に 0 を返す(ハング時のみ非0)。 */
    return 0;
}
