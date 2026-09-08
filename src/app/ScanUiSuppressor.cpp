#include "ScanUiSuppressor.h"

#if JUCE_WINDOWS

 #include <atomic>
 #include <cstring>
 #include <windows.h>

namespace
{
    std::atomic<int> suppressDepth { 0 };
    std::atomic<int> hookUsers { 0 };
    HHOOK cbtHook = nullptr;

    struct PrologueHook
    {
        void* target = nullptr;
        uint8_t saved[16] {};
        bool active = false;

        bool install (void* function, void* hookFn)
        {
            if (function == nullptr || hookFn == nullptr || active)
                return false;

            target = function;
            std::memcpy (saved, function, 12);

            DWORD oldProtect = 0;
            if (! VirtualProtect (function, 16, PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;

            auto* p = static_cast<uint8_t*> (function);
            p[0] = 0x48; // mov rax, imm64
            p[1] = 0xB8;
            std::memcpy (p + 2, &hookFn, 8);
            p[10] = 0xFF; // jmp rax
            p[11] = 0xE0;

            DWORD ignore = 0;
            VirtualProtect (function, 16, oldProtect, &ignore);
            FlushInstructionCache (GetCurrentProcess(), function, 16);
            active = true;
            return true;
        }

        void uninstall()
        {
            if (! active || target == nullptr)
                return;

            DWORD oldProtect = 0;
            if (VirtualProtect (target, 16, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                std::memcpy (target, saved, 12);
                DWORD ignore = 0;
                VirtualProtect (target, 16, oldProtect, &ignore);
                FlushInstructionCache (GetCurrentProcess(), target, 16);
            }

            active = false;
            target = nullptr;
        }
    };

    PrologueHook hookMessageBoxA, hookMessageBoxW, hookMessageBoxExA, hookMessageBoxExW;

    int messageBoxResult (UINT type) noexcept
    {
        switch (type & MB_TYPEMASK)
        {
            case MB_OKCANCEL:
            case MB_YESNO:
            case MB_YESNOCANCEL:
            case MB_RETRYCANCEL:
            case MB_ABORTRETRYIGNORE:
                return IDCANCEL;
            default:
                return IDOK;
        }
    }

    // Hooks are only installed while scanning — always swallow UI.
    int WINAPI hookedMessageBoxA (HWND, LPCSTR, LPCSTR, UINT type)
    {
        return messageBoxResult (type);
    }

    int WINAPI hookedMessageBoxW (HWND, LPCWSTR, LPCWSTR, UINT type)
    {
        return messageBoxResult (type);
    }

    int WINAPI hookedMessageBoxExA (HWND, LPCSTR, LPCSTR, UINT type, WORD)
    {
        return messageBoxResult (type);
    }

    int WINAPI hookedMessageBoxExW (HWND, LPCWSTR, LPCWSTR, UINT type, WORD)
    {
        return messageBoxResult (type);
    }

    BOOL CALLBACK closeDialogEnum (HWND hwnd, LPARAM)
    {
        wchar_t className[64] = {};
        if (GetClassNameW (hwnd, className, 64) > 0 && wcscmp (className, L"#32770") == 0)
        {
            if (HWND ok = GetDlgItem (hwnd, IDOK))
                PostMessageW (ok, BM_CLICK, 0, 0);
            PostMessageW (hwnd, WM_COMMAND, MAKEWPARAM (IDOK, BN_CLICKED), 0);
            EndDialog (hwnd, IDOK);
            PostMessageW (hwnd, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }

    LRESULT CALLBACK cbtProc (int code, WPARAM wParam, LPARAM lParam)
    {
        if (suppressDepth.load (std::memory_order_relaxed) > 0)
        {
            if (code == HCBT_ACTIVATE || code == HCBT_SETFOCUS)
            {
                auto* hwnd = reinterpret_cast<HWND> (wParam);
                wchar_t className[64] = {};
                if (GetClassNameW (hwnd, className, 64) > 0 && wcscmp (className, L"#32770") == 0)
                {
                    if (HWND ok = GetDlgItem (hwnd, IDOK))
                        PostMessageW (ok, BM_CLICK, 0, 0);
                    PostMessageW (hwnd, WM_COMMAND, MAKEWPARAM (IDOK, BN_CLICKED), 0);
                    EndDialog (hwnd, IDOK);
                    PostMessageW (hwnd, WM_CLOSE, 0, 0);
                }
            }
            else if (code == HCBT_CREATEWND)
            {
                // Closest backup: dismiss any dialogs already up on this thread.
                EnumThreadWindows (GetCurrentThreadId(), closeDialogEnum, 0);
            }
        }

        return CallNextHookEx (cbtHook, code, wParam, lParam);
    }

    void installHooks()
    {
        if (hookUsers.fetch_add (1) > 0)
            return;

        if (HMODULE user32 = GetModuleHandleW (L"user32.dll"))
        {
            hookMessageBoxA.install (reinterpret_cast<void*> (GetProcAddress (user32, "MessageBoxA")),
                                     reinterpret_cast<void*> (&hookedMessageBoxA));
            hookMessageBoxW.install (reinterpret_cast<void*> (GetProcAddress (user32, "MessageBoxW")),
                                     reinterpret_cast<void*> (&hookedMessageBoxW));
            hookMessageBoxExA.install (reinterpret_cast<void*> (GetProcAddress (user32, "MessageBoxExA")),
                                       reinterpret_cast<void*> (&hookedMessageBoxExA));
            hookMessageBoxExW.install (reinterpret_cast<void*> (GetProcAddress (user32, "MessageBoxExW")),
                                       reinterpret_cast<void*> (&hookedMessageBoxExW));
        }

        cbtHook = SetWindowsHookExW (WH_CBT, cbtProc, nullptr, GetCurrentThreadId());
    }

    void uninstallHooks()
    {
        if (hookUsers.fetch_sub (1) != 1)
            return;

        if (cbtHook != nullptr)
        {
            UnhookWindowsHookEx (cbtHook);
            cbtHook = nullptr;
        }

        hookMessageBoxA.uninstall();
        hookMessageBoxW.uninstall();
        hookMessageBoxExA.uninstall();
        hookMessageBoxExW.uninstall();
    }
}

struct ScanUiSuppressor::Impl
{
    Impl()
    {
        suppressDepth.fetch_add (1, std::memory_order_relaxed);
        installHooks();
    }

    ~Impl()
    {
        uninstallHooks();
        suppressDepth.fetch_sub (1, std::memory_order_relaxed);
    }
};

ScanUiSuppressor::ScanUiSuppressor()
    : impl (new Impl())
{
}

ScanUiSuppressor::~ScanUiSuppressor()
{
    delete impl;
    impl = nullptr;
}

#else

struct ScanUiSuppressor::Impl {};

ScanUiSuppressor::ScanUiSuppressor() {}
ScanUiSuppressor::~ScanUiSuppressor() {}

#endif
