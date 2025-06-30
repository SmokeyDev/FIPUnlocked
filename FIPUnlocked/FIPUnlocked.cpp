// --- GLOBALS (must be before any function that uses them) ---
volatile bool g_shouldExit = false;

#include <iostream>
#include <windows.h>
#include <wchar.h>
#include "DirectOutput.h"
#include "resource.h"
#include <fstream>
#include <cstdint>
#include <vector>
#include <cairo.h>
#include <cairo-win32.h>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <random>
#include <algorithm>
#undef max
#undef min
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <csignal>
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <conio.h>
#include <commctrl.h>
#include <richedit.h>
#include <thread>
#include <mutex>
#include <queue>
#include <sstream>

#pragma comment(lib, "comctl32.lib")

#define ID_EDIT_LOG 1001
#define ID_BUTTON_CLEAR 1002
#define ID_BUTTON_CONFIG 1003
#define ID_BUTTON_SETTINGS 1004
#define ID_BUTTON_EXIT 1005
#define ID_CHECKBOX_PREVIEW 2001
#define WM_APPEND_LOG (WM_APP + 1)

struct LogMsgStruct {
    std::wstring message;
    ULONGLONG timestamp = 0;
};

HWND g_hMainWindow = NULL;
HWND g_hLogEdit = NULL;
HWND g_hClearButton = NULL;
HWND g_hConfigButton = NULL;
HWND g_hSettingsButton = NULL;
HWND g_hExitButton = NULL;
HWND g_hPreview = NULL;
HBITMAP g_hPreviewBitmap = NULL;
std::mutex g_logMutex;
std::queue<LogMsgStruct> g_logQueue;
std::thread g_guiThread;

std::mutex g_previewMutex;
HBITMAP g_hPreviewDIB = NULL;
void* g_previewBits = NULL;
ULONGLONG g_lastPreviewUpdate = 0;
const ULONGLONG PREVIEW_UPDATE_INTERVAL = 33;

// Function for safely adding logs to the queue.
void QueueLogMessage(const std::wstring& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    LogMsgStruct logMsg;
    logMsg.message = message;
    // Use current local time for timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    logMsg.timestamp = (ULONGLONG)st.wHour * 3600000 + (ULONGLONG)st.wMinute * 60000 + (ULONGLONG)st.wSecond * 1000 + (ULONGLONG)st.wMilliseconds;
    g_logQueue.push(logMsg);
    
    if (g_hMainWindow) {
        PostMessage(g_hMainWindow, WM_APPEND_LOG, 0, 0);
    }
}

// Function for processing the log queue in the GUI thread.
void ProcessLogQueue() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    while (!g_logQueue.empty()) {
        LogMsgStruct logMsg = g_logQueue.front();
        g_logQueue.pop();

        // Use SYSTEMTIME for actual wall clock time
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t timestamp[32];
        swprintf_s(timestamp, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        std::wstring fullMessage = timestamp + logMsg.message + L"\r\n";
        
        if (g_hLogEdit) {
            int textLen = GetWindowTextLength(g_hLogEdit);
            SendMessage(g_hLogEdit, EM_SETSEL, textLen, textLen);
            SendMessage(g_hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)fullMessage.c_str());
            textLen = GetWindowTextLength(g_hLogEdit);
            SendMessage(g_hLogEdit, EM_SETSEL, textLen, textLen);
            SendMessage(g_hLogEdit, EM_SCROLLCARET, 0, 0);
            // Force scroll bar to bottom and refresh the control
            SendMessage(g_hLogEdit, WM_VSCROLL, SB_BOTTOM, 0);
            UpdateWindow(g_hLogEdit);
        }
    }
}

// Window procedure for the main application window.
LRESULT CALLBACK MainWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Create GUI controls.
            g_hLogEdit = CreateWindowEx(
                WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                10, 10, 480, 400, hWnd, (HMENU)ID_EDIT_LOG, GetModuleHandle(NULL), NULL
            );
            // Add checkbox for rendering preview
            HWND hPreviewCheckbox = CreateWindowEx(
                0, L"BUTTON", L"Show Preview",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                500, 60, 120, 24, hWnd, (HMENU)ID_CHECKBOX_PREVIEW, GetModuleHandle(NULL), NULL
            );
            SendMessage(hPreviewCheckbox, BM_SETCHECK, BST_UNCHECKED, 0); // Default is enabled
            // Preview window to the right of log (320x240, centered vertically, does not overlap log)
            g_hPreview = CreateWindowEx(
                WS_EX_CLIENTEDGE, L"STATIC", NULL,
                WS_CHILD | WS_VISIBLE | SS_BITMAP,
                630, 60, 320, 240, hWnd, NULL, GetModuleHandle(NULL), NULL
            );
            g_hClearButton = CreateWindow(
                L"BUTTON", L"Clear Log",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 420, 100, 30, hWnd, (HMENU)ID_BUTTON_CLEAR, GetModuleHandle(NULL), NULL
            );
            // g_hConfigButton = CreateWindow(
            //     L"BUTTON", L"Config Editor",
            //     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            //     120, 420, 100, 30, hWnd, (HMENU)ID_BUTTON_CONFIG, GetModuleHandle(NULL), NULL
            // );
            // g_hSettingsButton = CreateWindow(
            //     L"BUTTON", L"Settings",
            //     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            //     230, 420, 100, 30, hWnd, (HMENU)ID_BUTTON_SETTINGS, GetModuleHandle(NULL), NULL
            // );
            g_hExitButton = CreateWindow(
                L"BUTTON", L"Exit",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                690, 420, 100, 30, hWnd, (HMENU)ID_BUTTON_EXIT, GetModuleHandle(NULL), NULL
            );
            HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
            SendMessage(g_hLogEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            QueueLogMessage(L"=== FIPUnlocked ===");
            QueueLogMessage(L"GUI mode initialized. Application ready.");
            ProcessLogQueue();
            break;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_BUTTON_CLEAR:
                    // Clear the log.
                    SetWindowText(g_hLogEdit, L"");
                    QueueLogMessage(L"Log cleared.");
                    break;
                    
                case ID_BUTTON_CONFIG:
                    // TODO: Open config editor.
                    QueueLogMessage(L"Config editor - coming soon!");
                    break;
                    
                case ID_BUTTON_SETTINGS:
                    // TODO: Open settings.
                    QueueLogMessage(L"Settings - coming soon!");
                    break;
                    
                case ID_BUTTON_EXIT:
                    // Close the application.
                    g_shouldExit = true;
                    PostMessage(hWnd, WM_CLOSE, 0, 0);
                    break;
                case ID_CHECKBOX_PREVIEW:
                    // Show/hide preview based on checkbox
                    if (g_hPreview) {
                        BOOL checked = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        ShowWindow(g_hPreview, checked ? SW_SHOW : SW_HIDE);
                        // Reset preview update timer to immediately update when enabled
                        if (checked) {
                            g_lastPreviewUpdate = 0; // Force immediate update
                        }
                    }
                    break;
            }
            break;
        }
        
        case WM_APPEND_LOG:
            // Process log queue
            ProcessLogQueue();
            break;
        
        case WM_SIZE:
            // Adjust controls' size and position to not overlap
            if (g_hLogEdit && g_hPreview) {
                RECT clientRect;
                GetClientRect(hWnd, &clientRect);
                int margin = 20;
                int previewWidth = 320;
                int previewHeight = 240;
                int previewAreaWidth = previewWidth + margin;
                int logWidth = clientRect.right - clientRect.left - previewAreaWidth - margin;
                if (logWidth < 200) logWidth = 200; // minimal log width
                int logHeight = clientRect.bottom - clientRect.top - 80;
                if (logHeight < 100) logHeight = 100;
                SetWindowPos(g_hLogEdit, NULL, 10, 10, logWidth, logHeight, SWP_NOZORDER);
                // Preview to the right of log
                int previewX = 10 + logWidth + margin;
                int previewY = 60;
                SetWindowPos(g_hPreview, NULL, previewX, previewY, previewWidth, previewHeight, SWP_NOZORDER);
                // Checkbox above preview
                HWND hPreviewCheckbox = GetDlgItem(hWnd, ID_CHECKBOX_PREVIEW);
                if (hPreviewCheckbox) {
                    SetWindowPos(hPreviewCheckbox, NULL, previewX, previewY - 30, 120, 24, SWP_NOZORDER);
                }
                // Move buttons to bottom, spaced out
                int buttonY = clientRect.bottom - 50;
                SetWindowPos(g_hClearButton, NULL, 10, buttonY, 100, 30, SWP_NOZORDER);
                SetWindowPos(g_hConfigButton, NULL, 120, buttonY, 100, 30, SWP_NOZORDER);
                SetWindowPos(g_hSettingsButton, NULL, 230, buttonY, 100, 30, SWP_NOZORDER);
                SetWindowPos(g_hExitButton, NULL, clientRect.right - 110, buttonY, 100, 30, SWP_NOZORDER);
            }
            break;
        
        case WM_CLOSE:
            // Close the application.
            g_shouldExit = true;
            DestroyWindow(hWnd);
            break;
        
        case WM_DESTROY:
            if (g_hPreviewBitmap) {
                DeleteObject(g_hPreviewBitmap);
                g_hPreviewBitmap = NULL;
            }
            // Clean up preview resources
            if (g_hPreviewDIB) {
                DeleteObject(g_hPreviewDIB);
                g_hPreviewDIB = NULL;
                g_previewBits = NULL;
            }
            PostQuitMessage(0);
            break;
        
        case WM_GETMINMAXINFO: {
            // Set minimum window size to 720x420px
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = 734;
            mmi->ptMinTrackSize.y = 427;
            break;
        }
        
        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Function for initializing GUI.
bool InitializeGUI() {
    // Load RichEdit (Msftedit.dll) before creating control
    LoadLibrary(L"Msftedit.dll");
    // Register window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"FIPUnlockedMainWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    
    if (!RegisterClassEx(&wc)) {
        return false;
    }
    
    // Create main window
    g_hMainWindow = CreateWindowEx(
        0, L"FIPUnlockedMainWindow", L"FIPUnlocked",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 500,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    if (!g_hMainWindow) {
        return false;
    }
    
    // Show window
    ShowWindow(g_hMainWindow, SW_SHOW);
    UpdateWindow(g_hMainWindow);
    
    return true;
}

// Function for running GUI message loop in a separate thread
void GUIMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// Function for logging in both console and GUI mode
void LogMessage(const std::wstring& message) {
    QueueLogMessage(message);
}

// Function for logging with formatting printf
void LogMessageFormatted(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer, format, args);
    va_end(args);
    LogMessage(std::wstring(buffer));
}

// Maximum number of FIP pages limited to 6
constexpr int MAX_PAGES = 6;

// Configuration structures for JSON
struct CaptureRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class ScaleMode {
    Nearest,
    Bilinear
};

struct PageConfig {
    std::string name;
    CaptureRegion capture_region = {};
    ScaleMode scale_mode = ScaleMode::Nearest;
};

// Extend AppConfig to support button mappings
struct AppConfig {
    std::vector<PageConfig> pages;
    double target_fps = 0.0;
    bool debug = false;
    bool show_fps = false;
    bool show_screen_names = false;
    std::map<std::string, std::string> button_mappings; // S1-S6 to key combo
};

// Global variables for configuration
AppConfig g_config;
bool g_configLoaded = false;

void signalHandler(int signal) {
    if (signal == SIGINT) {
        g_shouldExit = true;
    }
}

// Helper struct for parsed key token
struct ParsedKey {
    bool isModifier;
    WORD vkCode;
};

struct KeyCombo {
    std::vector<WORD> modifiers; // e.g. VK_LSHIFT, VK_RCONTROL
    WORD mainKey;
    std::string combo;
    KeyCombo() : mainKey(0) {}
};

// Parse a single key token into a ParsedKey (modifier or main key)
ParsedKey parseKeyToken(const std::string& token) {
    ParsedKey pk = {false, 0};
    if (token == "LShift") { pk.isModifier = true; pk.vkCode = VK_LSHIFT; }
    else if (token == "RShift") { pk.isModifier = true; pk.vkCode = VK_RSHIFT; }
    else if (token == "LControl" || token == "LCtrl") { pk.isModifier = true; pk.vkCode = VK_LCONTROL; }
    else if (token == "RControl" || token == "RCtrl") { pk.isModifier = true; pk.vkCode = VK_RCONTROL; }
    else if (token == "LAlt") { pk.isModifier = true; pk.vkCode = VK_LMENU; }
    else if (token == "RAlt") { pk.isModifier = true; pk.vkCode = VK_RMENU; }
    else if (token == "Escape" || token == "Esc") { pk.isModifier = false; pk.vkCode = VK_ESCAPE; }
    else if (token == "Space") { pk.isModifier = false; pk.vkCode = VK_SPACE; }
    else if (token == "Enter") { pk.isModifier = false; pk.vkCode = VK_RETURN; }
    else if (token == "ArrowUp" || token == "Up") { pk.isModifier = false; pk.vkCode = VK_UP; }
    else if (token == "ArrowDown" || token == "Down") { pk.isModifier = false; pk.vkCode = VK_DOWN; }
    else if (token == "ArrowLeft" || token == "Left") { pk.isModifier = false; pk.vkCode = VK_LEFT; }
    else if (token == "ArrowRight" || token == "Right") { pk.isModifier = false; pk.vkCode = VK_RIGHT; }
    else if (token.length() == 1) { pk.isModifier = false; pk.vkCode = VkKeyScanA(token[0]) & 0xFF; }
    else if (token.length() > 1 && token[0] == 'F' && isdigit(token[1])) { pk.isModifier = false; pk.vkCode = VK_F1 + (std::stoi(token.substr(1)) - 1); }
    // Add more keys as needed
    return pk;
}

// Parse a key combination string like "LShift+H", "RCtrl+LAlt+E", "Escape", "Space", "Enter", "ArrowUp", or "F1"
KeyCombo parseKeyCombo(const std::string& combo) {
    KeyCombo result;
    result.combo = combo;
    size_t start = 0, end = 0;
    while ((end = combo.find('+', start)) != std::string::npos) {
        std::string token = combo.substr(start, end - start);
        ParsedKey pk = parseKeyToken(token);
        if (pk.isModifier) {
            result.modifiers.push_back(pk.vkCode);
        }
        // If not a modifier, ignore (main key should be last token)
        start = end + 1;
    }
    std::string last = combo.substr(start);
    if (!last.empty()) {
        ParsedKey pk = parseKeyToken(last);
        if (pk.isModifier) {
            result.modifiers.push_back(pk.vkCode);
        } else if (pk.vkCode != 0) {
            result.mainKey = pk.vkCode;
        }
    }
    return result;
}

// Store parsed combos for S1-S6
std::map<int, KeyCombo> g_fipButtonCombos;
// Add support for rotary encoders
std::map<std::string, KeyCombo> g_rotaryEncoderCombos;
DWORD g_prevSoftButtonState = 0;

// Typedef and global for soft button callback registration
// According to DirectOutput SDK, signature is:
// HRESULT DirectOutput_RegisterSoftButtonCallback(void* hDevice, Pfn_DirectOutput_SoftButtonCallback pfnCb, void* pCtxt);
typedef HRESULT(__stdcall* Pfn_DirectOutput_RegisterSoftButtonCallback)(void*, void(__stdcall*)(void*, DWORD, void*), void*);
Pfn_DirectOutput_RegisterSoftButtonCallback g_pfnRegisterSoftButtonCallback = NULL;

// Typedef and global for LED control
// HRESULT DirectOutput_SetLed(void* hDevice, DWORD dwPage, DWORD dwIndex, DWORD dwValue);
typedef HRESULT(__stdcall* Pfn_DirectOutput_SetLed)(void*, DWORD, DWORD, DWORD);
Pfn_DirectOutput_SetLed g_pfnSetLed = NULL;

// Typedef and global for page removal
// HRESULT DirectOutput_RemovePage(void* hDevice, DWORD dwPage);
typedef HRESULT(__stdcall* Pfn_DirectOutput_RemovePage)(void*, DWORD);
Pfn_DirectOutput_RemovePage g_pfnRemovePage = NULL;

// Function for loading configuration from JSON file
bool LoadConfiguration(const std::string& configFile) {
    try {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            LogMessageFormatted(L"Could not open configuration file: %s", std::wstring(configFile.begin(), configFile.end()).c_str());
            return false;
        }

        nlohmann::json j;
        file >> j;

        g_config.target_fps = j.value("target_fps", 30.0);
        g_config.debug = j.value("debug", false);
        g_config.show_fps = j.value("show_fps", false);
        g_config.show_screen_names = j.value("show_screen_names", false);

        // Parse button mappings
        if (j.contains("button_mappings")) {
            nlohmann::json& bm = j["button_mappings"];
            for (nlohmann::json::iterator it = bm.begin(); it != bm.end(); ++it) {
                g_config.button_mappings[it.key()] = it.value();
            }
        }
        // Parse and store combos for S1-S6
        g_fipButtonCombos.clear();
        for (int i = 0; i < 6; ++i) {
            std::string sx = "S" + std::to_string(i+1);
            std::map<std::string, std::string>::iterator it = g_config.button_mappings.find(sx);
            if (it != g_config.button_mappings.end() && !it->second.empty()) {
                g_fipButtonCombos[i] = parseKeyCombo(it->second);
            }
        }
        
        // Parse and store combos for rotary encoders
        g_rotaryEncoderCombos.clear();
        std::vector<std::string> rotaryKeys = {"LeftRotaryPlus", "LeftRotaryMinus", "RightRotaryPlus", "RightRotaryMinus"};
        for (const auto& key : rotaryKeys) {
            auto it = g_config.button_mappings.find(key);
            if (it != g_config.button_mappings.end() && !it->second.empty()) {
                g_rotaryEncoderCombos[key] = parseKeyCombo(it->second);
                if (g_config.debug) {
                    LogMessageFormatted(L"Rotary encoder %s mapped to: %s", 
                        std::wstring(key.begin(), key.end()).c_str(),
                        std::wstring(g_rotaryEncoderCombos[key].combo.begin(), g_rotaryEncoderCombos[key].combo.end()).c_str());
                }
            }
        }

        auto pagesArray = j["pages"];
        int pageCount = std::min(static_cast<int>(pagesArray.size()), MAX_PAGES);
        
        if (pagesArray.size() > MAX_PAGES) {
            LogMessageFormatted(L"Warning: Config contains %d pages, but only %d will be used.", pagesArray.size(), MAX_PAGES);
        }

        g_config.pages.clear();
        g_config.pages.reserve(pageCount);

        for (int i = 0; i < pageCount; ++i) {
            const auto& pageJson = pagesArray[i];
            PageConfig page;

            page.name = pageJson["name"];
            const auto& region = pageJson["capture_region"];
            page.capture_region.x = region["x"];
            page.capture_region.y = region["y"];
            page.capture_region.width = region["width"];
            page.capture_region.height = region["height"];

            std::string scaleMode = pageJson["scale_mode"];
            if (scaleMode == "bilinear") {
                page.scale_mode = ScaleMode::Bilinear;
            } else {
                page.scale_mode = ScaleMode::Nearest;
            }

            g_config.pages.push_back(page);
        }

        LogMessageFormatted(L"Configuration loaded successfully. Page count: %d", g_config.pages.size());
        return true;
    }
    catch (const std::exception& e) {
        LogMessageFormatted(L"Error loading configuration: %s", std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        return false;
    }
}

// Manual clamp for pre-C++17 compatibility
inline unsigned char clampToByte(float v) {
    return (unsigned char)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
}

// Forward declaration for UpdatePreviewBitmap to fix C3861 errors
void UpdatePreviewBitmap(const std::vector<unsigned char>& buffer);

// Declaration of screenshot function for FIP
void captureScreenRegionToFIPBuffer(int x, int y, int w, int h, std::vector<unsigned char>& outBuffer, const wchar_t* overlayText, ScaleMode scaleMode);

// Forward declaration for FIP soft button callback
void __stdcall SoftButtonCallback(void* hDevice, DWORD dwButtons, void* pCtxt);

// Forward declaration for page cleanup
void cleanupDevicePages(void* hDevice);

// Class for managing rendering of instruments on FIP
class FIPDisplay {
private:
    cairo_surface_t* surface;
    cairo_t* cr;
    std::vector<unsigned char> buffer;
    const int width = 320;
    const int height = 240;
    
public:
    FIPDisplay() : surface(nullptr), cr(nullptr) {
        // Initialize Cairo buffer for FIP (320x240, 24-bit RGB)
        buffer.resize(width * height * 4); // BGRA format for Cairo
        surface = cairo_image_surface_create_for_data(
            buffer.data(), 
            CAIRO_FORMAT_ARGB32, 
            width, 
            height, 
            width * 4
        );
        cr = cairo_create(surface);
        
        // Set transformation for FIP (origin in top-left corner)
        cairo_translate(cr, width/2, height/2); // Centrum ekranu
    }
    
    ~FIPDisplay() {
        if (cr) cairo_destroy(cr);
        if (surface) cairo_surface_destroy(surface);
    }
    
    // Przeciążenie: aktualizuje FIP z gotowego bufora (np. screenshot)
    HRESULT updateFIP(void* hDevice, DWORD dwPage, Pfn_DirectOutput_SetImage setImageFunc, const std::vector<unsigned char>& buf) {
        HRESULT hr = setImageFunc(hDevice, dwPage, 0, (DWORD)buf.size(), buf.data());
        if (SUCCEEDED(hr)) {
            return hr;
        } else {
            if (g_config.debug) {
                LogMessageFormatted(L"Error updating FIP: 0x%08X", hr);
            }
            return hr;
        }
    }
};

// Definitions for pointers to functions from DirectOutput.dll
typedef HRESULT(__stdcall* Pfn_DirectOutput_Initialize)(const wchar_t* wszPluginName);
typedef HRESULT(__stdcall* Pfn_DirectOutput_Deinitialize)();
typedef HRESULT(__stdcall* Pfn_DirectOutput_RegisterDeviceCallback)(Pfn_DirectOutput_DeviceChange pfnCb, void* pCtxt);
typedef HRESULT(__stdcall* Pfn_DirectOutput_Enumerate)(Pfn_DirectOutput_EnumerateCallback pfnCb, void* pCtxt);
typedef HRESULT(__stdcall* Pfn_DirectOutput_AddPage)(void* hDevice, DWORD dwPage, const wchar_t* wszDebugName, DWORD dwFlags);
typedef HRESULT(__stdcall* Pfn_DirectOutput_SetString)(void* hDevice, DWORD dwPage, DWORD dwIndex, DWORD cchValue, const wchar_t* wszValue);
typedef HRESULT(__stdcall* Pfn_DirectOutput_GetDeviceType)(void* hDevice, LPGUID pGuid);
typedef HRESULT(__stdcall* Pfn_DirectOutput_SetImageFromFile)(void* hDevice, DWORD dwPage, DWORD dwIndex, DWORD cchFilename, const wchar_t* wszFilename);
typedef HRESULT(__stdcall* Pfn_DirectOutput_SetImage)(void* hDevice, DWORD dwPage, DWORD dwIndex, DWORD cbValue, const void* pvValue);
typedef void(__stdcall* Pfn_DirectOutput_PageCallback)(void* hDevice, DWORD dwPage, bool bSetActive, void* pCtxt);
typedef HRESULT(__stdcall* Pfn_DirectOutput_RegisterPageCallback)(void* hDevice, Pfn_DirectOutput_PageCallback pfnCb, void* pCtxt);

// Global variables for storing pointers to functions from DLL
HMODULE g_hDirectOutput = NULL;
Pfn_DirectOutput_Initialize g_pfnInitialize = NULL;
Pfn_DirectOutput_Deinitialize g_pfnDeinitialize = NULL;
Pfn_DirectOutput_RegisterDeviceCallback g_pfnRegisterDeviceCallback = NULL;
Pfn_DirectOutput_Enumerate g_pfnEnumerate = NULL;
Pfn_DirectOutput_AddPage g_pfnAddPage = NULL;
Pfn_DirectOutput_SetString g_pfnSetString = NULL;
Pfn_DirectOutput_GetDeviceType g_pfnGetDeviceType = NULL;
Pfn_DirectOutput_SetImageFromFile g_pfnSetImageFromFile = NULL;
Pfn_DirectOutput_SetImage g_pfnSetImage = NULL;
Pfn_DirectOutput_RegisterPageCallback g_pfnRegisterPageCallback = NULL;

// Global variables for storing device information
void* g_hDevice = NULL;
DWORD g_dwPage = 1;
bool g_bDeviceFound = false;

// Global variable to track the active page
DWORD g_activePage = 2;

// Global vector to track created page IDs for cleanup on disconnect
std::vector<DWORD> g_createdPageIds;

// Page callback to track active page changes
void __stdcall PageCallback(void* hDevice, DWORD dwPage, bool bSetActive, void* pCtxt) {
    if (g_config.debug) {
        LogMessage(L"PageCallback triggered!");
    }
    if (bSetActive) {
        g_activePage = dwPage;
        if (g_config.debug) {
            LogMessageFormatted(L"User switched to page %d (now active)", dwPage);
        }
    }
}

void __stdcall LightUpButtons(void* hDevice) {
        // Reapply LED illumination for new active page
        if (g_pfnSetLed) {
            for (int i = 0; i < 6; ++i) {
                std::string sx = "S" + std::to_string(i+1);
                auto it = g_config.button_mappings.find(sx);
                DWORD ledValue = (it != g_config.button_mappings.end() && !it->second.empty()) ? 1 : 0;
                HRESULT hrLed = g_pfnSetLed(hDevice, g_activePage, i+1, ledValue);
                if (g_config.debug) {
                    LogMessageFormatted(L"[PageCallback] Set LED for S%d (index %d, page %d) to %d (HRESULT=0x%08X)", i+1, i+1, g_activePage, ledValue, hrLed);
                }
            }
        }
        // End LED logic
}

// Callback called when device is added or removed
void __stdcall DeviceChangeCallback(void* hDevice, bool bAdded, void* pCtxt)
{
    if (bAdded) 
    {
        LogMessage(L"FIP device detected!");
        g_hDevice = hDevice;
        g_bDeviceFound = true;

        if (g_pfnRegisterSoftButtonCallback) {
            HRESULT hrSoftBtn = g_pfnRegisterSoftButtonCallback(hDevice, SoftButtonCallback, nullptr);
            if (!SUCCEEDED(hrSoftBtn)) {
                LogMessage(L"Failed to register soft button callback!");
            }
        } else {
            LogMessage(L"Cannot register soft button callback!");
        }

        // Check if configuration has been loaded
        if (!g_configLoaded || g_config.pages.empty()) {
            LogMessage(L"Error: No configuration or no pages to display!");
            return;
        }

        // Add pages based on configuration
        std::vector<DWORD> pageIds;
        bool allPagesAdded = true;

        for (size_t i = 0; i < g_config.pages.size(); ++i) {
            DWORD pageId = static_cast<DWORD>(i + 1);
            const auto& pageConfig = g_config.pages[i];
            
            // Convert page name to wchar_t*
            std::wstring wName(pageConfig.name.begin(), pageConfig.name.end());
            
            HRESULT hr = g_pfnAddPage(hDevice, pageId, wName.c_str(), 
                (i == 0) ? FLAG_SET_AS_ACTIVE : 0); // First page as active
            
            if (SUCCEEDED(hr)) {
                pageIds.push_back(pageId);
                // Track created page for cleanup
                g_createdPageIds.push_back(pageId);
                if (g_config.debug) {
                    LogMessageFormatted(L"Page %d (%s) added successfully.", pageId, wName.c_str());
                }
            } else {
                LogMessageFormatted(L"Error adding page %d: 0x%08X", pageId, hr);
                allPagesAdded = false;
                break;
            }
        }

        if (!allPagesAdded) {
            LogMessage(L"Not all pages were added. Initialization aborted.");
            return;
        }

        // Set FIP button LEDs based on config
        LightUpButtons(hDevice);

        // Check device type and start rendering loop
        if (g_pfnGetDeviceType) {
            GUID guid = {0};
            HRESULT hrType = g_pfnGetDeviceType(hDevice, &guid);
            extern const GUID DeviceType_Fip;
            if (SUCCEEDED(hrType)) {
                if (memcmp(&guid, &DeviceType_Fip, sizeof(GUID)) == 0) {
                    LogMessage(L"FIP Detected! Starting rendering loop...");
                    
                    FIPDisplay fipDisplay;
                    LARGE_INTEGER freq, t0, t1;
                    if (g_config.show_fps) {
                        QueryPerformanceFrequency(&freq);
                    }
                    
                    // Initialize FPS vectors for each page
                    std::vector<std::vector<double>> frameTimes(g_config.pages.size());
                    for (auto& ft : frameTimes) {
                        ft.reserve(60); // Pre-allocate space for 2 seconds at 30 FPS
                    }
                    
                    if (g_config.show_fps) {
                        QueryPerformanceCounter(&t0);
                    }
                    if (g_config.debug) {
                        LogMessage(L"FIP screenshot test running. Press Ctrl+C to exit.");
                    }
                    
                    while (!g_shouldExit) {
                        if (g_config.show_fps) {
                            QueryPerformanceCounter(&t1);
                        }
                        double frameTime = 0.0;
                        if (g_config.show_fps) {
                        double now = double(t1.QuadPart) / freq.QuadPart;
                        double prev = double(t0.QuadPart) / freq.QuadPart;
                        frameTime = now - prev;
                        t0 = t1;

                            // Update FPS for all pages
                            for (size_t i = 0; i < g_config.pages.size(); ++i) {
                                frameTimes[i].push_back(now);
                                // Remove old frames (older than 2 seconds)
                                while (!frameTimes[i].empty() && now - frameTimes[i].front() > 2.0) {
                                    frameTimes[i].erase(frameTimes[i].begin());
                                }
                            }
                        }


                        // Capture screen for active page
                        if (g_activePage > 0 && g_activePage <= static_cast<DWORD>(g_config.pages.size())) {
                            size_t pageIndex = g_activePage - 1;
                            const auto& pageConfig = g_config.pages[pageIndex];
                            
                            // Calculate FPS for active page
                            double avgFps = 0.0;
                            if (frameTimes[pageIndex].size() > 1) {
                                avgFps = (frameTimes[pageIndex].size() - 1) / 
                                        (frameTimes[pageIndex].back() - frameTimes[pageIndex].front());
                            }
                            
                            // Create text overlay with FPS
                            wchar_t overlay[128];
                            if (g_config.show_screen_names && g_config.show_fps) {
                                swprintf(overlay, 128, L"FPS: %.1f | %s", avgFps, 
                                        std::wstring(pageConfig.name.begin(), pageConfig.name.end()).c_str());
                            } else if (g_config.show_screen_names) {
                                swprintf(overlay, 128, L"%s", 
                                        std::wstring(pageConfig.name.begin(), pageConfig.name.end()).c_str());
                            } else if (g_config.show_fps) {
                                swprintf(overlay, 128, L"FPS: %.1f", avgFps);
                            } else {
                                swprintf(overlay, 128, L"");
                            }
                            
                            // Capture screen with page configuration
                            std::vector<unsigned char> buffer;
                            captureScreenRegionToFIPBuffer(
                                pageConfig.capture_region.x,
                                pageConfig.capture_region.y,
                                pageConfig.capture_region.width,
                                pageConfig.capture_region.height,
                                buffer,
                                overlay,
                                pageConfig.scale_mode
                            );
                            
                            // Update FIP with intelligent page detection
                            UpdatePreviewBitmap(buffer);
                            HRESULT hr = fipDisplay.updateFIP(hDevice, g_activePage, g_pfnSetImage, buffer);
                            if (FAILED(hr)) {
                                if (g_config.debug) {
                                    LogMessageFormatted(L"Update failed for page %d. Probing all pages...", g_activePage);
                                }
                                
                                // Try all pages to find active
                                bool foundActive = false;
                                for (size_t i = 0; i < g_config.pages.size(); ++i) {
                                    DWORD testPage = static_cast<DWORD>(i + 1);
                                    const auto& testConfig = g_config.pages[i];
                                    
                                    std::vector<unsigned char> testBuffer;
                                    captureScreenRegionToFIPBuffer(
                                        testConfig.capture_region.x,
                                        testConfig.capture_region.y,
                                        testConfig.capture_region.width,
                                        testConfig.capture_region.height,
                                        testBuffer,
                                        overlay,
                                        testConfig.scale_mode
                                    );
                                    
                                    UpdatePreviewBitmap(testBuffer);
                                    HRESULT testHr = fipDisplay.updateFIP(hDevice, testPage, g_pfnSetImage, testBuffer);
                                    if (SUCCEEDED(testHr)) {
                                        g_activePage = testPage;
                                        LogMessageFormatted(L"Page %d is now considered active.", testPage);
                                        foundActive = true;
                                        LightUpButtons(hDevice);
                                        break;
                                    }
                                }
                                
                                if (!foundActive) {
                                    if (g_config.debug) {
                                        LogMessage(L"Failed to update any page!");
                                    }
                                }
                            }
                        }
                        
                        // Control FPS
                        double frameTimeMs = frameTime * 1000.0;
                        int sleepMs = std::max(0, int((1000.0 / g_config.target_fps) - frameTimeMs));
                        Sleep(sleepMs);
                    }
                    
                    if (g_config.debug) {
                        LogMessage(L"Ctrl+C received. Exiting...");
                    }
                    return;
                } else {
                    LogMessage(L"FIP was NOT detected! Aborting...");
                    return;
                }
            } else {
                LogMessage(L"Failed to get device type!");
                return;
            }
        }
    }
    else
    {
        LogMessage(L"FIP device disconnected. Cleaning up pages...");
        
        // Clean up created pages before clearing device state
        cleanupDevicePages(hDevice);
        
        LogMessage(L"FIP device disconnected. Waiting for reconnection...");
        g_hDevice = NULL;
        g_bDeviceFound = false;
    }
}

// Callback called during device enumeration
void __stdcall EnumerateCallback(void* hDevice, void* pCtxt)
{
    if (g_config.debug) {
        LogMessage(L"Device found during enumeration.");
    }
    DeviceChangeCallback(hDevice, true, pCtxt);
}

// Function for dynamic loading of functions from DirectOutput.dll
bool LoadDirectOutputFunctions()
{
    // Load DirectOutput.dll
    g_hDirectOutput = LoadLibrary(L"DirectOutput.dll");
    if (!g_hDirectOutput)
    {
        LogMessage(L"Error: Could not load DirectOutput.dll!");
        return false;
    }

    // Get pointers to functions from DLL
    g_pfnInitialize = (Pfn_DirectOutput_Initialize)GetProcAddress(g_hDirectOutput, "DirectOutput_Initialize");
    g_pfnDeinitialize = (Pfn_DirectOutput_Deinitialize)GetProcAddress(g_hDirectOutput, "DirectOutput_Deinitialize");
    g_pfnRegisterDeviceCallback = (Pfn_DirectOutput_RegisterDeviceCallback)GetProcAddress(g_hDirectOutput, "DirectOutput_RegisterDeviceCallback");
    g_pfnEnumerate = (Pfn_DirectOutput_Enumerate)GetProcAddress(g_hDirectOutput, "DirectOutput_Enumerate");
    g_pfnAddPage = (Pfn_DirectOutput_AddPage)GetProcAddress(g_hDirectOutput, "DirectOutput_AddPage");
    g_pfnSetString = (Pfn_DirectOutput_SetString)GetProcAddress(g_hDirectOutput, "DirectOutput_SetString");
    g_pfnGetDeviceType = (Pfn_DirectOutput_GetDeviceType)GetProcAddress(g_hDirectOutput, "DirectOutput_GetDeviceType");
    g_pfnSetImageFromFile = (Pfn_DirectOutput_SetImageFromFile)GetProcAddress(g_hDirectOutput, "DirectOutput_SetImageFromFile");
    g_pfnSetImage = (Pfn_DirectOutput_SetImage)GetProcAddress(g_hDirectOutput, "DirectOutput_SetImage");
    g_pfnRegisterPageCallback = (Pfn_DirectOutput_RegisterPageCallback)GetProcAddress(g_hDirectOutput, "DirectOutput_RegisterPageCallback");
    g_pfnRegisterSoftButtonCallback = (Pfn_DirectOutput_RegisterSoftButtonCallback)GetProcAddress(g_hDirectOutput, "DirectOutput_RegisterSoftButtonCallback");
    g_pfnSetLed = (Pfn_DirectOutput_SetLed)GetProcAddress(g_hDirectOutput, "DirectOutput_SetLed");
    g_pfnRemovePage = (Pfn_DirectOutput_RemovePage)GetProcAddress(g_hDirectOutput, "DirectOutput_RemovePage");

    // Check if all functions are loaded
    if (!g_pfnInitialize || !g_pfnDeinitialize || !g_pfnRegisterDeviceCallback || 
        !g_pfnEnumerate || !g_pfnAddPage || !g_pfnSetString || !g_pfnGetDeviceType || !g_pfnSetImageFromFile || !g_pfnSetImage)
    {
        LogMessage(L"Error: Could not load all functions from DirectOutput.dll!");
        FreeLibrary(g_hDirectOutput);
        g_hDirectOutput = NULL;
        return false;
    }
    if (!g_pfnRegisterSoftButtonCallback && g_config.debug) {
        LogMessage(L"Warning: DirectOutput_RegisterSoftButtonCallback is not available in this DLL. Soft button events will not be handled.");
    }
    if (g_config.debug) {
        if (g_pfnRegisterPageCallback && g_config.debug) {
            LogMessage(L"RegisterPageCallback is available.");
        } else {
            LogMessage(L"RegisterPageCallback is NOT available in this DLL.");
        }
    }
    if (!g_pfnSetLed && g_config.debug) {
        LogMessage(L"Warning: DirectOutput_SetLed is not available in this DLL. Button LEDs will not be controlled.");
    }
    if (!g_pfnRemovePage && g_config.debug) {
        LogMessage(L"Warning: DirectOutput_RemovePage is not available in this DLL. Pages will not be cleaned up on disconnect.");
    }

    LogMessage(L"DirectOutput.dll loaded successfully.");
    return true;
}

// Function for releasing DirectOutput resources
void UnloadDirectOutput()
{
    if (g_hDirectOutput)
    {
        if (g_pfnDeinitialize)
        {
            g_pfnDeinitialize();
        }
        FreeLibrary(g_hDirectOutput);
        g_hDirectOutput = NULL;
    }
}

// --- Image scaling functions ---
// Scales a 24bpp RGB buffer from srcW x srcH to dstW x dstH using nearest-neighbor
void scaleNearestNeighbor(const std::vector<unsigned char>& src, int srcW, int srcH, std::vector<unsigned char>& dst, int dstW, int dstH) {
    dst.resize(dstW * dstH * 3);
    for (int y = 0; y < dstH; ++y) {
        int srcY = y * srcH / dstH;
        for (int x = 0; x < dstW; ++x) {
            int srcX = x * srcW / dstW;
            int srcIdx = (srcY * srcW + srcX) * 3;
            int dstIdx = (y * dstW + x) * 3;
            dst[dstIdx + 0] = src[srcIdx + 0];
            dst[dstIdx + 1] = src[srcIdx + 1];
            dst[dstIdx + 2] = src[srcIdx + 2];
        }
    }
}

// Scales a 24bpp RGB buffer from srcW x srcH to dstW x dstH using bilinear interpolation
void scaleBilinear(const std::vector<unsigned char>& src, int srcW, int srcH, std::vector<unsigned char>& dst, int dstW, int dstH) {
    dst.resize(dstW * dstH * 3);
    for (int y = 0; y < dstH; ++y) {
        float srcY = (float)y * srcH / dstH;
        int y0 = (int)srcY;
        int y1 = std::min<int>(y0 + 1, srcH - 1);
        float fy = srcY - y0;
        for (int x = 0; x < dstW; ++x) {
            float srcX = (float)x * srcW / dstW;
            int x0 = (int)srcX;
            int x1 = std::min<int>(x0 + 1, srcW - 1);
            float fx = srcX - x0;
            for (int c = 0; c < 3; ++c) {
                float v00 = src[(y0 * srcW + x0) * 3 + c];
                float v01 = src[(y0 * srcW + x1) * 3 + c];
                float v10 = src[(y1 * srcW + x0) * 3 + c];
                float v11 = src[(y1 * srcW + x1) * 3 + c];
                float v0 = v00 + (v01 - v00) * fx;
                float v1 = v10 + (v11 - v10) * fx;
                float v = v0 + (v1 - v0) * fy;
                dst[(y * dstW + x) * 3 + c] = clampToByte(v);
            }
        }
    }
}

// Draw overlay text on a 320x240 RGB buffer using Cairo (robust BGRA temp buffer)
void drawOverlayTextOnFIPBuffer(std::vector<unsigned char>& rgbBuffer, const wchar_t* overlayText) {
    if (!overlayText) return;
    // Create a temporary 320x240x4 BGRA buffer for Cairo
    std::vector<unsigned char> tempBGRA(320 * 240 * 4, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(tempBGRA.data(), CAIRO_FORMAT_ARGB32, 320, 240, 320 * 4);
    cairo_t* cr = cairo_create(surface);
    // Transparent background (do not clear, just draw text)
    cairo_select_font_face(cr, "Consolas", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16);
    cairo_set_source_rgb(cr, 0, 1, 0); // Green
    cairo_move_to(cr, 4, 16);
    // Convert wchar_t* to UTF-8
    char utf8[128];
    size_t converted = 0;
    wcstombs_s(&converted, utf8, sizeof(utf8), overlayText, _TRUNCATE);
    cairo_show_text(cr, utf8);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    // Composite: copy RGB from BGRA temp buffer to FIP buffer (ignore alpha), flipping vertically
    for (int y = 0; y < 240; ++y) {
        int cairoY = 239 - y; // Flip vertically
        for (int x = 0; x < 320; ++x) {
            int idx3 = (y * 320 + x) * 3;
            int idx4 = (cairoY * 320 + x) * 4;
            // Simple alpha blend: if alpha > 0, overwrite pixel
            if (tempBGRA[idx4 + 3] > 0) {
                rgbBuffer[idx3 + 0] = tempBGRA[idx4 + 2]; // R
                rgbBuffer[idx3 + 1] = tempBGRA[idx4 + 1]; // G
                rgbBuffer[idx3 + 2] = tempBGRA[idx4 + 0]; // B
            }
        }
    }
}

// --- Modified screenshot-to-FIP function ---
void captureScreenRegionToFIPBuffer(int x, int y, int w, int h, std::vector<unsigned char>& outBuffer, const wchar_t* overlayText, ScaleMode scaleMode) {
    // Adjust coordinates if virtual monitor is enabled
    int adjustedX = x;
    int adjustedY = y;
    
    // Capture at configured resolution
    HDC hScreen = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, w, h);
    HGDIOBJ old = SelectObject(hMemDC, hBitmap);
    BitBlt(hMemDC, 0, 0, w, h, hScreen, adjustedX, adjustedY, SRCCOPY);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = h; // bottom-up
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    // Calculate row padding for 24bpp (each row must be a multiple of 4 bytes)
    int row_padded = ((w * 3 + 3) & ~3);
    std::vector<unsigned char> captureBuf(row_padded * h);
    GetDIBits(hMemDC, hBitmap, 0, h, captureBuf.data(), &bmi, DIB_RGB_COLORS);

    SelectObject(hMemDC, old);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreen);

    // Convert captureBuf (with padding) to a packed RGB buffer for scaling/copying
    std::vector<unsigned char> packedBuf(w * h * 3);
    for (int y = 0; y < h; ++y) {
        memcpy(&packedBuf[y * w * 3], &captureBuf[y * row_padded], w * 3);
    }

    // If capture resolution matches FIP output, skip scaling
    if (w == 320 && h == 240) {
        outBuffer = std::move(packedBuf);
    } else {
        // Scale to FIP resolution (320x240)
        if (scaleMode == ScaleMode::Bilinear) {
            scaleBilinear(packedBuf, w, h, outBuffer, 320, 240);
        } else {
            scaleNearestNeighbor(packedBuf, w, h, outBuffer, 320, 240);
        }
    }
    // Draw overlay text after scaling for constant size
    drawOverlayTextOnFIPBuffer(outBuffer, overlayText);
}

// Send key events using SendInput
void sendKeyCombo(const KeyCombo& combo, bool down) {
    std::vector<INPUT> inputs;
    // Modifiers first (down), main key, then modifiers up (if releasing)
    if (down) {
        for (WORD vk : combo.modifiers) {
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = vk;
            input.ki.dwFlags = 0;
            inputs.push_back(input);
        }
        if (combo.mainKey) {
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = MapVirtualKey(combo.mainKey, MAPVK_VK_TO_VSC);
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
            inputs.push_back(input);
        }
    } else {
        if (combo.mainKey) {
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = MapVirtualKey(combo.mainKey, MAPVK_VK_TO_VSC);
            input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
        for (auto it = combo.modifiers.rbegin(); it != combo.modifiers.rend(); ++it) {
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = *it;
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(input);
        }
    }
    if (!inputs.empty()) {
        SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
    }
}

// Clean up created pages on device disconnect
void cleanupDevicePages(void* hDevice) {
    if (!g_pfnRemovePage || !hDevice) {
        if (g_config.debug) {
            LogMessage(L"Cannot remove pages: RemovePage function not available or invalid device handle.");
        }
        return;
    }
    
    if (g_config.debug) {
        LogMessageFormatted(L"Cleaning up %d created pages...", g_createdPageIds.size());
    }
    
    // Remove pages in reverse order (highest page ID first) to avoid potential issues
    for (auto it = g_createdPageIds.rbegin(); it != g_createdPageIds.rend(); ++it) {
        DWORD pageId = *it;
        HRESULT hr = g_pfnRemovePage(hDevice, pageId);
        if (SUCCEEDED(hr)) {
            if (g_config.debug) {
                LogMessageFormatted(L"Successfully removed page %d", pageId);
            }
        } else {
            if (g_config.debug) {
                LogMessageFormatted(L"Failed to remove page %d (HRESULT=0x%08X)", pageId, hr);
            }
        }
    }
    
    // Clear the tracking vector
    g_createdPageIds.clear();
    
    if (g_config.debug) {
        LogMessage(L"Page cleanup completed.");
    }
}

// FIP soft button callback
void __stdcall SoftButtonCallback(void* hDevice, DWORD dwButtons, void* pCtxt) {
    // S1-S6 are bits 10-5 (0x400, 0x200, 0x100, 0x80, 0x40, 0x20)
    // Rotary encoders: LeftMinus=bit16, LeftPlus=bit8, RightMinus=bit4, RightPlus=bit2
    if (g_config.debug) LogMessageFormatted(L"FIP pressed -> address -> %d", dwButtons);
    
    // Handle S1-S6 buttons
    for (int i = 0; i < 6; ++i) {
        DWORD mask = (1 << (10 - i)); // S1=10, S2=9, ..., S6=5
        bool wasDown = (g_prevSoftButtonState & mask) != 0;
        bool isDown = (dwButtons & mask) != 0;
        if (isDown != wasDown) {
            std::map<int, KeyCombo>::iterator it = g_fipButtonCombos.find(5 - i);
            if (it != g_fipButtonCombos.end()) {
                if (isDown) {
                    sendKeyCombo(it->second, true); // key down
                    if (g_config.debug) LogMessageFormatted(L"FIP S%d pressed -> key down ->%s", 6-i, std::wstring(it->second.combo.begin(), it->second.combo.end()).c_str());
                } else {
                    sendKeyCombo(it->second, false); // key up
                    if (g_config.debug) LogMessageFormatted(L"FIP S%d released -> key up ->%s", 6-i, std::wstring(it->second.combo.begin(), it->second.combo.end()).c_str());
                }
            }
        }
    }
    
    // Handle rotary encoders
    std::vector<std::pair<DWORD, std::string> > rotaryMasks;
    rotaryMasks.push_back(std::make_pair(0x00000002, "RightRotaryPlus"));   // bit 2
    rotaryMasks.push_back(std::make_pair(0x00000004, "RightRotaryMinus"));  // bit 4
    rotaryMasks.push_back(std::make_pair(0x00000008, "LeftRotaryPlus"));    // bit 8
    rotaryMasks.push_back(std::make_pair(0x00000010, "LeftRotaryMinus"));   // bit 16 (0x10)
    for (size_t i = 0; i < rotaryMasks.size(); ++i) {
        DWORD mask = rotaryMasks[i].first;
        const std::string& key = rotaryMasks[i].second;
        bool wasDown = (g_prevSoftButtonState & mask) != 0;
        bool isDown = (dwButtons & mask) != 0;
        std::map<std::string, KeyCombo>::iterator it = g_rotaryEncoderCombos.find(key);
        if (isDown != wasDown) {
            if (it != g_rotaryEncoderCombos.end()) {
                if (isDown) {
                    sendKeyCombo(it->second, true); // key down
                    if (g_config.debug) LogMessageFormatted(L"FIP %s pressed -> key down ->%s", 
                        std::wstring(key.begin(), key.end()).c_str(),
                        std::wstring(it->second.combo.begin(), it->second.combo.end()).c_str());
                } else {
                    sendKeyCombo(it->second, false); // key up
                    if (g_config.debug) LogMessageFormatted(L"FIP %s released -> key up ->%s", 
                        std::wstring(key.begin(), key.end()).c_str(),
                        std::wstring(it->second.combo.begin(), it->second.combo.end()).c_str());
                }
            }
        }
    }
    
    g_prevSoftButtonState = dwButtons;
}

// Main FIP logic moved to separate function
void RunFIPLogic() {
    LogMessage(L"Loading configuration...");
    if (!LoadConfiguration("config.json")) {
        LogMessage(L"Error loading configuration. Check if config.json exists.");
        g_shouldExit = true;
        return;
    }
    g_configLoaded = true;
    LogMessage(L"Initializing DirectOutput SDK...");
    if (!LoadDirectOutputFunctions()) {
        LogMessage(L"Failed to load DirectOutput functions.");
        g_shouldExit = true;
        return;
    }
    HRESULT hr = g_pfnInitialize(L"FIPUnlocked");
    if (FAILED(hr)) {
        LogMessageFormatted(L"Error initializing DirectOutput: 0x%08X", hr);
        UnloadDirectOutput();
        g_shouldExit = true;
        return;
    }
    LogMessage(L"DirectOutput SDK initialized.");
    hr = g_pfnRegisterDeviceCallback(DeviceChangeCallback, NULL);
    if (FAILED(hr)) {
        LogMessageFormatted(L"Error registering device callback: 0x%08X", hr);
        UnloadDirectOutput();
        g_shouldExit = true;
        return;
    }
    hr = g_pfnEnumerate(EnumerateCallback, NULL);
    if (FAILED(hr)) {
        LogMessageFormatted(L"Error during device enumeration: 0x%08X", hr);
    }
    if (!g_bDeviceFound) {
        LogMessage(L"FIP device not found. Please check if the device is connected.");
        LogMessage(L"Software will wait for the device to be connected...");
        int waitCounter = 0;
        while (!g_bDeviceFound && !g_shouldExit) {
            Sleep(500);
            waitCounter++;
            if (waitCounter % 10 == 0) {
                LogMessage(L"Still waiting for FIP device...");
            }
        }
        if (g_bDeviceFound) {
            LogMessage(L"FIP device connected!");
        }
    }
    LogMessage(L"Software is running. Use the window controls to manage the application.");
    while (!g_shouldExit) {
        Sleep(200);
    }
    UnloadDirectOutput();
    timeEndPeriod(1);
    LogMessage(L"Software terminated.");
}

int main(int argc, char* argv[])
{
    timeBeginPeriod(1); // Set timer resolution to 1 ms for accurate Sleep()
    std::signal(SIGINT, signalHandler);
    if (!InitializeGUI()) {
        // If GUI fails, just exit silently (no console fallback)
        return 1;
    }
    std::thread fipThread(RunFIPLogic);
    GUIMessageLoop();
    g_shouldExit = true;
    if (fipThread.joinable()) fipThread.join();
    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

// Add SAL annotations to WinMain
#include <sal.h>
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    return main(__argc, __argv);
}

// Convert 320x240 RGB buffer to HBITMAP and update preview
void UpdatePreviewBitmap(const std::vector<unsigned char>& rgbBuffer) {
    // Check if preview is enabled and if enough time has passed since last update
    HWND hPreviewCheckbox = GetDlgItem(g_hMainWindow, ID_CHECKBOX_PREVIEW);
    if (!g_hPreview || !hPreviewCheckbox) return;
    if (SendMessage(hPreviewCheckbox, BM_GETCHECK, 0, 0) != BST_CHECKED) return;
    
    // Limit preview update frequency to 10 FPS to reduce CPU load
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - g_lastPreviewUpdate < PREVIEW_UPDATE_INTERVAL) return;
    g_lastPreviewUpdate = currentTime;
    
    std::lock_guard<std::mutex> lock(g_previewMutex);
    
    // Create DIB section only once and reuse it
    if (!g_hPreviewDIB) {
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 320;
        bmi.bmiHeader.biHeight = 240; // bottom-up for correct orientation
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        
        HDC hdc = GetDC(NULL);
        g_hPreviewDIB = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &g_previewBits, NULL, 0);
        ReleaseDC(NULL, hdc);
        
        if (!g_hPreviewDIB || !g_previewBits) {
            if (g_hPreviewDIB) {
                DeleteObject(g_hPreviewDIB);
                g_hPreviewDIB = NULL;
            }
            return;
        }
    }
    
    // Convert RGB directly to DIB buffer
    if (g_previewBits) {
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 320; ++x) {
                int srcIdx = (y * 320 + x) * 3;
                int dstIdx = (y * 320 + x) * 4;
                ((unsigned char*)g_previewBits)[dstIdx + 0] = rgbBuffer[srcIdx + 2]; // B
                ((unsigned char*)g_previewBits)[dstIdx + 1] = rgbBuffer[srcIdx + 1]; // G
                ((unsigned char*)g_previewBits)[dstIdx + 2] = rgbBuffer[srcIdx + 0]; // R
                ((unsigned char*)g_previewBits)[dstIdx + 3] = 255; // A
            }
        }
        
        // Update control only if buffer changed
        HBITMAP oldBmp = (HBITMAP)SendMessage(g_hPreview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_hPreviewDIB);
        if (g_hPreviewBitmap && g_hPreviewBitmap != g_hPreviewDIB) {
            DeleteObject(g_hPreviewBitmap);
        }
        g_hPreviewBitmap = g_hPreviewDIB;
        if (oldBmp && oldBmp != g_hPreviewDIB) {
            DeleteObject(oldBmp);
        }
    }
}


