#ifdef _WIN32

#include "WsaExtras.h"

#include "MaaUtils/SafeWindows.hpp"

#include <objbase.h>
#include <propsys.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

#include "Utils/Logger.hpp"
#include "Utils/Platform.hpp"

namespace asst
{
namespace wsa_detail
{
std::wstring utf8_to_wide(const std::string& str)
{
    if (str.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), out.data(), len);
    return out;
}

std::string wide_to_utf8(const std::wstring& wstr)
{
    if (wstr.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::string to_lower_ascii(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains_case_insensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return false;
    }
    return to_lower_ascii(haystack).find(to_lower_ascii(needle)) != std::string::npos;
}

std::string query_process_image_name(DWORD pid)
{
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        return {};
    }
    std::wstring buf(512, L'\0');
    DWORD size = static_cast<DWORD>(buf.size());
    std::string result;
    if (QueryFullProcessImageNameW(proc, 0, buf.data(), &size) && size > 0) {
        buf.resize(size);
        result = wide_to_utf8(buf);
    }
    CloseHandle(proc);
    return result;
}

bool is_wsa_client_process(DWORD pid)
{
    std::string path = to_lower_ascii(query_process_image_name(pid));
    constexpr std::string_view kSuffix = "\\wsaclient.exe";
    return path.size() >= kSuffix.size() && path.compare(path.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

std::string get_window_title_utf8(HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) {
        return {};
    }
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    int copied = GetWindowTextW(hwnd, buf.data(), static_cast<int>(buf.size()));
    if (copied <= 0) {
        return {};
    }
    buf.resize(static_cast<size_t>(copied));
    return wide_to_utf8(buf);
}

std::string get_window_aumid(HWND hwnd)
{
    using ShGetPropStoreFn = HRESULT(WINAPI*)(HWND, REFIID, void**);
    static const auto sh_get_prop_store = []() -> ShGetPropStoreFn {
        HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
        if (!shell32) {
            shell32 = LoadLibraryW(L"shell32.dll");
        }
        if (!shell32) {
            return nullptr;
        }
        return reinterpret_cast<ShGetPropStoreFn>(GetProcAddress(shell32, "SHGetPropertyStoreForWindow"));
    }();

    if (!sh_get_prop_store) {
        return {};
    }

    IPropertyStore* store = nullptr;
    if (FAILED(sh_get_prop_store(hwnd, IID_PPV_ARGS(&store))) || !store) {
        return {};
    }

    // PKEY_AppUserModel_ID = {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5
    static constexpr PROPERTYKEY kPkeyAppUserModelId = {
        { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } },
        5,
    };

    std::string aumid;
    PROPVARIANT pv {};
    PropVariantInit(&pv);
    if (SUCCEEDED(store->GetValue(kPkeyAppUserModelId, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
        aumid = wide_to_utf8(pv.pwszVal);
    }
    PropVariantClear(&pv);
    store->Release();
    return aumid;
}

std::string get_window_class_utf8(HWND hwnd)
{
    std::wstring buf(256, L'\0');
    int copied = GetClassNameW(hwnd, buf.data(), static_cast<int>(buf.size()));
    if (copied <= 0) {
        return {};
    }
    buf.resize(static_cast<size_t>(copied));
    return wide_to_utf8(buf);
}

bool is_wsa_ignored_class(const std::string& class_name)
{
    if (class_name.empty()) {
        return false;
    }
    // 跳过 WSA 启动闪屏窗口（如 "com.hypergryph.arknights(splash)"）及输入法/辅助浮窗
    if (contains_case_insensitive(class_name, "(splash)") ||
        contains_case_insensitive(class_name, "uac_inputindicator") ||
        contains_case_insensitive(class_name, "uac input indicator")) {
        return true;
    }
    return false;
}

bool is_wsa_utility_window(const std::string& title, const std::string& aumid, const std::string& class_name)
{
    static const std::array<std::string_view, 8> kUtilityTitles = {
        "Windows Subsystem for Android",
        "Android 子系统",
        "Settings",
        "设置",
        "設定",
        "Files",
        "文件",
        "ファイル",
    };
    for (auto u : kUtilityTitles) {
        if (title == u) {
            return true;
        }
    }
    if (!aumid.empty()) {
        if (contains_case_insensitive(aumid, "com.android.settings") ||
            contains_case_insensitive(aumid, "com.android.documentsui") ||
            contains_case_insensitive(aumid, "com.microsoft.windows.homeapp")) {
            return true;
        }
    }
    if (!class_name.empty()) {
        if (contains_case_insensitive(class_name, "com.android.settings") ||
            contains_case_insensitive(class_name, "com.android.documentsui") ||
            contains_case_insensitive(class_name, "com.microsoft.windows.homeapp")) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> known_titles_for_package(const std::string& package_name)
{
    if (package_name == "com.hypergryph.arknights" || package_name == "com.hypergryph.arknights.bilibili" ||
        package_name == "tw.txwy.and.arknights") {
        return { "明日方舟" };
    }
    if (package_name == "com.YoStarJP.Arknights") {
        return { "アークナイツ", "Arknights" };
    }
    if (package_name == "com.YoStarEN.Arknights") {
        return { "Arknights" };
    }
    if (package_name == "com.YoStarKR.Arknights") {
        return { "명일방주", "Arknights" };
    }
    return {};
}

struct WsaWindowCandidate
{
    HWND hwnd = nullptr;
    std::string class_name;
    std::string title;
    std::string aumid;
    int width = 0;
    int height = 0;
};

struct EnumContext
{
    std::vector<WsaWindowCandidate> candidates;
};

BOOL CALLBACK enum_wsa_windows_proc(HWND hwnd, LPARAM lparam)
{
    auto* ctx = reinterpret_cast<EnumContext*>(lparam);
    if (!ctx || !IsWindowVisible(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || !is_wsa_client_process(pid)) {
        return TRUE;
    }

    std::string class_name = get_window_class_utf8(hwnd);
    if (is_wsa_ignored_class(class_name)) {
        return TRUE;
    }

    RECT client_rect {};
    if (!GetClientRect(hwnd, &client_rect)) {
        return TRUE;
    }
    int w = client_rect.right - client_rect.left;
    int h = client_rect.bottom - client_rect.top;
    if (w <= 0 || h <= 0) {
        // 窗口最小化时 client_rect 为 0x0，保留以便诊断或 FramePool 透明还原
        if (!IsIconic(hwnd)) {
            return TRUE;
        }
    }

    std::string title = get_window_title_utf8(hwnd);
    if (!IsIconic(hwnd) && title.empty() && (w < 200 || h < 200)) {
        return TRUE;
    }

    WsaWindowCandidate cand;
    cand.hwnd = hwnd;
    cand.class_name = std::move(class_name);
    cand.title = std::move(title);
    cand.aumid = get_window_aumid(hwnd);
    cand.width = w;
    cand.height = h;
    ctx->candidates.push_back(std::move(cand));
    return TRUE;
}
} // namespace wsa_detail

WsaExtras::WsaExtras() : m_loader(std::make_unique<Win32ControlUnitLoader>()) {}

WsaExtras::~WsaExtras()
{
    uninit();
}

bool WsaExtras::init(const std::string& package_name, const std::string& window_name, bool enable_input)
{
    LogTraceFunction;

    bool same =
        m_inited && m_package_name == package_name && m_window_name == window_name && m_input_enabled == enable_input;
    if (same && has_target_window()) {
        return true;
    }

    uninit();

    m_package_name = package_name;
    m_window_name = window_name;
    m_input_enabled = enable_input;

    if (!m_loader->loaded()) {
        if (!m_loader->load("MaaWin32ControlUnit")) {
            LogError << "WsaExtras failed to load MaaWin32ControlUnit.dll";
            return false;
        }
    }

    m_inited = true;

    // 连接时游戏窗口可能尚未启动（例如由 StartUpTask 稍后拉起），不将此处未找到窗口视为 init 失败；
    // screencap() 和 ensure_unit() 会在窗口出现后自动绑定。
    if (void* hwnd = find_wsa_window(m_package_name, m_window_name)) {
        attach_hwnd(hwnd);
    }
    else {
        LogInfo << "WsaExtras initialized, waiting for WSA window" << VAR(m_package_name) << VAR(m_window_name);
    }

    return true;
}

void WsaExtras::set_package_name(const std::string& package_name)
{
    if (m_package_name == package_name) {
        return;
    }
    m_package_name = package_name;
    destroy_unit();
}

bool WsaExtras::reload()
{
    LogTraceFunction;
    destroy_unit();
    if (!m_inited) {
        return false;
    }
    return ensure_unit();
}

void WsaExtras::uninit()
{
    m_inited = false;
    destroy_unit();
}

bool WsaExtras::has_target_window() const noexcept
{
    return m_unit_handle != nullptr && m_hwnd != nullptr && IsWindow(static_cast<HWND>(m_hwnd)) != FALSE;
}

std::optional<cv::Mat> WsaExtras::screencap()
{
    if (!m_inited) {
        return std::nullopt;
    }

    if (!ensure_unit()) {
        return std::nullopt;
    }

    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    if (!unit) {
        return std::nullopt;
    }

    cv::Mat image;
    if (!unit->screencap(image) || image.empty()) {
        LogWarn << "WsaExtras screencap failed, retrying after re-attach";
        destroy_unit();
        if (!ensure_unit()) {
            return std::nullopt;
        }
        unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
        if (!unit || !unit->screencap(image) || image.empty()) {
            LogError << "WsaExtras screencap failed after retry";
            return std::nullopt;
        }
    }

    return image;
}

std::optional<std::pair<int, int>> WsaExtras::client_size() const
{
    if (!m_hwnd || !IsWindow(static_cast<HWND>(m_hwnd))) {
        return std::nullopt;
    }
    RECT rect {};
    if (!GetClientRect(static_cast<HWND>(m_hwnd), &rect)) {
        return std::nullopt;
    }
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) {
        return std::nullopt;
    }
    return std::make_pair(w, h);
}

std::optional<WsaWindowRect> WsaExtras::window_rect() const
{
    if (!m_hwnd || !IsWindow(static_cast<HWND>(m_hwnd))) {
        return std::nullopt;
    }
    RECT rect {};
    if (!GetWindowRect(static_cast<HWND>(m_hwnd), &rect)) {
        return std::nullopt;
    }
    WsaWindowRect out { rect.left, rect.top, rect.right, rect.bottom };
    if (!out.valid()) {
        return std::nullopt;
    }
    return out;
}

bool WsaExtras::touch_down(int contact, int x, int y)
{
    if (!m_input_available || !ensure_unit()) {
        return false;
    }
    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    return unit && unit->touch_down(contact, x, y, 0);
}

bool WsaExtras::touch_move(int contact, int x, int y)
{
    if (!m_input_available || !ensure_unit()) {
        return false;
    }
    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    return unit && unit->touch_move(contact, x, y, 0);
}

bool WsaExtras::touch_up(int contact)
{
    if (!m_input_available || !ensure_unit()) {
        return false;
    }
    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    return unit && unit->touch_up(contact);
}

bool WsaExtras::key_down(int vk_code)
{
    if (!m_input_available || !ensure_unit()) {
        return false;
    }
    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    return unit && unit->key_down(vk_code);
}

bool WsaExtras::key_up(int vk_code)
{
    if (!m_input_available || !ensure_unit()) {
        return false;
    }
    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    return unit && unit->key_up(vk_code);
}

bool WsaExtras::click_key(int vk_code)
{
    if (!m_input_available || !ensure_unit()) {
        return false;
    }
    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    return unit && unit->click_key(vk_code);
}

bool WsaExtras::input_text(const std::string& text)
{
    if (!m_input_available || !ensure_unit()) {
        return false;
    }
    auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle);
    return unit && unit->input_text(text);
}

void* WsaExtras::find_wsa_window(const std::string& package_name, const std::string& custom_window_name)
{
    wsa_detail::EnumContext ctx;
    EnumWindows(&wsa_detail::enum_wsa_windows_proc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.candidates.empty()) {
        return nullptr;
    }

    // 1. 用户自定义窗口标题优先匹配（精确匹配优先于子串匹配）
    if (!custom_window_name.empty()) {
        for (const auto& c : ctx.candidates) {
            if (c.title == custom_window_name) {
                return c.hwnd;
            }
        }
        for (const auto& c : ctx.candidates) {
            if (wsa_detail::contains_case_insensitive(c.title, custom_window_name)) {
                return c.hwnd;
            }
        }
        return nullptr;
    }

    // 2. 通过窗口 AUMID 或 Win32 ClassName 精确匹配 Android 包名（不受语言/标题本地化影响）
    if (!package_name.empty()) {
        for (const auto& c : ctx.candidates) {
            if (wsa_detail::contains_case_insensitive(c.aumid, package_name)) {
                return c.hwnd;
            }
        }
        for (const auto& c : ctx.candidates) {
            if (wsa_detail::to_lower_ascii(c.class_name) == wsa_detail::to_lower_ascii(package_name)) {
                return c.hwnd;
            }
        }

        // 3. 按包名对应的已知客户端窗口标题匹配
        auto titles = wsa_detail::known_titles_for_package(package_name);
        for (const auto& expected : titles) {
            for (const auto& c : ctx.candidates) {
                if (c.title == expected || wsa_detail::contains_case_insensitive(c.title, expected)) {
                    return c.hwnd;
                }
            }
        }
    }

    // 4. 兜底：过滤 WSA 工具窗口后，若仅剩一个可见应用窗口则直接绑定
    std::vector<const wsa_detail::WsaWindowCandidate*> app_windows;
    for (const auto& c : ctx.candidates) {
        if (!wsa_detail::is_wsa_utility_window(c.title, c.aumid, c.class_name)) {
            app_windows.push_back(&c);
        }
    }
    if (app_windows.size() == 1) {
        return app_windows.front()->hwnd;
    }

    return nullptr;
}

bool WsaExtras::ensure_unit()
{
    if (has_target_window()) {
        return true;
    }

    destroy_unit();

    void* hwnd = find_wsa_window(m_package_name, m_window_name);
    if (!hwnd) {
        return false;
    }

    return attach_hwnd(hwnd);
}

bool WsaExtras::attach_hwnd(void* hwnd)
{
    LogTraceFunction;

    if (!m_loader || !m_loader->loaded()) {
        if (!m_loader || !m_loader->load("MaaWin32ControlUnit")) {
            LogError << "WsaExtras cannot load MaaWin32ControlUnit.dll";
            return false;
        }
    }

    const Win32InputMethod mouse_method = m_input_enabled ? Win32Input::PostMessage : Win32Input::None;
    const Win32InputMethod keyboard_method = m_input_enabled ? Win32Input::SendMessage : Win32Input::None;

    void* handle = m_loader->create(hwnd, Win32Screencap::FramePool, mouse_method, keyboard_method);
    if (!handle) {
        LogError << "WsaExtras failed to create Win32ControlUnit" << VAR(hwnd);
        return false;
    }

    auto* unit = static_cast<MaaFwControlUnitAPI*>(handle);
    if (!unit->connect()) {
        LogError << "WsaExtras failed to connect Win32ControlUnit" << VAR(hwnd);
        m_loader->destroy(handle);
        return false;
    }

    m_unit_handle = handle;
    m_hwnd = hwnd;
    m_input_available = m_input_enabled;

    LogInfo << "WsaExtras attached to WSA window" << VAR(hwnd)
            << VAR(wsa_detail::get_window_title_utf8(static_cast<HWND>(hwnd))) << VAR(m_input_available);
    return true;
}

void WsaExtras::destroy_unit()
{
    if (m_unit_handle && m_loader) {
        if (auto* unit = static_cast<MaaFwControlUnitAPI*>(m_unit_handle)) {
            unit->inactive();
        }
        m_loader->destroy(m_unit_handle);
        m_unit_handle = nullptr;
    }
    m_hwnd = nullptr;
    m_input_available = false;
}
} // namespace asst

#endif // _WIN32
