#pragma once

#ifdef _WIN32

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "MaaUtils/NoWarningCVMat.hpp"
#include "Win32ControlUnitLoader.h"

namespace asst
{
struct WsaWindowRect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const noexcept { return right - left; }
    int height() const noexcept { return bottom - top; }
    bool valid() const noexcept { return right > left && bottom > top; }
    bool operator==(const WsaWindowRect&) const noexcept = default;
};

// WSA 窗口级高速截图与输入增强（复用 MaaWin32ControlUnit.dll 的 WGC FramePool）
// 兼容 Android 11 / 12L / 13 / 16 等各版本 WSA，不依赖 Root 或第三方外部库。
class WsaExtras
{
public:
    WsaExtras();
    ~WsaExtras();

    WsaExtras(const WsaExtras&) = delete;
    WsaExtras& operator=(const WsaExtras&) = delete;
    WsaExtras(WsaExtras&&) = delete;
    WsaExtras& operator=(WsaExtras&&) = delete;

    bool inited() const noexcept { return m_inited; }

    bool init(const std::string& package_name, const std::string& window_name, bool enable_input);
    void set_package_name(const std::string& package_name);
    bool reload();
    void uninit();

    // 是否已绑定到正在运行的目标 WSA 应用窗口
    bool has_target_window() const noexcept;
    void* hwnd() const noexcept { return m_hwnd; }

    // 捕获目标 WSA 应用窗口的客户区画面（BGR，WGC FramePool）
    std::optional<cv::Mat> screencap();

    // 目标窗口当前客户区尺寸（像素）与屏幕外框位置（用于检测拖动/缩放）
    std::optional<std::pair<int, int>> client_size() const;
    std::optional<WsaWindowRect> window_rect() const;

    // 宿主侧 Win32 输入（可选）
    bool input_available() const noexcept { return m_input_available; }
    bool touch_down(int contact, int x, int y);
    bool touch_move(int contact, int x, int y);
    bool touch_up(int contact);
    bool key_down(int vk_code);
    bool key_up(int vk_code);
    bool click_key(int vk_code);
    bool input_text(const std::string& text);

    // 供外部查询指定包名对应的 WSA 窗口句柄
    static void* find_wsa_window(const std::string& package_name, const std::string& custom_window_name);

private:
    bool ensure_unit();
    bool attach_hwnd(void* hwnd);
    void destroy_unit();

private:
    std::unique_ptr<Win32ControlUnitLoader> m_loader;
    void* m_unit_handle = nullptr;
    void* m_hwnd = nullptr;

    std::string m_package_name;
    std::string m_window_name;
    bool m_input_enabled = false;
    bool m_input_available = false;
    bool m_inited = false;
};
} // namespace asst

#endif // _WIN32
