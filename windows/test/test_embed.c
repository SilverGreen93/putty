/*
 * test_embed: drive putty-embed.dll the way SuperXPuTTY does, from a
 * plain Win32 message loop, and check the things that matter for
 * embedding:
 *  - two terminals as child windows of one top-level window
 *  - output keeps flowing (title escapes via on_title) through the
 *    host's message loop, including data read from a pipe, which goes
 *    through the handle-wait helper thread
 *  - destroying one terminal fires on_closed once and leaves the other
 *    one working
 *  - a bad command line returns NULL with an error, and doesn't exit
 *
 * Usage: test_embed <path-to-putty-embed.dll>      Exit code 0 = pass.
 * The sessions use "test_embed --ticker" as a local proxy command; it
 * prints xterm title escapes with an increasing counter.
 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>

typedef void (__cdecl *title_fn)(void *, const wchar_t *);
typedef void (__cdecl *closed_fn)(void *, int);
typedef HWND (__cdecl *create_fn)(HWND, const wchar_t *, title_fn,
                                  closed_fn, void *);
typedef const wchar_t *(__cdecl *lasterr_fn)(void);

static int titles[2], closed[2];
static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); \
            failures++; } } while (0)

static void __cdecl on_title(void *ctx, const wchar_t *title)
{
    titles[(INT_PTR)ctx]++;
    if (getenv("TEST_EMBED_VERBOSE"))
        wprintf(L"[%d] title: %ls\n", (int)(INT_PTR)ctx, title);
}

static void __cdecl on_closed(void *ctx, int code)
{
    closed[(INT_PTR)ctx]++;
}

/* Pump messages for 'ms' milliseconds or until *flag >= target */
static void pump(int ms, int *flag, int target)
{
    DWORD end = GetTickCount() + ms;
    while ((int)(end - GetTickCount()) > 0) {
        if (flag && *flag >= target)
            return;
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
}

static int ticker(void)
{
    for (int i = 0;; i++) {
        if (printf("\033]0;tick %d\007", i) < 0 || fflush(stdout) != 0)
            return 0;                  /* PuTTY went away */
        Sleep(100);
    }
}

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 2 && !wcscmp(argv[1], L"--ticker"))
        return ticker();
    if (argc < 2) {
        printf("usage: test_embed <dll>\n");
        return 2;
    }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) {
        printf("FAIL: LoadLibrary %lu\n", GetLastError());
        return 1;
    }
    create_fn create = (create_fn)GetProcAddress(dll, "putty_session_create");
    lasterr_fn lasterr = (lasterr_fn)GetProcAddress(dll, "putty_last_error");
    CHECK(create && lasterr, "exports missing");
    if (failures)
        return 1;

    HWND top = CreateWindowW(L"STATIC", L"test_embed host",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             0, 0, 800, 600, NULL, NULL, NULL, NULL);
    HWND panel[2];
    for (int i = 0; i < 2; i++)
        panel[i] = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                 i * 400, 0, 400, 600, top, NULL, NULL,
                                 NULL);

    /* Bad command line: must fail softly */
    HWND bad = create(panel[0], L"-no-such-option", on_title, on_closed,
                      (void *)0);
    CHECK(bad == NULL, "bad command line accepted");
    CHECK(wcslen(lasterr()) > 0, "no error text for bad command line");
    wprintf(L"bad cmdline error: %ls\n", lasterr());

    wchar_t args[2048];
    /* PuTTY never proxies "localhost" by default, hence the fake host.
     * Proxy commands treat '\' as an escape character */
    wchar_t selfesc[2 * MAX_PATH], *q = selfesc;
    for (wchar_t *c = self; *c; c++) {
        if (*c == L'\\')
            *q++ = L'\\';
        *q++ = *c;
    }
    *q = 0;
    swprintf(args, 2048, L"-raw -proxycmd \"%ls --ticker\" embed-test.invalid",
             selfesc);

    HWND term[2];
    for (int i = 0; i < 2; i++) {
        term[i] = create(panel[i], args, on_title, on_closed,
                         (void *)(INT_PTR)i);
        CHECK(term[i] != NULL, "session %d not created: %ls", i, lasterr());
    }
    if (failures)
        return 1;

    CHECK(GetParent(term[0]) == panel[0], "terminal is not a child");
    CHECK(GetWindowLongW(term[0], GWL_STYLE) & WS_CHILD, "no WS_CHILD");
    CHECK(GetAncestor(term[1], GA_ROOT) == top, "wrong root window");

    /* Initial titles from both sessions */
    pump(10000, &titles[0], 3);
    pump(10000, &titles[1], 3);
    CHECK(titles[0] >= 3 && titles[1] >= 3,
          "no output: titles %d/%d", titles[0], titles[1]);

    /* Focus moves into a terminal like into any child control */
    SetFocus(term[1]);
    CHECK(GetFocus() == term[1], "terminal can't take focus");

    /* Close the first one */
    DestroyWindow(term[0]);
    CHECK(closed[0] == 1, "on_closed not called on destroy (%d)",
          closed[0]);
    CHECK(!IsWindow(term[0]), "terminal 0 still exists");

    /* The second keeps working */
    int before = titles[1];
    pump(10000, &titles[1], before + 3);
    CHECK(titles[1] >= before + 3, "session 1 stalled after closing 0");

    /* Destroying the host window destroys the remaining terminal */
    DestroyWindow(top);
    CHECK(closed[1] == 1, "on_closed not called via parent destroy");
    CHECK(closed[0] == 1, "on_closed called twice");
    pump(300, NULL, 0);                /* drain anything left queued */

    printf(failures ? "FAILED (%d)\n" : "PASS\n", failures);
    return failures ? 1 : 0;
}
