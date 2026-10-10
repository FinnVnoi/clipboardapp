#include "Ui.h"

#include <Shellapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <sstream>
#include <vector>

namespace {
const UINT TrayMessage = WM_APP + 1;
const UINT TrayIconId = 1;
const UINT MenuSettings = 1001;
const UINT MenuOpenLibrary = 1002;
const UINT MenuExit = 1003;

const int SettingsWidth = 640;
const int SettingsHeight = 750;
const int IdThreshold = 2001;
const int IdMaxItems = 2002;
const int IdAskConfirm = 2003;
const int IdAutoDetect = 2004;
const int IdAllowlist = 2005;
const int IdSave = 2006;
const int IdCancel = 2007;
const int IdClearCache = 2008;
const int IdMaxFileSize = 2009;
const int IdCacheMaxAgeDays = 2010;
const int IdStartWithWindows = 2011;
const int IdMultitextSeparator = 2012;
const int IdMultitextCustom = 2013;
const int IdMultitextHelp = 2014;

const int PickerWidth = 760;
const int PickerHeight = 520;
const int IdPickerList = 3001;
const int IdPickerChoose = 3002;
const int IdPickerCancel = 3003;
const int IdPickerFileName = 3004;
const int IdPickerSendAsFile = 3005;
const int IdPickerSearch = 3006;

const int ConversionWidth = 500;
const int ConversionHeight = 230;
const int IdConversionName = 4001;
const int IdConversionConvert = 4002;
const int IdConversionRaw = 4003;
const int IdConversionCancel = 4004;

WNDPROC originalAllowlistProc = nullptr;

struct SettingsWindowState {
    Ui* ui;
    HWND thresholdEdit;
    HWND maxItemsEdit;
    HWND maxFileSizeEdit;
    HWND cacheMaxAgeDaysEdit;
    HWND askConfirmCheck;
    HWND autoDetectCheck;
    HWND startWithWindowsCheck;
    HWND multitextSeparatorCombo;
    HWND multitextCustomLabel;
    HWND multitextCustomEdit;
    HWND multitextHelpButton;
    HWND allowlistEdit;
    bool done;
    bool saved;

    SettingsWindowState()
        : ui(nullptr), thresholdEdit(nullptr), maxItemsEdit(nullptr), maxFileSizeEdit(nullptr), cacheMaxAgeDaysEdit(nullptr),
          askConfirmCheck(nullptr), autoDetectCheck(nullptr), startWithWindowsCheck(nullptr),
          multitextSeparatorCombo(nullptr), multitextCustomLabel(nullptr), multitextCustomEdit(nullptr),
          multitextHelpButton(nullptr), allowlistEdit(nullptr),
          done(false), saved(false) {}
};

struct PickerWindowState {
    ClipboardLibrary* library;
    const AppConfig* config;
    HWND searchEdit;
    HWND listBox;
    HWND fileNameEdit;
    HWND sendAsFileCheck;
    std::vector<size_t> filteredIndices;
    std::vector<size_t> multiSelectedIndices;
    int selectedIndex;
    int previewScrollOffset;
    int previewMaxScroll;
    int itemPreviewScrollOffset;
    int itemPreviewMaxScroll;
    bool defaultSendTextAsFile;
    bool sendTextAsFile;
    bool multiText;
    bool extractTextFromFile;
    bool done;
    std::wstring selectedFileName;
    std::wstring selectedText;

    PickerWindowState()
        : library(nullptr), config(nullptr), searchEdit(nullptr), listBox(nullptr), fileNameEdit(nullptr), sendAsFileCheck(nullptr),
          selectedIndex(-1), previewScrollOffset(0), previewMaxScroll(0),
          itemPreviewScrollOffset(0), itemPreviewMaxScroll(0), defaultSendTextAsFile(true),
          sendTextAsFile(true), multiText(false), extractTextFromFile(false), done(false) {}
};

struct ConversionWindowState {
    size_t characterCount;
    std::wstring defaultFileName;
    HWND fileNameEdit;
    TextConversionChoice choice;
    bool done;

    ConversionWindowState() : characterCount(0), fileNameEdit(nullptr), done(false) {}
};

std::wstring typeLabel(const ClipboardItem& item) {
    if (item.type == ClipboardItem::Image) {
        return L"Ảnh";
    }
    if (item.type == ClipboardItem::Files) {
        return L"File";
    }
    return L"Text";
}

std::wstring defaultPickerFileName(const ClipboardItem& item) {
    if (item.type == ClipboardItem::Text) {
        std::wstring value = item.title;
        const size_t colon = value.find(L": ");
        if (colon != std::wstring::npos && colon + 2 < value.size()) {
            value = value.substr(colon + 2);
        }
        value = safeFilePart(value);
        if (value.empty() || value == L"clipboard") {
            value = L"clipboard_text.txt";
        }
        if (extensionFromPath(value).empty()) {
            value += L".txt";
        }
        return value;
    }

    if (!item.files.empty()) {
        return safeFilePart(fileNameFromPath(item.files.front()));
    }
    return item.type == ClipboardItem::Image ? L"image" : L"file";
}

std::wstring itemLabel(const ClipboardItem& item, size_t index, bool multiSelected) {
    std::wstringstream stream;
    if (item.pinned) {
        stream << L"📌 ";
    }
    if (multiSelected) {
        stream << L"[✓] ";
    }
    stream << (index + 1) << L". [" << typeLabel(item) << L"] " << item.title;
    if ((item.type == ClipboardItem::Files || item.type == ClipboardItem::Image) && !item.files.empty()) {
        for (size_t fileIndex = 0; fileIndex < item.files.size(); ++fileIndex) {
            if (!fileExists(item.files[fileIndex])) {
                stream << L" (cache missing)";
                break;
            }
        }
    }
    return stream.str();
}

std::wstring joinAllowlist(const std::vector<std::wstring>& values) {
    std::wstring result;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += L"\r\n";
        }
        result += values[index];
    }
    return result;
}

std::vector<std::wstring> parseAllowlist(std::wstring value) {
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] == L'\r' || value[index] == L'\n' || value[index] == L';') {
            value[index] = L',';
        }
    }

    std::vector<std::wstring> result;
    std::wstringstream stream(value);
    std::wstring part;
    while (std::getline(stream, part, L',')) {
        part = toLower(trim(part));
        if (!part.empty()) {
            result.push_back(part);
        }
    }
    return result;
}

std::wstring decodeMultitextSeparator(const std::wstring& value) {
    std::wstring decoded;
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] != L'\\' || index + 1 >= value.size()) {
            decoded += value[index];
            continue;
        }
        const wchar_t next = value[++index];
        if (next == L'n') {
            decoded += L'\r';
            decoded += L'\n';
        } else if (next == L'r') {
            decoded += L'\r';
        } else if (next == L't') {
            decoded += L'\t';
        } else if (next == L'\\') {
            decoded += L'\\';
        } else {
            decoded += next;
        }
    }
    return decoded;
}

std::wstring multitextSeparator(const AppConfig& config) {
    if (config.multitextSeparatorMode == MultitextSeparatorSpace) {
        return L" ";
    }
    if (config.multitextSeparatorMode == MultitextSeparatorBlankLine) {
        return L"\r\n\r\n";
    }
    if (config.multitextSeparatorMode == MultitextSeparatorCustom) {
        return decodeMultitextSeparator(config.multitextCustomSeparator);
    }
    return L"\r\n";
}

std::wstring windowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(window, &text[0], length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

bool handleSelectAllShortcut(const MSG& message) {
    if (message.message != WM_KEYDOWN || message.wParam != 'A' ||
        (GetKeyState(VK_CONTROL) & 0x8000) == 0 || message.hwnd == nullptr) {
        return false;
    }

    wchar_t className[16];
    ZeroMemory(className, sizeof(className));
    GetClassNameW(message.hwnd, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
    if (_wcsicmp(className, L"Edit") != 0) {
        return false;
    }

    SendMessageW(message.hwnd, EM_SETSEL, 0, -1);
    return true;
}

void setDefaultFont(HWND window) {
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND createControl(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style, DWORD exStyle, int x, int y, int width, int height, int id, bool tabStop = true) {
    DWORD controlStyle = WS_CHILD | WS_VISIBLE | style;
    if (id != 0 && tabStop) {
        controlStyle |= WS_TABSTOP;
    }
    HWND control = CreateWindowExW(exStyle, className, text, controlStyle, x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    setDefaultFont(control);
    return control;
}

void centerWindow(HWND window, HWND owner) {
    RECT windowRect;
    GetWindowRect(window, &windowRect);
    RECT ownerRect;
    if (owner != nullptr) {
        GetWindowRect(owner, &ownerRect);
    } else {
        ownerRect.left = 0;
        ownerRect.top = 0;
        ownerRect.right = GetSystemMetrics(SM_CXSCREEN);
        ownerRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

LRESULT CALLBACK allowlistEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessageW(window, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
        return 0;
    }
    return CallWindowProcW(originalAllowlistProc, window, message, wParam, lParam);
}

void updateMultitextCustomVisibility(SettingsWindowState* state) {
    if (state == nullptr || state->multitextSeparatorCombo == nullptr) {
        return;
    }
    const bool custom = SendMessageW(state->multitextSeparatorCombo, CB_GETCURSEL, 0, 0) == MultitextSeparatorCustom;
    ShowWindow(state->multitextCustomLabel, custom ? SW_SHOW : SW_HIDE);
    ShowWindow(state->multitextCustomEdit, custom ? SW_SHOW : SW_HIDE);
    ShowWindow(state->multitextHelpButton, custom ? SW_SHOW : SW_HIDE);
    EnableWindow(state->multitextCustomEdit, custom ? TRUE : FALSE);
    EnableWindow(state->multitextHelpButton, custom ? TRUE : FALSE);
}

void showMultitextHelp(HWND window) {
    MessageBoxW(window,
        L"Dấu phân cách tùy chỉnh hỗ trợ ký tự thường và escape kiểu C++:\n"
        L"\\t = tab, \\n = xuống dòng, \\r = carriage return, \\\\ = dấu gạch chéo.",
        L"Trợ giúp multitext", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

std::wstring readTextFileForPreview(const std::wstring& path) {
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (file == nullptr) {
        return L"";
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return L"";
    }

    std::string bytes(static_cast<size_t>(size), '\0');
    fread(&bytes[0], 1, bytes.size(), file);
    fclose(file);

    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring result;
        for (size_t index = 2; index + 1 < bytes.size(); index += 2) {
            result += static_cast<wchar_t>(static_cast<unsigned char>(bytes[index]) |
                (static_cast<unsigned char>(bytes[index + 1]) << 8));
        }
        return result;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE &&
        static_cast<unsigned char>(bytes[1]) == 0xFF) {
        std::wstring result;
        for (size_t index = 2; index + 1 < bytes.size(); index += 2) {
            result += static_cast<wchar_t>((static_cast<unsigned char>(bytes[index]) << 8) |
                static_cast<unsigned char>(bytes[index + 1]));
        }
        return result;
    }
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }

    const std::wstring utf8 = utf8ToWide(bytes);
    if (!utf8.empty()) {
        return utf8;
    }

    const int wideSize = MultiByteToWideChar(
        CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideSize <= 0) {
        return L"";
    }
    std::wstring ansi(static_cast<size_t>(wideSize), L'\0');
    MultiByteToWideChar(
        CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), &ansi[0], wideSize);
    return ansi;
}

void clearCacheFromSettings(HWND window, SettingsWindowState* state) {
    if (MessageBoxW(window, L"Xóa toàn bộ file cache chưa pin? Text thô và các mục đã pin vẫn được giữ.", L"Xóa cache", MB_YESNO | MB_ICONWARNING | MB_TOPMOST) != IDYES) {
        return;
    }
    const std::vector<std::wstring> pinnedFiles = state->ui->library().pinnedCachePaths();
    clearDirectoryContentsExcept(state->ui->appState().cacheDir(), pinnedFiles);
    ensureDirectory(state->ui->appState().cacheDir());
    state->ui->library().removeCachedItems();
    MessageBoxW(window, L"Đã xóa cache chưa pin. Text thô, mục đã pin và file của chúng vẫn được giữ.", L"ClipboardTxtApp", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

void saveSettings(HWND window, SettingsWindowState* state) {
    const std::wstring thresholdText = trim(windowText(state->thresholdEdit));
    const int threshold = _wtoi(thresholdText.c_str());
    if (thresholdText.empty() || threshold < 0) {
        MessageBoxW(window, L"Ngưỡng ký tự phải là số từ 0 trở lên. Giá trị 0 và 1 có cùng hiệu lực.", L"ClipboardTxtApp", MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring maxItemsText = trim(windowText(state->maxItemsEdit));
    const int maxItems = _wtoi(maxItemsText.c_str());
    if (maxItemsText.empty() || maxItems < 0 || maxItems > 500) {
        MessageBoxW(window, L"Số slot clipboard phải nằm trong khoảng 0–500. Nhập 0 để không giới hạn.", L"ClipboardTxtApp", MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring maxFileSizeText = trim(windowText(state->maxFileSizeEdit));
    const int maxFileSizeMB = _wtoi(maxFileSizeText.c_str());
    if (maxFileSizeText.empty() || maxFileSizeMB < 0 || maxFileSizeMB > 10240) {
        MessageBoxW(window, L"Dung lượng file cache tối đa phải nằm trong khoảng 0–10240 MB. Nhập 0 để không giới hạn.", L"ClipboardTxtApp", MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring cacheMaxAgeText = trim(windowText(state->cacheMaxAgeDaysEdit));
    const int cacheMaxAgeDays = _wtoi(cacheMaxAgeText.c_str());
    if (cacheMaxAgeText.empty() || cacheMaxAgeDays < 0 || cacheMaxAgeDays > 36500) {
        MessageBoxW(window, L"Số ngày giữ cache phải nằm trong khoảng 0–36500. Nhập 0 để không tự xóa.", L"ClipboardTxtApp", MB_OK | MB_ICONWARNING);
        return;
    }

    std::vector<std::wstring> allowed = parseAllowlist(windowText(state->allowlistEdit));
    if (allowed.empty()) {
        MessageBoxW(window, L"Allowlist phải có ít nhất một process, ví dụ chrome.exe.", L"ClipboardTxtApp", MB_OK | MB_ICONWARNING);
        return;
    }

    const bool startWithWindows = SendMessageW(state->startWithWindowsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!setStartWithWindows(startWithWindows)) {
        MessageBoxW(window, L"Không thể cập nhật tùy chọn khởi động cùng Windows.", L"ClipboardTxtApp", MB_OK | MB_ICONWARNING);
        return;
    }

    AppConfig& config = state->ui->appState().config();
    const int multitextSeparatorMode = static_cast<int>(SendMessageW(state->multitextSeparatorCombo, CB_GETCURSEL, 0, 0));
    const std::wstring customSeparator = windowText(state->multitextCustomEdit);
    if (multitextSeparatorMode == MultitextSeparatorCustom && customSeparator.empty()) {
        MessageBoxW(window, L"Dấu phân cách tùy chỉnh không được để trống.", L"ClipboardTxtApp", MB_OK | MB_ICONWARNING);
        return;
    }
    config.textThreshold = threshold;
    config.maxItems = maxItems;
    config.maxFileSizeMB = maxFileSizeMB;
    config.cacheMaxAgeDays = cacheMaxAgeDays;
    config.askBeforeConverting = SendMessageW(state->askConfirmCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.enableFileDialogAutoDetect = SendMessageW(state->autoDetectCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.startWithWindows = startWithWindows;
    config.multitextSeparatorMode = multitextSeparatorMode;
    config.multitextCustomSeparator = customSeparator;
    config.allowedProcesses = allowed;
    state->ui->appState().save();
    state->ui->library().trimToLimit();
    state->ui->library().save();

    state->saved = true;
    state->done = true;
    DestroyWindow(window);
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsWindowState* state = reinterpret_cast<SettingsWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE: {
        state = reinterpret_cast<SettingsWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        const AppConfig& config = state->ui->appState().config();

        createControl(window, L"STATIC", L"Giới hạn clipboard", 0, 0, 20, 16, 220, 22, 0);
        createControl(window, L"STATIC", L"Ngưỡng ký tự:", 0, 0, 36, 48, 150, 22, 0);
        state->thresholdEdit = createControl(window, L"EDIT", numberToWString(static_cast<size_t>(config.textThreshold)).c_str(), ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 200, 45, 100, 24, IdThreshold);
        createControl(window, L"STATIC", L"0 và 1 đều áp dụng từ 1 ký tự.", 0, 0, 320, 48, 270, 22, 0);

        createControl(window, L"STATIC", L"Số slot clipboard:", 0, 0, 36, 80, 150, 22, 0);
        state->maxItemsEdit = createControl(window, L"EDIT", numberToWString(static_cast<size_t>(config.maxItems)).c_str(), ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 200, 77, 100, 24, IdMaxItems);
        createControl(window, L"STATIC", L"0 = không giới hạn số mục.", 0, 0, 320, 80, 270, 22, 0);

        createControl(window, L"STATIC", L"Cache", 0, 0, 20, 122, 220, 22, 0);
        createControl(window, L"STATIC", L"File tối đa (MB):", 0, 0, 36, 154, 150, 22, 0);
        state->maxFileSizeEdit = createControl(window, L"EDIT", numberToWString(static_cast<size_t>(config.maxFileSizeMB)).c_str(), ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 200, 151, 100, 24, IdMaxFileSize);
        createControl(window, L"STATIC", L"0 = không giới hạn dung lượng.", 0, 0, 320, 154, 270, 22, 0);

        createControl(window, L"STATIC", L"Tự xóa cache cũ hơn:", 0, 0, 36, 186, 160, 22, 0);
        state->cacheMaxAgeDaysEdit = createControl(window, L"EDIT", numberToWString(static_cast<size_t>(config.cacheMaxAgeDays)).c_str(), ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 200, 183, 100, 24, IdCacheMaxAgeDays);
        createControl(window, L"STATIC", L"ngày (0 = không tự xóa)", 0, 0, 320, 186, 220, 22, 0);
        createControl(window, L"BUTTON", L"Xóa toàn bộ cache", 0, 0, 450, 214, 140, 30, IdClearCache);

        createControl(window, L"STATIC", L"Hành vi", 0, 0, 20, 258, 220, 22, 0);
        state->startWithWindowsCheck = createControl(window, L"BUTTON", L"Khởi động ClipboardTxtApp cùng Windows", BS_AUTOCHECKBOX, 0, 36, 288, 500, 24, IdStartWithWindows);
        SendMessageW(state->startWithWindowsCheck, BM_SETCHECK, config.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);

        state->askConfirmCheck = createControl(window, L"BUTTON", L"Luôn hỏi trước khi chuyển văn bản dài thành file TXT", BS_AUTOCHECKBOX, 0, 36, 318, 500, 24, IdAskConfirm);
        SendMessageW(state->askConfirmCheck, BM_SETCHECK, config.askBeforeConverting ? BST_CHECKED : BST_UNCHECKED, 0);

        state->autoDetectCheck = createControl(window, L"BUTTON", L"Tự phát hiện hộp Choose file trong browser/app được cho phép", BS_AUTOCHECKBOX, 0, 36, 348, 540, 24, IdAutoDetect);
        SendMessageW(state->autoDetectCheck, BM_SETCHECK, config.enableFileDialogAutoDetect ? BST_CHECKED : BST_UNCHECKED, 0);

        createControl(window, L"STATIC", L"Multitext", 0, 0, 20, 390, 220, 22, 0);
        createControl(window, L"STATIC", L"Cách nhau giữa các mục:", 0, 0, 36, 420, 170, 22, 0);
        state->multitextSeparatorCombo = createControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 0, 205, 416, 270, 160, IdMultitextSeparator);
        SendMessageW(state->multitextSeparatorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Chỉ một dấu cách"));
        SendMessageW(state->multitextSeparatorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Chừa một dòng trống (mặc định)"));
        SendMessageW(state->multitextSeparatorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Chỉ xuống dòng"));
        SendMessageW(state->multitextSeparatorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tùy chỉnh"));
        SendMessageW(state->multitextSeparatorCombo, CB_SETCURSEL, config.multitextSeparatorMode, 0);
        state->multitextCustomLabel = createControl(window, L"STATIC", L"Dấu tùy chỉnh:", 0, 0, 36, 454, 150, 22, 0);
        state->multitextCustomEdit = createControl(window, L"EDIT", config.multitextCustomSeparator.c_str(), ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 205, 451, 270, 24, IdMultitextCustom);
        state->multitextHelpButton = createControl(window, L"BUTTON", L"?", BS_PUSHBUTTON, 0, 486, 451, 28, 24, IdMultitextHelp);
        updateMultitextCustomVisibility(state);

        createControl(window, L"STATIC", L"Ứng dụng được phép", 0, 0, 20, 484, 220, 22, 0);
        createControl(window, L"STATIC", L"Nhập mỗi process một dòng, ví dụ chrome.exe hoặc discord.exe", 0, 0, 36, 510, 550, 22, 0);
        state->allowlistEdit = createControl(window, L"EDIT", joinAllowlist(config.allowedProcesses).c_str(), ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE, 36, 535, 554, 72, IdAllowlist);
        originalAllowlistProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state->allowlistEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(allowlistEditProc)));

        createControl(window, L"STATIC", L"Hotkey: Ctrl+Alt+V", 0, 0, 36, 616, 560, 22, 0);
        createControl(window, L"STATIC",
            L"Trong cửa sổ clipboard:\n"
            L"Ctrl+Enter hoặc Ctrl+click: chọn/bỏ chọn multitext.\n"
            L"Shift+Enter hoặc Shift+click: pin/bỏ pin mục đang trỏ tới.\n"
            L"Lăn chuột trên preview: xem nội dung dài.",
            0, 0, 36, 637, 560, 64, 0);
        createControl(window, L"BUTTON", L"Lưu", BS_DEFPUSHBUTTON, 0, 430, 662, 86, 30, IdSave);
        createControl(window, L"BUTTON", L"Hủy", 0, 0, 526, 662, 86, 30, IdCancel);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IdSave && state != nullptr) {
            saveSettings(window, state);
            return 0;
        }
        if (LOWORD(wParam) == IdClearCache && state != nullptr) {
            clearCacheFromSettings(window, state);
            return 0;
        }
        if (LOWORD(wParam) == IdMultitextSeparator && HIWORD(wParam) == CBN_SELCHANGE && state != nullptr) {
            updateMultitextCustomVisibility(state);
            return 0;
        }
        if (LOWORD(wParam) == IdMultitextHelp) {
            showMultitextHelp(window);
            return 0;
        }
        if (LOWORD(wParam) == IdCancel && state != nullptr) {
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (state != nullptr) {
            state->done = true;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (state != nullptr) {
            state->done = true;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

RECT previewRect() {
    RECT rect;
    rect.left = 345;
    rect.top = 55;
    rect.right = 730;
    rect.bottom = 345;
    return rect;
}

bool isPickerMultiSelected(const PickerWindowState* state, size_t itemIndex) {
    return state != nullptr &&
        std::find(state->multiSelectedIndices.begin(), state->multiSelectedIndices.end(), itemIndex) != state->multiSelectedIndices.end();
}

std::wstring combinedPickerText(const PickerWindowState* state) {
    if (state == nullptr || state->library == nullptr) {
        return L"";
    }

    std::wstring combined;
    const std::wstring separator = state->config == nullptr ? L"\r\n" : multitextSeparator(*state->config);
    const std::vector<ClipboardItem>& items = state->library->items();
    for (size_t index = 0; index < items.size(); ++index) {
        if (!isPickerMultiSelected(state, index) || items[index].type != ClipboardItem::Text) {
            continue;
        }
        const std::wstring text = trim(items[index].text);
        if (text.empty()) {
            continue;
        }
        if (!combined.empty()) {
            combined += separator;
        }
        combined += text;
    }
    return combined;
}

bool isTxtFileItem(const ClipboardItem& item) {
    return item.type == ClipboardItem::Files && item.files.size() == 1 &&
        toLower(extensionFromPath(item.files.front())) == L".txt";
}

void resetPickerPreviewScroll(PickerWindowState* state) {
    if (state != nullptr) {
        state->previewScrollOffset = 0;
        state->previewMaxScroll = 0;
        state->itemPreviewScrollOffset = 0;
        state->itemPreviewMaxScroll = 0;
    }
}

void invalidatePickerPreview(HWND window, PickerWindowState* state) {
    resetPickerPreviewScroll(state);
    RECT rect = previewRect();
    InvalidateRect(window, &rect, TRUE);
}

void drawImagePreview(HDC dc, const std::wstring& path, const RECT& rect) {
    Gdiplus::Graphics graphics(dc);
    Gdiplus::Image image(path.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok || image.GetWidth() == 0 || image.GetHeight() == 0) {
        RECT textRect = rect;
        DrawTextW(dc, L"Không thể preview ảnh này. File vẫn có thể được chọn để upload.", -1, &textRect, DT_WORDBREAK | DT_CENTER | DT_VCENTER);
        return;
    }

    const double maxWidth = static_cast<double>(rect.right - rect.left - 24);
    const double maxHeight = static_cast<double>(rect.bottom - rect.top - 24);
    const double scaleX = maxWidth / static_cast<double>(image.GetWidth());
    const double scaleY = maxHeight / static_cast<double>(image.GetHeight());
    const double scale = std::min(scaleX, scaleY);
    const int width = static_cast<int>(image.GetWidth() * scale);
    const int height = static_cast<int>(image.GetHeight() * scale);
    const int x = rect.left + ((rect.right - rect.left) - width) / 2;
    const int y = rect.top + ((rect.bottom - rect.top) - height) / 2;
    graphics.DrawImage(&image, x, y, width, height);
}

void updatePickerFileNameEdit(PickerWindowState* state) {
    if (state == nullptr || state->fileNameEdit == nullptr || state->library == nullptr) {
        return;
    }

    const std::vector<ClipboardItem>& items = state->library->items();
    if (!state->multiSelectedIndices.empty()) {
        EnableWindow(state->fileNameEdit, TRUE);
        SetWindowTextW(state->fileNameEdit, L"clipboard_multitext.txt");
        if (state->sendAsFileCheck != nullptr) {
            SetWindowTextW(state->sendAsFileCheck, L"Với multitext: gửi dạng TXT (bỏ tick để dán text thô khi nhập liệu)");
            EnableWindow(state->sendAsFileCheck, TRUE);
            SendMessageW(state->sendAsFileCheck, BM_SETCHECK, state->defaultSendTextAsFile ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        return;
    }

    if (state->sendAsFileCheck != nullptr) {
        SetWindowTextW(state->sendAsFileCheck, L"Với text: gửi dạng TXT (bỏ tick để dán text thô khi nhập liệu)");
    }
    if (state->selectedIndex >= 0 && static_cast<size_t>(state->selectedIndex) < items.size()) {
        const ClipboardItem& item = items[static_cast<size_t>(state->selectedIndex)];
        EnableWindow(state->fileNameEdit, TRUE);
        SetWindowTextW(state->fileNameEdit, defaultPickerFileName(item).c_str());
        if (state->sendAsFileCheck != nullptr) {
            if (isTxtFileItem(item)) {
                SetWindowTextW(state->sendAsFileCheck, L"Với file TXT: phân rã thành text thô");
                EnableWindow(state->sendAsFileCheck, TRUE);
                SendMessageW(state->sendAsFileCheck, BM_SETCHECK, BST_UNCHECKED, 0);
            } else {
                EnableWindow(state->sendAsFileCheck, item.type == ClipboardItem::Text);
                SendMessageW(state->sendAsFileCheck, BM_SETCHECK,
                    item.type == ClipboardItem::Text && state->defaultSendTextAsFile ? BST_CHECKED : BST_UNCHECKED, 0);
            }
        }
    } else {
        SetWindowTextW(state->fileNameEdit, L"");
        EnableWindow(state->fileNameEdit, FALSE);
        if (state->sendAsFileCheck != nullptr) {
            EnableWindow(state->sendAsFileCheck, FALSE);
            SendMessageW(state->sendAsFileCheck, BM_SETCHECK, BST_UNCHECKED, 0);
        }
    }
}

bool pickerItemMatches(const ClipboardItem& item, const std::wstring& query) {
    if (query.empty()) {
        return true;
    }

    if (toLower(item.title).find(query) != std::wstring::npos ||
        toLower(item.text).find(query) != std::wstring::npos ||
        toLower(item.createdAt).find(query) != std::wstring::npos ||
        toLower(typeLabel(item)).find(query) != std::wstring::npos) {
        return true;
    }

    for (size_t index = 0; index < item.files.size(); ++index) {
        if (toLower(item.files[index]).find(query) != std::wstring::npos) {
            return true;
        }
    }
    for (size_t index = 0; index < item.originalFiles.size(); ++index) {
        if (toLower(item.originalFiles[index]).find(query) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

void updatePickerSelectionFromList(PickerWindowState* state) {
    if (state == nullptr || state->listBox == nullptr) {
        return;
    }

    const int listIndex = static_cast<int>(SendMessageW(state->listBox, LB_GETCURSEL, 0, 0));
    if (listIndex >= 0 && static_cast<size_t>(listIndex) < state->filteredIndices.size()) {
        state->selectedIndex = static_cast<int>(state->filteredIndices[static_cast<size_t>(listIndex)]);
    } else {
        state->selectedIndex = -1;
    }
}

void refreshPickerList(HWND window, PickerWindowState* state) {
    if (state == nullptr || state->library == nullptr || state->listBox == nullptr) {
        return;
    }

    const int previousSelectedIndex = state->selectedIndex;
    const std::wstring query = state->searchEdit == nullptr ? L"" : toLower(trim(windowText(state->searchEdit)));
    const std::vector<ClipboardItem>& items = state->library->items();

    SendMessageW(state->listBox, LB_RESETCONTENT, 0, 0);
    state->filteredIndices.clear();

    int selectedListIndex = -1;
    for (size_t index = 0; index < items.size(); ++index) {
        if (!pickerItemMatches(items[index], query)) {
            continue;
        }

        const int listIndex = static_cast<int>(state->filteredIndices.size());
        state->filteredIndices.push_back(index);
        SendMessageW(state->listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(itemLabel(items[index], index, isPickerMultiSelected(state, index)).c_str()));
        if (static_cast<int>(index) == previousSelectedIndex) {
            selectedListIndex = listIndex;
        }
    }

    if (selectedListIndex < 0 && !state->filteredIndices.empty()) {
        selectedListIndex = 0;
    }
    if (selectedListIndex >= 0) {
        SendMessageW(state->listBox, LB_SETCURSEL, selectedListIndex, 0);
    }
    updatePickerSelectionFromList(state);
    updatePickerFileNameEdit(state);

    invalidatePickerPreview(window, state);
}

void splitPickerPreviewRects(RECT& topRect, RECT& bottomRect) {
    topRect = previewRect();
    bottomRect = previewRect();
    const int middle = (topRect.top + topRect.bottom) / 2;
    topRect.bottom = middle - 3;
    bottomRect.top = middle + 3;
}

std::wstring pickerItemPreviewBody(const ClipboardItem& item) {
    std::wstringstream body;
    if (isTxtFileItem(item)) {
        const std::wstring text = readTextFileForPreview(item.files.front());
        body << L"Loại: File TXT\n\n";
        if (!fileExists(item.files.front())) {
            body << L"Không tìm thấy file cache để preview.";
        } else if (text.empty()) {
            body << L"File trống hoặc không thể đọc nội dung.";
        } else {
            body << text;
        }
    } else if (item.type == ClipboardItem::Files) {
        body << L"Loại: File cache\n\n";
        for (size_t index = 0; index < item.files.size(); ++index) {
            body << L"Cache: " << item.files[index] << L"\n";
            if (index < item.originalFiles.size()) {
                body << L"Gốc:   " << item.originalFiles[index] << L"\n";
            }
            body << L"\n";
        }
    } else {
        body << L"Loại: Text thô\n\n" << item.text;
    }
    return body.str();
}

void drawPickerPreviewPanel(
    HDC dc,
    const RECT& rect,
    const std::wstring& title,
    const std::wstring& bodyText,
    const ClipboardItem* imageItem,
    int& scrollOffset,
    int& maxScroll) {
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rect, background);
    DeleteObject(background);
    FrameRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, titleFont));

    RECT titleRect = rect;
    titleRect.left += 14;
    titleRect.top += 8;
    titleRect.right -= 14;
    titleRect.bottom = titleRect.top + 26;
    DrawTextW(dc, title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    DeleteObject(titleFont);

    RECT bodyRect = rect;
    bodyRect.left += 14;
    bodyRect.top += 38;
    bodyRect.right -= 14;
    bodyRect.bottom -= 10;

    if (imageItem != nullptr && imageItem->type == ClipboardItem::Image && !imageItem->files.empty()) {
        scrollOffset = 0;
        maxScroll = 0;
        drawImagePreview(dc, imageItem->previewPath.empty() ? imageItem->files.front() : imageItem->previewPath, bodyRect);
        return;
    }

    RECT measureRect = bodyRect;
    measureRect.top = 0;
    measureRect.bottom = 0;
    DrawTextW(dc, bodyText.c_str(), static_cast<int>(bodyText.size()), &measureRect, DT_WORDBREAK | DT_LEFT | DT_TOP | DT_CALCRECT);

    const int visibleHeight = bodyRect.bottom - bodyRect.top;
    const int contentHeight = measureRect.bottom - measureRect.top;
    maxScroll = std::max(0, contentHeight - visibleHeight);
    scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));

    RECT drawRect = bodyRect;
    drawRect.top -= scrollOffset;
    drawRect.bottom = drawRect.top + std::max(contentHeight, visibleHeight);
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, bodyRect.left, bodyRect.top, bodyRect.right, bodyRect.bottom);
    DrawTextW(dc, bodyText.c_str(), static_cast<int>(bodyText.size()), &drawRect, DT_WORDBREAK | DT_LEFT | DT_TOP);
    RestoreDC(dc, savedDc);
}

void drawPickerPreview(HDC dc, PickerWindowState* state) {
    const std::vector<ClipboardItem>& items = state->library->items();
    if (state->selectedIndex < 0 || static_cast<size_t>(state->selectedIndex) >= items.size()) {
        drawPickerPreviewPanel(
            dc, previewRect(), L"Chọn một item ở danh sách bên trái", L"", nullptr,
            state->previewScrollOffset, state->previewMaxScroll);
        return;
    }

    const ClipboardItem& item = items[static_cast<size_t>(state->selectedIndex)];
    if (!state->multiSelectedIndices.empty()) {
        RECT multitextRect;
        RECT itemRect;
        splitPickerPreviewRects(multitextRect, itemRect);

        std::wstringstream multitextTitle;
        multitextTitle << L"Multitext (" << state->multiSelectedIndices.size() << L" mục)";
        drawPickerPreviewPanel(
            dc, multitextRect, multitextTitle.str(), combinedPickerText(state), nullptr,
            state->previewScrollOffset, state->previewMaxScroll);
        drawPickerPreviewPanel(
            dc, itemRect, L"Đang trỏ: " + item.title, pickerItemPreviewBody(item),
            item.type == ClipboardItem::Image ? &item : nullptr,
            state->itemPreviewScrollOffset, state->itemPreviewMaxScroll);
        return;
    }

    drawPickerPreviewPanel(
        dc, previewRect(), item.title, pickerItemPreviewBody(item),
        item.type == ClipboardItem::Image ? &item : nullptr,
        state->previewScrollOffset, state->previewMaxScroll);
}

bool togglePickerMultiSelection(HWND window, PickerWindowState* state) {
    if (state == nullptr || state->library == nullptr || state->selectedIndex < 0) {
        return false;
    }

    const size_t itemIndex = static_cast<size_t>(state->selectedIndex);
    const std::vector<ClipboardItem>& items = state->library->items();
    if (itemIndex >= items.size() || items[itemIndex].type != ClipboardItem::Text) {
        return false;
    }

    std::vector<size_t>::iterator selected = std::find(state->multiSelectedIndices.begin(), state->multiSelectedIndices.end(), itemIndex);
    if (selected == state->multiSelectedIndices.end()) {
        state->multiSelectedIndices.push_back(itemIndex);
    } else {
        state->multiSelectedIndices.erase(selected);
    }
    refreshPickerList(window, state);
    return true;
}

void clearPickerMultiSelection(HWND window, PickerWindowState* state) {
    if (state == nullptr || state->multiSelectedIndices.empty()) {
        return;
    }
    state->multiSelectedIndices.clear();
    refreshPickerList(window, state);
}

bool togglePickerPinnedItem(HWND window, PickerWindowState* state, size_t itemIndex) {
    if (state == nullptr || state->library == nullptr || itemIndex >= state->library->items().size()) {
        return false;
    }

    state->multiSelectedIndices.clear();
    const int newIndex = state->library->togglePinned(itemIndex);
    if (newIndex < 0) {
        return false;
    }
    state->selectedIndex = newIndex;
    refreshPickerList(window, state);
    SetFocus(state->searchEdit);
    return true;
}

void acceptPickerSelection(HWND window, PickerWindowState* state) {
    if (state == nullptr) {
        return;
    }
    updatePickerSelectionFromList(state);
    const std::vector<ClipboardItem>& items = state->library->items();
    if (!state->multiSelectedIndices.empty()) {
        state->selectedIndex = static_cast<int>(state->multiSelectedIndices.front());
        state->selectedText = combinedPickerText(state);
        state->multiText = true;
        state->selectedFileName = safeFilePart(windowText(state->fileNameEdit));
        if (state->selectedFileName.empty()) {
            state->selectedFileName = L"clipboard_multitext.txt";
        }
        state->sendTextAsFile = SendMessageW(state->sendAsFileCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->done = true;
        DestroyWindow(window);
        return;
    }
    if (state->selectedIndex >= 0 && static_cast<size_t>(state->selectedIndex) < items.size()) {
        const ClipboardItem& item = items[static_cast<size_t>(state->selectedIndex)];
        state->selectedFileName = safeFilePart(windowText(state->fileNameEdit));
        if (state->selectedFileName.empty()) {
            state->selectedFileName = defaultPickerFileName(item);
        }
        if (isTxtFileItem(item)) {
            state->extractTextFromFile = SendMessageW(state->sendAsFileCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            state->sendTextAsFile = !state->extractTextFromFile;
        } else {
            state->sendTextAsFile = item.type != ClipboardItem::Text || SendMessageW(state->sendAsFileCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        }
    }
    state->done = true;
    DestroyWindow(window);
}

LRESULT CALLBACK conversionProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    ConversionWindowState* state = reinterpret_cast<ConversionWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE: {
        state = reinterpret_cast<ConversionWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        std::wstringstream messageText;
        messageText << L"Văn bản dài " << state->characterCount << L" ký tự. Target hiện tại có thể nhận file, bạn muốn gửi dạng .txt không?";
        createControl(window, L"STATIC", messageText.str().c_str(), 0, 0, 18, 18, 450, 42, 0);
        createControl(window, L"STATIC", L"Tên file TXT:", 0, 0, 18, 78, 110, 22, 0);
        state->fileNameEdit = createControl(window, L"EDIT", state->defaultFileName.c_str(), ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 130, 75, 330, 24, IdConversionName);
        createControl(window, L"BUTTON", L"Chuyển thành TXT", BS_DEFPUSHBUTTON, 0, 70, 138, 130, 30, IdConversionConvert);
        createControl(window, L"BUTTON", L"Dán text gốc", 0, 0, 210, 138, 110, 30, IdConversionRaw);
        createControl(window, L"BUTTON", L"Hủy", 0, 0, 330, 138, 80, 30, IdConversionCancel);
        SetFocus(state->fileNameEdit);
        SendMessageW(state->fileNameEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (state != nullptr && LOWORD(wParam) == IdConversionConvert) {
            state->choice.handled = true;
            state->choice.convertToFile = true;
            state->choice.fileNamePrefix = safeFilePart(windowText(state->fileNameEdit));
            if (state->choice.fileNamePrefix.empty()) {
                state->choice.fileNamePrefix = L"pasted_text";
            }
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        if (state != nullptr && LOWORD(wParam) == IdConversionRaw) {
            state->choice.handled = true;
            state->choice.convertToFile = false;
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        if (state != nullptr && LOWORD(wParam) == IdConversionCancel) {
            state->choice.handled = false;
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (state != nullptr) {
            state->done = true;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (state != nullptr) {
            state->done = true;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK pickerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    PickerWindowState* state = reinterpret_cast<PickerWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE: {
        state = reinterpret_cast<PickerWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        createControl(window, L"STATIC", L"Clipboard library", 0, 0, 16, 15, 220, 22, 0);
        createControl(window, L"STATIC", L"Preview", 0, 0, 345, 15, 220, 22, 0);
        createControl(window, L"STATIC", L"Tìm kiếm:", 0, 0, 16, 44, 66, 22, 0);
        state->searchEdit = createControl(window, L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 82, 41, 244, 24, IdPickerSearch);
        createControl(window, L"STATIC", L"Tên file:", 0, 0, 345, 365, 120, 22, 0);
        state->fileNameEdit = createControl(window, L"EDIT", L"clipboard_text", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 455, 362, 275, 24, IdPickerFileName);
        state->sendAsFileCheck = createControl(window, L"BUTTON", L"Với text: gửi dạng TXT (bỏ tick để dán text thô khi nhập liệu)", BS_AUTOCHECKBOX, 0, 345, 395, 385, 24, IdPickerSendAsFile);
        createControl(window, L"BUTTON", L"Chọn", BS_DEFPUSHBUTTON, 0, 550, 442, 86, 30, IdPickerChoose);
        createControl(window, L"BUTTON", L"Hủy", 0, 0, 645, 442, 86, 30, IdPickerCancel);
        state->listBox = createControl(window, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, WS_EX_CLIENTEDGE, 16, 73, 310, 302, IdPickerList, false);
        refreshPickerList(window, state);
        SetFocus(state->searchEdit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IdPickerSearch && HIWORD(wParam) == EN_CHANGE && state != nullptr) {
            refreshPickerList(window, state);
            return 0;
        }
        if (LOWORD(wParam) == IdPickerList && HIWORD(wParam) == LBN_SELCHANGE && state != nullptr) {
            updatePickerSelectionFromList(state);
            updatePickerFileNameEdit(state);
            invalidatePickerPreview(window, state);
            return 0;
        }
        if (LOWORD(wParam) == IdPickerList && HIWORD(wParam) == LBN_DBLCLK && state != nullptr) {
            acceptPickerSelection(window, state);
            return 0;
        }
        if (LOWORD(wParam) == IdPickerChoose && state != nullptr) {
            acceptPickerSelection(window, state);
            return 0;
        }
        if (LOWORD(wParam) == IdPickerCancel && state != nullptr) {
            state->selectedIndex = -1;
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        if (state != nullptr) {
            drawPickerPreview(dc, state);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        if (state != nullptr) {
            state->selectedIndex = -1;
            state->done = true;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (state != nullptr) {
            state->done = true;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

Ui::Ui(HINSTANCE instance, HWND owner, AppState& state, ClipboardLibrary& library)
    : instance_(instance), owner_(owner), state_(state), library_(library) {}

bool Ui::addTrayIcon() {
    NOTIFYICONDATAW data;
    ZeroMemory(&data, sizeof(data));
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = TrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy(data.szTip, L"ClipboardTxtApp", sizeof(data.szTip) / sizeof(wchar_t) - 1);
    return Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void Ui::removeTrayIcon() {
    NOTIFYICONDATAW data;
    ZeroMemory(&data, sizeof(data));
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = TrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void Ui::showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, MenuSettings, L"Cài đặt");
    AppendMenuW(menu, MF_STRING, MenuOpenLibrary, L"Chọn từ clipboard library");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MenuExit, L"Thoát");

    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(owner_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, owner_, nullptr);
    DestroyMenu(menu);

    if (command == MenuSettings) {
        showSettings();
    } else if (command == MenuOpenLibrary) {
        chooseClipboardItem();
    } else if (command == MenuExit) {
        PostMessageW(owner_, WM_CLOSE, 0, 0);
    }
}

void Ui::showSettings() {
    const wchar_t* className = L"ClipboardTxtAppSettingsWindow";
    WNDCLASSW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.lpfnWndProc = settingsProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&windowClass);

    SettingsWindowState state;
    state.ui = this;

    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, className, L"ClipboardTxtApp - Cài đặt", WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, SettingsWidth, SettingsHeight, owner_, nullptr, instance_, &state);
    if (window == nullptr) {
        MessageBoxW(owner_, L"Không thể mở cửa sổ cài đặt.", L"ClipboardTxtApp", MB_OK | MB_ICONERROR);
        return;
    }

    centerWindow(window, nullptr);
    EnableWindow(owner_, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message;
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (handleSelectAllShortcut(message)) {
            continue;
        }
        if (GetFocus() == state.allowlistEdit && message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            DispatchMessageW(&message);
            continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner_, TRUE);
    SetForegroundWindow(owner_);

    if (state.saved) {
        MessageBoxW(owner_, L"Đã lưu cài đặt.", L"ClipboardTxtApp", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }
}

TextConversionChoice Ui::askTextConversion(size_t characterCount, const std::wstring& defaultFileName) {
    const wchar_t* className = L"ClipboardTxtAppConversionWindow";
    WNDCLASSW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.lpfnWndProc = conversionProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&windowClass);

    ConversionWindowState state;
    state.characterCount = characterCount;
    state.defaultFileName = safeFilePart(defaultFileName.empty() ? L"pasted_text" : defaultFileName);

    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, className, L"Chuyển văn bản dài thành TXT?", WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, ConversionWidth, ConversionHeight, owner_, nullptr, instance_, &state);
    if (window == nullptr) {
        return TextConversionChoice();
    }

    centerWindow(window, nullptr);
    EnableWindow(owner_, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message;
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (handleSelectAllShortcut(message)) {
            continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner_, TRUE);
    SetForegroundWindow(owner_);
    return state.choice;
}


void Ui::showInfo(const std::wstring& message) {
    MessageBoxW(owner_, message.c_str(), L"ClipboardTxtApp", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

ClipboardPickResult Ui::chooseClipboardItem(bool sendTextAsFileByDefault) {
    ClipboardPickResult result;
    if (library_.items().empty()) {
        MessageBoxW(owner_, L"Chưa có mục clipboard nào được lưu.", L"ClipboardTxtApp", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return result;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    const bool gdiplusStarted = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) == Gdiplus::Ok;

    const wchar_t* className = L"ClipboardTxtAppPickerWindow";
    WNDCLASSW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.lpfnWndProc = pickerProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&windowClass);

    PickerWindowState state;
    state.library = &library_;
    state.config = &state_.config();
    state.defaultSendTextAsFile = sendTextAsFileByDefault;
    state.sendTextAsFile = sendTextAsFileByDefault;

    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, className, L"Chọn item clipboard", WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, PickerWidth, PickerHeight, owner_, nullptr, instance_, &state);
    if (window == nullptr) {
        if (gdiplusStarted) {
            Gdiplus::GdiplusShutdown(gdiplusToken);
        }
        return result;
    }

    centerWindow(window, nullptr);
    EnableWindow(owner_, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    if (state.searchEdit != nullptr) {
        SetFocus(state.searchEdit);
    }

    MSG message;
    bool shiftDown = false;
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
            (message.wParam == VK_SHIFT || message.wParam == VK_LSHIFT || message.wParam == VK_RSHIFT)) {
            shiftDown = true;
        } else if ((message.message == WM_KEYUP || message.message == WM_SYSKEYUP) &&
                   (message.wParam == VK_SHIFT || message.wParam == VK_LSHIFT || message.wParam == VK_RSHIFT)) {
            shiftDown = false;
        }
        if (handleSelectAllShortcut(message)) {
            continue;
        }
        if (message.message == WM_MOUSEWHEEL &&
            (message.hwnd == window || IsChild(window, message.hwnd))) {
            POINT point;
            point.x = static_cast<short>(LOWORD(message.lParam));
            point.y = static_cast<short>(HIWORD(message.lParam));
            ScreenToClient(window, &point);

            int* scrollOffset = &state.previewScrollOffset;
            int* maxScroll = &state.previewMaxScroll;
            RECT scrollRect = previewRect();
            if (!state.multiSelectedIndices.empty()) {
                RECT multitextRect;
                RECT itemRect;
                splitPickerPreviewRects(multitextRect, itemRect);
                if (PtInRect(&itemRect, point)) {
                    scrollRect = itemRect;
                    scrollOffset = &state.itemPreviewScrollOffset;
                    maxScroll = &state.itemPreviewMaxScroll;
                } else {
                    scrollRect = multitextRect;
                }
            }

            if (PtInRect(&scrollRect, point) && *maxScroll > 0) {
                const int wheelSteps = GET_WHEEL_DELTA_WPARAM(message.wParam) / WHEEL_DELTA;
                *scrollOffset = std::max(0, std::min(
                    *scrollOffset - wheelSteps * 48, *maxScroll));
                RECT invalidRect = previewRect();
                InvalidateRect(window, &invalidRect, TRUE);
                continue;
            }
        }
        if (message.message == WM_LBUTTONDOWN && message.hwnd == state.listBox) {
            const DWORD hit = static_cast<DWORD>(SendMessageW(state.listBox, LB_ITEMFROMPOINT, 0, message.lParam));
            if (HIWORD(hit) != 0) {
                clearPickerMultiSelection(window, &state);
                continue;
            }
            const int listIndex = static_cast<int>(LOWORD(hit));
            if (((message.wParam & MK_SHIFT) != 0 || (GetKeyState(VK_SHIFT) & 0x8000) != 0) &&
                listIndex >= 0 && static_cast<size_t>(listIndex) < state.filteredIndices.size()) {
                const size_t itemIndex = state.filteredIndices[static_cast<size_t>(listIndex)];
                SendMessageW(state.listBox, LB_SETCURSEL, listIndex, 0);
                updatePickerSelectionFromList(&state);
                if (togglePickerPinnedItem(window, &state, itemIndex)) {
                    continue;
                }
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                bool handled = false;
                if (listIndex >= 0 && static_cast<size_t>(listIndex) < state.filteredIndices.size()) {
                    const size_t itemIndex = state.filteredIndices[static_cast<size_t>(listIndex)];
                    const std::vector<ClipboardItem>& items = state.library->items();
                    if (itemIndex < items.size() && items[itemIndex].type == ClipboardItem::Text) {
                        SendMessageW(state.listBox, LB_SETCURSEL, listIndex, 0);
                        updatePickerSelectionFromList(&state);
                        togglePickerMultiSelection(window, &state);
                        handled = true;
                    }
                }
                if (handled) {
                    continue;
                }
            }
        }
        if (message.message == WM_LBUTTONDOWN && message.hwnd == window) {
            clearPickerMultiSelection(window, &state);
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
            (message.hwnd == window || IsChild(window, message.hwnd))) {
            state.selectedIndex = -1;
            state.done = true;
            DestroyWindow(window);
            continue;
        }
        if (message.message == WM_KEYDOWN &&
            (message.wParam == VK_UP || message.wParam == VK_DOWN) &&
            (message.hwnd == window || IsChild(window, message.hwnd))) {
            const int itemCount = static_cast<int>(SendMessageW(state.listBox, LB_GETCOUNT, 0, 0));
            if (itemCount > 0) {
                int listIndex = static_cast<int>(SendMessageW(state.listBox, LB_GETCURSEL, 0, 0));
                if (listIndex < 0) {
                    listIndex = 0;
                } else if (message.wParam == VK_UP && listIndex > 0) {
                    --listIndex;
                } else if (message.wParam == VK_DOWN && listIndex + 1 < itemCount) {
                    ++listIndex;
                }
                SendMessageW(state.listBox, LB_SETCURSEL, listIndex, 0);
                updatePickerSelectionFromList(&state);
                updatePickerFileNameEdit(&state);
                SetFocus(state.searchEdit);
                invalidatePickerPreview(window, &state);
            }
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
            (shiftDown || (GetKeyState(VK_SHIFT) & 0x8000) != 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) &&
            (message.hwnd == window || IsChild(window, message.hwnd))) {
            const int listIndex = static_cast<int>(SendMessageW(state.listBox, LB_GETCURSEL, 0, 0));
            if (listIndex >= 0 && static_cast<size_t>(listIndex) < state.filteredIndices.size()) {
                const size_t itemIndex = state.filteredIndices[static_cast<size_t>(listIndex)];
                togglePickerPinnedItem(window, &state, itemIndex);
            }
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
            (message.hwnd == window || IsChild(window, message.hwnd))) {
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                togglePickerMultiSelection(window, &state);
                continue;
            }
            const HWND focused = GetFocus();
            if (focused == state.sendAsFileCheck && IsWindowEnabled(state.sendAsFileCheck)) {
                SendMessageW(state.sendAsFileCheck, BM_CLICK, 0, 0);
            } else if (GetDlgCtrlID(focused) == IdPickerCancel) {
                state.selectedIndex = -1;
                state.done = true;
                DestroyWindow(window);
            } else if (SendMessageW(state.listBox, LB_GETCURSEL, 0, 0) != LB_ERR) {
                acceptPickerSelection(window, &state);
            }
            continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner_, TRUE);
    if (gdiplusStarted) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
    }
    result.selectedIndex = state.selectedIndex;
    result.selectedFileName = state.selectedFileName;
    result.selectedText = state.selectedText;
    result.sendTextAsFile = state.sendTextAsFile;
    result.multiText = state.multiText;
    result.extractTextFromFile = state.extractTextFromFile;
    return result;
}
