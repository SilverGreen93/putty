/*
 * putty-dll.c: PuTTY as a DLL whose terminals are child windows of a
 * host application (SuperXPuTTY), so the host stays one top-level
 * window for Alt+Tab, focus and keyboard handling.
 *
 * Threading model: everything runs on the host's UI thread, driven by
 * the host's own message loop. Call every export from that thread.
 *
 * What replaces PuTTY's WinMain loop:
 *  - socket events: WSAAsyncSelect to a message-only window (below)
 *  - timers: the hidden timing window from utils/gui-timing.c
 *  - toplevel callbacks: a posted message to that window
 *  - handle waits (pipes, serial, agent): a helper thread waits and
 *    posts a message; the handle is serviced on the UI thread
 *
 * As in PuTTY's own loop, callbacks and handle waits only run "at the
 * top level": never while PuTTY code is already on the stack (e.g. in
 * a nested modal loop of a host key prompt), see putty_code_depth.
 */

#include "putty.h"
#include "win-gui-seat.h"

#define PUTTY_API __declspec(dllexport)

#define WM_PD_CALLBACKS (WM_APP + 1)
#define WM_PD_HANDLE    (WM_APP + 2)

extern int putty_code_depth;           /* window.c */
extern char *embed_last_error;         /* window.c */

static HINSTANCE dll_hinst;
static bool inited;
static HWND notify_hwnd;

static bool callbacks_posted, callbacks_deferred;
static HANDLE handle_deferred;
static bool handle_is_deferred;

/* ---------------------------------------------------------------- *
 * Handle-wait helper thread
 * ---------------------------------------------------------------- */

static HANDLE hw_go;        /* auto-reset: main -> thread "wait now" */
static HANDLE hw_wake;      /* manual-reset: main -> thread "stop waiting" */
static HANDLE hw_idle;      /* manual-reset: thread is not waiting */
static bool hw_armed;
static HANDLE hw_handles[MAXIMUM_WAIT_OBJECTS]; /* [0] is hw_wake */
static DWORD hw_n;          /* number of entries in hw_handles */

static DWORD WINAPI hw_thread_proc(LPVOID param)
{
    for (;;) {
        WaitForSingleObject(hw_go, INFINITE);
        DWORD r = WaitForMultipleObjects(hw_n, hw_handles, false, INFINITE);
        bool stopped = (r == WAIT_OBJECT_0);
        HANDLE h = NULL;
        if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + hw_n)
            h = hw_handles[r - WAIT_OBJECT_0];
        SetEvent(hw_idle);
        /* h == NULL && !stopped means WAIT_FAILED: just ask to re-arm */
        if (!stopped)
            PostMessage(notify_hwnd, WM_PD_HANDLE, (WPARAM)h, 0);
    }
    return 0;
}

static void hw_stop(void)
{
    if (!hw_armed)
        return;
    SetEvent(hw_wake);
    WaitForSingleObject(hw_idle, INFINITE);
    ResetEvent(hw_wake);
    hw_armed = false;
}

static void hw_arm(void)
{
    hw_stop();

    HandleWaitList *hwl = get_handle_wait_list();
    size_t n = hwl->nhandles;
    /* ponytail: 63 handles max (one slot is hw_wake), same order of
     * limit as PuTTY's own MsgWaitForMultipleObjects loop */
    if (n > MAXIMUM_WAIT_OBJECTS - 1)
        n = MAXIMUM_WAIT_OBJECTS - 1;
    hw_handles[0] = hw_wake;
    for (size_t i = 0; i < n; i++)
        hw_handles[i + 1] = hwl->handles[i];
    hw_n = n + 1;
    handle_wait_list_free(hwl);

    if (n == 0)
        return;
    ResetEvent(hw_idle);
    hw_armed = true;
    SetEvent(hw_go);
}

/* add_handle_wait / delete_handle_wait hook */
static void hw_changed(void)
{
    hw_arm();
}

static void hw_service(HANDLE h)
{
    if (h) {
        /* Look the handle up afresh: the set may have changed since
         * the thread saw it signalled. */
        HandleWaitList *hwl = get_handle_wait_list();
        for (int i = 0; i < hwl->nhandles; i++) {
            if (hwl->handles[i] == h) {
                handle_wait_activate(hwl, i);
                break;
            }
        }
        handle_wait_list_free(hwl);
    }
    hw_arm();
}

/* ---------------------------------------------------------------- *
 * Toplevel callbacks and the notification window
 * ---------------------------------------------------------------- */

static void post_callbacks(void)
{
    if (!callbacks_posted) {
        callbacks_posted = true;
        PostMessage(notify_hwnd, WM_PD_CALLBACKS, 0, 0);
    }
}

static void notify_callbacks(void *ctx)
{
    post_callbacks();
}

/* window.c calls this whenever PuTTY code fully leaves the stack */
void putty_code_left(void)
{
    if (callbacks_deferred) {
        callbacks_deferred = false;
        post_callbacks();
    }
    if (handle_is_deferred) {
        handle_is_deferred = false;
        PostMessage(notify_hwnd, WM_PD_HANDLE, (WPARAM)handle_deferred, 0);
    }
}

static void run_callbacks(void)
{
    /* Run for a bounded time, then yield to the host's other messages
     * (input, painting) and continue on the next posted message. */
    DWORD start = GetTickCount();
    while (run_toplevel_callbacks()) {
        if (GetTickCount() - start > 20)
            break;
    }
    if (toplevel_callback_pending())
        post_callbacks();
}

static LRESULT CALLBACK notify_wndproc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    bool top_level = (putty_code_depth == 0);
    LRESULT ret = 0;

    putty_code_depth++;
    switch (msg) {
      case WM_NETEVENT:
        winselgui_response(wParam, lParam);   /* only queues a callback */
        break;
      case WM_PD_CALLBACKS:
        callbacks_posted = false;
        if (top_level)
            run_callbacks();
        else
            callbacks_deferred = true;
        break;
      case WM_PD_HANDLE:
        if (top_level) {
            hw_service((HANDLE)wParam);
        } else {
            handle_deferred = (HANDLE)wParam;
            handle_is_deferred = true;
        }
        break;
      default:
        ret = DefWindowProcW(hwnd, msg, wParam, lParam);
        break;
    }
    if (--putty_code_depth == 0)
        putty_code_left();
    return ret;
}

/* ---------------------------------------------------------------- *
 * Exported API
 * ---------------------------------------------------------------- */

typedef void (__cdecl *putty_title_fn)(void *ctx, const wchar_t *title);
typedef void (__cdecl *putty_closed_fn)(void *ctx, int exitcode);

/* Returns nonzero on success. Called implicitly by session_create. */
PUTTY_API int __cdecl putty_init(void)
{
    if (inited)
        return 1;

    if (!putty_process_init(dll_hinst, NULL))
        return 0;

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = notify_wndproc;
    wc.hInstance = dll_hinst;
    wc.lpszClassName = L"PuTTYEmbedNotify";
    RegisterClassW(&wc);
    notify_hwnd = CreateWindowW(L"PuTTYEmbedNotify", L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, NULL, dll_hinst, NULL);
    if (!notify_hwnd)
        return 0;

    winselgui_set_hwnd(notify_hwnd);
    request_callback_notifications(notify_callbacks, NULL);

    hw_go = CreateEvent(NULL, false, false, NULL);
    hw_wake = CreateEvent(NULL, true, false, NULL);
    hw_idle = CreateEvent(NULL, true, true, NULL);
    HANDLE th = CreateThread(NULL, 0, hw_thread_proc, NULL, 0, NULL);
    if (!th)
        return 0;
    CloseHandle(th);
    handle_wait_list_changed = hw_changed;
    hw_arm();                          /* in case anything is waiting */

    inited = true;
    return 1;
}

/*
 * Start a session from a PuTTY command line (without the program
 * name) in a new child window of 'parent'. Returns the terminal window,
 * or NULL with the reason available from putty_last_error().
 *
 * on_title is called when the window title changes. on_closed is
 * called once, when the terminal window is destroyed: by the session
 * ending (close-on-exit), or by the host destroying it or its parent.
 */
PUTTY_API HWND __cdecl putty_session_create(
    HWND parent, const wchar_t *args,
    putty_title_fn on_title, putty_closed_fn on_closed, void *ctx)
{
    sfree(embed_last_error);
    embed_last_error = NULL;

    if (!putty_init()) {
        embed_last_error = dupstr("PuTTY initialisation failed");
        return NULL;
    }

    /* GetCommandLineW() replacement: needs a program name first and
     * must be writable (cmdline_arg_wipe blanks out -pw values). */
    size_t len = wcslen(args);
    wchar_t *wcmd = snewn(len + 7, wchar_t);
    wcscpy(wcmd, L"putty ");
    wcscat(wcmd, args);
    char *acmd = dup_wc_to_mb(CP_ACP, args, "?");

    PuttyEmbedCallbacks cb = {
        .on_title = on_title,
        .on_closed = on_closed,
    };

    cmdline_override_w = wcmd;
    putty_code_depth++;
    WinGuiSeat *wgs = wgs_new(acmd, parent, &cb, ctx, SW_SHOW);
    if (--putty_code_depth == 0)
        putty_code_left();
    cmdline_override_w = NULL;

    smemclr(wcmd, (len + 7) * sizeof(wchar_t));
    sfree(wcmd);
    burnstr(acmd);

    return wgs ? wgs->term_hwnd : NULL;
}

/* Reason for the last failed putty_session_create (static buffer). */
PUTTY_API const wchar_t *__cdecl putty_last_error(void)
{
    static wchar_t *last;
    sfree(last);
    last = dup_mb_to_wc(CP_ACP, embed_last_error ? embed_last_error : "");
    return last;
}

/* PuTTY's configuration dialog, for editing saved sessions. */
PUTTY_API int __cdecl putty_show_config(void)
{
    if (!putty_init())
        return 0;

    settings_set_default_protocol(be_default_protocol);
    const struct BackendVtable *vt =
        backend_vt_from_proto(be_default_protocol);
    settings_set_default_port(vt ? vt->default_port : 0);

    Conf *conf = conf_new();
    do_defaults(NULL, conf);
    putty_code_depth++;
    bool ret = do_config(conf);
    if (--putty_code_depth == 0)
        putty_code_left();
    conf_free(conf);
    return ret;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        dll_hinst = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
