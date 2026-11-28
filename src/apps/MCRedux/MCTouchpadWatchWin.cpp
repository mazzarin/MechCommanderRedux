// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCTouchpadWatch.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#pragma comment(lib, "user32.lib")

#include "iface/MCTouchpadNotice.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <string>

namespace
{
    /// <summary>HID's generic desktop page and its mouse (what Windows' raw input registers).</summary>
    constexpr USHORT GenericDesktopPage = 0x01;
    constexpr USHORT MouseUsage = 0x02;
    /// <summary>The window class of the watcher's message-only window.</summary>
    constexpr const wchar_t* WatchClass = L"MCRedux.TouchpadWatch";
    /// <summary>The first virtual key past the mouse's buttons (VK_BACK), and the last there is (VK_OEM_CLEAR).</summary>
    constexpr int FirstKey = VK_BACK;
    constexpr int LastKey = VK_OEM_CLEAR;
    /// <summary>GetAsyncKeyState's bit for a key down now.</summary>
    constexpr SHORT DownBit = static_cast<SHORT>(0x8000);

    /// <summary>What a plugged-in mouse's device name carries: its vendor's id (USB's "VID_", Bluetooth's "VID&").</summary>
    constexpr std::wstring_view UsbVendor = L"VID_";
    constexpr std::wstring_view BluetoothVendor = L"VID&";

    std::atomic<bool> Started = false;

    /// <summary>The keys a held click passes with (the drivers let Shift, Ctrl, Alt and the Windows keys through).</summary>
    bool PassesClicks(int key)
    {
        switch (key)
        {
            case VK_SHIFT:
            case VK_CONTROL:
            case VK_MENU:
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_LCONTROL:
            case VK_RCONTROL:
            case VK_LMENU:
            case VK_RMENU:
            case VK_LWIN:
            case VK_RWIN:
                return true;
            default:
                return false;
        }
    }

    /// <summary>Whether a key the drivers drop clicks under is down now (Space, L, C: any but the modifiers).</summary>
    bool KeyHeld()
    {
        for (int key = FirstKey; key <= LastKey; ++key)
        {
            if (!PassesClicks(key) && (GetAsyncKeyState(key) & DownBit) != 0)
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>A packet with nothing in it: no button, no wheel, no move (what a dropped press or release leaves).</summary>
    bool Empty(const RAWMOUSE& mouse)
    {
        return mouse.usButtonFlags == 0 && mouse.usButtonData == 0 && mouse.ulRawButtons == 0 && mouse.lLastX == 0 &&
               mouse.lLastY == 0 && (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0;
    }

    /// <summary>
    /// Whether <paramref name="device"/> is built in: a laptop's touchpad (on ACPI, I2C or the PS/2 port), not a mouse
    /// plugged in by USB or paired by Bluetooth, which never drops clicks this way. Injected input has no device.
    /// </summary>
    bool BuiltIn(HANDLE device)
    {
        if (device == nullptr)
        {
            return false;
        }

        UINT length = 0;

        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &length) != 0 || length == 0)
        {
            return false;
        }

        std::wstring name(length, L'\0');

        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &length) == static_cast<UINT>(-1))
        {
            return false;
        }

        std::ranges::transform(name, name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        return !name.contains(UsbVendor) && !name.contains(BluetoothVendor);
    }

    LRESULT CALLBACK WatchProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_INPUT)
        {
            RAWINPUT input{};
            UINT size = sizeof(input);

            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size,
                                sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                input.header.dwType == RIM_TYPEMOUSE && Empty(input.data.mouse) && KeyHeld() &&
                BuiltIn(input.header.hDevice))
            {
                MCTouchpadNotice::EmptyPacket(MCTouchpadNotice::Clock::now());
            }
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }

    DWORD WINAPI Watch(LPVOID)
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WatchProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = WatchClass;

        if (RegisterClassExW(&windowClass) == 0)
        {
            SDL_Log("Touchpad: the watcher's window class can't be registered (%lu); dropped clicks go unseen",
                    GetLastError());
            return 0;
        }

        const HWND window =
            CreateWindowExW(0, WatchClass, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);

        if (window == nullptr)
        {
            SDL_Log("Touchpad: the watcher's window can't be made (%lu); dropped clicks go unseen", GetLastError());
            return 0;
        }

        // Without RIDEV_INPUTSINK: only while the game is in front, as SDL's own raw input thread registers.
        const RAWINPUTDEVICE mouse{
            .usUsagePage = GenericDesktopPage, .usUsage = MouseUsage, .dwFlags = 0, .hwndTarget = window};

        if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse)))
        {
            SDL_Log("Touchpad: the mouse's raw input can't be registered (%lu); dropped clicks go unseen",
                    GetLastError());
            DestroyWindow(window);
            return 0;
        }

        MSG message{};

        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            DispatchMessageW(&message);
        }

        return 0;
    }
}

void MCStartTouchpadWatch()
{
    if (Started.exchange(true))
    {
        return;
    }

    // The watcher runs until the game quits, its window and registration going with the process.
    const HANDLE thread = CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);

    if (thread == nullptr)
    {
        SDL_Log("Touchpad: the watcher can't start (%lu); dropped clicks go unseen", GetLastError());
        return;
    }

    CloseHandle(thread);
}

#endif
