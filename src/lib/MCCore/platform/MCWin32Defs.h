#pragma once

// The Win32 constants MechCommander's own logic compares against: window-message ids, virtual-key codes and mouse
// flags. The platform layer (MCPlatform) turns SDL events into messages with these values and feeds them to the
// game's translateMessage, so the original's input code reads the same as it did. Only values, no Win32 calls.

// Window messages (WM_*).
inline constexpr uint32_t WM_NULL = 0x0000;
inline constexpr uint32_t WM_CREATE = 0x0001;
inline constexpr uint32_t WM_DESTROY = 0x0002;
inline constexpr uint32_t WM_MOVE = 0x0003;
inline constexpr uint32_t WM_SIZE = 0x0005;
inline constexpr uint32_t WM_ACTIVATE = 0x0006;
inline constexpr uint32_t WM_SETFOCUS = 0x0007;
inline constexpr uint32_t WM_KILLFOCUS = 0x0008;
inline constexpr uint32_t WM_PAINT = 0x000F;
inline constexpr uint32_t WM_CLOSE = 0x0010;
inline constexpr uint32_t WM_QUIT = 0x0012;
inline constexpr uint32_t WM_ACTIVATEAPP = 0x001C;
inline constexpr uint32_t WM_SETCURSOR = 0x0020;
inline constexpr uint32_t WM_KEYDOWN = 0x0100;
inline constexpr uint32_t WM_KEYUP = 0x0101;
inline constexpr uint32_t WM_CHAR = 0x0102;
inline constexpr uint32_t WM_DEADCHAR = 0x0103;
inline constexpr uint32_t WM_SYSKEYDOWN = 0x0104;
inline constexpr uint32_t WM_SYSKEYUP = 0x0105;
inline constexpr uint32_t WM_SYSCHAR = 0x0106;
inline constexpr uint32_t WM_TIMER = 0x0113;
inline constexpr uint32_t WM_SYSCOMMAND = 0x0112;
inline constexpr uint32_t WM_MOUSEMOVE = 0x0200;
inline constexpr uint32_t WM_LBUTTONDOWN = 0x0201;
inline constexpr uint32_t WM_LBUTTONUP = 0x0202;
inline constexpr uint32_t WM_LBUTTONDBLCLK = 0x0203;
inline constexpr uint32_t WM_RBUTTONDOWN = 0x0204;
inline constexpr uint32_t WM_RBUTTONUP = 0x0205;
inline constexpr uint32_t WM_RBUTTONDBLCLK = 0x0206;
inline constexpr uint32_t WM_MBUTTONDOWN = 0x0207;
inline constexpr uint32_t WM_MBUTTONUP = 0x0208;
inline constexpr uint32_t WM_MBUTTONDBLCLK = 0x0209;
inline constexpr uint32_t WM_MOUSEWHEEL = 0x020A;
inline constexpr uint32_t WM_QUERYNEWPALETTE = 0x030F;
inline constexpr uint32_t WM_PALETTECHANGED = 0x0311;
inline constexpr uint32_t WM_USER = 0x0400;

// Mouse-message key state flags (wParam of the mouse messages).
inline constexpr uint32_t MK_LBUTTON = 0x0001;
inline constexpr uint32_t MK_RBUTTON = 0x0002;
inline constexpr uint32_t MK_SHIFT = 0x0004;
inline constexpr uint32_t MK_CONTROL = 0x0008;
inline constexpr uint32_t MK_MBUTTON = 0x0010;

// Virtual-key codes (VK_*). Letters and digits are their ASCII upper-case codes ('A'..'Z', '0'..'9').
inline constexpr int VK_LBUTTON = 0x01;
inline constexpr int VK_RBUTTON = 0x02;
inline constexpr int VK_CANCEL = 0x03;
inline constexpr int VK_MBUTTON = 0x04;
inline constexpr int VK_BACK = 0x08;
inline constexpr int VK_TAB = 0x09;
inline constexpr int VK_CLEAR = 0x0C;
inline constexpr int VK_RETURN = 0x0D;
inline constexpr int VK_SHIFT = 0x10;
inline constexpr int VK_CONTROL = 0x11;
inline constexpr int VK_MENU = 0x12;
inline constexpr int VK_PAUSE = 0x13;
inline constexpr int VK_CAPITAL = 0x14;
inline constexpr int VK_ESCAPE = 0x1B;
inline constexpr int VK_SPACE = 0x20;
inline constexpr int VK_PRIOR = 0x21;
inline constexpr int VK_NEXT = 0x22;
inline constexpr int VK_END = 0x23;
inline constexpr int VK_HOME = 0x24;
inline constexpr int VK_LEFT = 0x25;
inline constexpr int VK_UP = 0x26;
inline constexpr int VK_RIGHT = 0x27;
inline constexpr int VK_DOWN = 0x28;
inline constexpr int VK_SELECT = 0x29;
inline constexpr int VK_PRINT = 0x2A;
inline constexpr int VK_EXECUTE = 0x2B;
inline constexpr int VK_SNAPSHOT = 0x2C;
inline constexpr int VK_INSERT = 0x2D;
inline constexpr int VK_DELETE = 0x2E;
inline constexpr int VK_HELP = 0x2F;
inline constexpr int VK_LWIN = 0x5B;
inline constexpr int VK_RWIN = 0x5C;
inline constexpr int VK_APPS = 0x5D;
inline constexpr int VK_NUMPAD0 = 0x60;
inline constexpr int VK_NUMPAD1 = 0x61;
inline constexpr int VK_NUMPAD2 = 0x62;
inline constexpr int VK_NUMPAD3 = 0x63;
inline constexpr int VK_NUMPAD4 = 0x64;
inline constexpr int VK_NUMPAD5 = 0x65;
inline constexpr int VK_NUMPAD6 = 0x66;
inline constexpr int VK_NUMPAD7 = 0x67;
inline constexpr int VK_NUMPAD8 = 0x68;
inline constexpr int VK_NUMPAD9 = 0x69;
inline constexpr int VK_MULTIPLY = 0x6A;
inline constexpr int VK_ADD = 0x6B;
inline constexpr int VK_SEPARATOR = 0x6C;
inline constexpr int VK_SUBTRACT = 0x6D;
inline constexpr int VK_DECIMAL = 0x6E;
inline constexpr int VK_DIVIDE = 0x6F;
inline constexpr int VK_F1 = 0x70;
inline constexpr int VK_F2 = 0x71;
inline constexpr int VK_F3 = 0x72;
inline constexpr int VK_F4 = 0x73;
inline constexpr int VK_F5 = 0x74;
inline constexpr int VK_F6 = 0x75;
inline constexpr int VK_F7 = 0x76;
inline constexpr int VK_F8 = 0x77;
inline constexpr int VK_F9 = 0x78;
inline constexpr int VK_F10 = 0x79;
inline constexpr int VK_F11 = 0x7A;
inline constexpr int VK_F12 = 0x7B;
inline constexpr int VK_NUMLOCK = 0x90;
inline constexpr int VK_SCROLL = 0x91;
inline constexpr int VK_LSHIFT = 0xA0;
inline constexpr int VK_RSHIFT = 0xA1;
inline constexpr int VK_LCONTROL = 0xA2;
inline constexpr int VK_RCONTROL = 0xA3;
inline constexpr int VK_LMENU = 0xA4;
inline constexpr int VK_RMENU = 0xA5;
inline constexpr int VK_OEM_1 = 0xBA;      // ;:
inline constexpr int VK_OEM_PLUS = 0xBB;   // =+
inline constexpr int VK_OEM_COMMA = 0xBC;  // ,<
inline constexpr int VK_OEM_MINUS = 0xBD;  // -_
inline constexpr int VK_OEM_PERIOD = 0xBE; // .>
inline constexpr int VK_OEM_2 = 0xBF;      // /?
inline constexpr int VK_OEM_3 = 0xC0;      // `~
inline constexpr int VK_OEM_4 = 0xDB;      // [{
inline constexpr int VK_OEM_5 = 0xDC;      // \|
inline constexpr int VK_OEM_6 = 0xDD;      // ]}
inline constexpr int VK_OEM_7 = 0xDE;      // '"
inline constexpr int VK_OEM_102 = 0xE2;    // <> (the key left of Z on European keyboards)

/// <summary>
/// A Win32/COM GUID (16 bytes). The game identifies DirectPlay sessions and applications by them, stores them in
/// save data and sends them over the network, so it keeps the original layout.
/// </summary>
struct _GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

static_assert(sizeof(_GUID) == 16);
using GUID = _GUID;

/// <summary>A Win32 rectangle (<c>RECT</c>): the game keeps hot spots and window frames in them.</summary>
struct tagRECT
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

static_assert(sizeof(tagRECT) == 16);
using RECT = tagRECT;

/// <summary>A Win32 point (<c>POINT</c>).</summary>
struct tagPOINT
{
    int32_t x;
    int32_t y;
};

static_assert(sizeof(tagPOINT) == 8);
using POINT = tagPOINT;

/// <summary>Win32 <c>PtInRect</c>: whether the point lies in the rectangle (right and bottom edges excluded).</summary>
inline int PtInRect(const RECT& rect, POINT point)
{
    return (point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom) ? 1 : 0;
}

/// <summary>
/// Win32 <c>IntersectRect</c>: the overlap of two rectangles into <paramref name="dest"/>; 0 (and an empty
/// <paramref name="dest"/>) when they don't overlap.
/// </summary>
inline int IntersectRect(RECT& dest, const RECT& first, const RECT& second)
{
    RECT overlap{first.left > second.left ? first.left : second.left, first.top > second.top ? first.top : second.top,
                 first.right < second.right ? first.right : second.right,
                 first.bottom < second.bottom ? first.bottom : second.bottom};

    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom)
    {
        dest = RECT{};
        return 0;
    }

    dest = overlap;
    return 1;
}

/// <summary>
/// A Win32 date and time (<c>SYSTEMTIME</c>). The game times its logistics phase with them; the platform layer fills
/// them from the system clock.
/// </summary>
struct _SYSTEMTIME
{
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
};

static_assert(sizeof(_SYSTEMTIME) == 16);
using SYSTEMTIME = _SYSTEMTIME;

/// <summary>
/// A wave file's format chunk (<c>WAVEFORMATEX</c>): the sound renderer reads it from RIFF WAVE files and takes the
/// sample rate and bits per sample from it.
/// </summary>
#pragma pack(push, 1)
struct tWAVEFORMATEX
{
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
};
#pragma pack(pop)
static_assert(sizeof(tWAVEFORMATEX) == 18);
using WAVEFORMATEX = tWAVEFORMATEX;
