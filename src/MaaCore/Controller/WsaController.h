#pragma once

#ifdef _WIN32

#include <optional>
#include <string>

#include "MaatouchController.h"

namespace asst
{
// WSA 专用控制器：
// - 截图由基类 AdbController 的 WsaExtras（WGC FramePool 按窗口抓帧）提供，延迟 2~8ms 且无黑边。
// - 触控坐标自动在窗口客户区坐标系与 WSA freeform Task bounds（Display 0）之间换算：
//   1) 若用户启用 WsaExtras 触控且窗口句柄有效，优先经 MaaWin32ControlUnit 后台消息注入（不抢焦点、无 Root 依赖）；
//   2) 否则经 Android 侧 MaaTouch（不可用时回落 ADB input）并按游戏 Task 实时 mBounds 映射到 Display 0 绝对坐标。
class WsaController : public MaatouchController
{
public:
    WsaController(const AsstCallback& callback, Assistant* inst, PlatformType type) :
        MaatouchController(callback, inst, type)
    {
    }

    WsaController(const WsaController&) = delete;
    WsaController(WsaController&&) = delete;
    virtual ~WsaController() override = default;

    virtual bool connect(const std::string& adb_path, const std::string& address, const std::string& config) override;

    virtual bool screencap(cv::Mat& image_payload, bool allow_reconnect = false) override;

    virtual std::pair<int, int> get_screen_res() const noexcept override;

    virtual bool click(const Point& p) override;

    virtual bool swipe(
        const Point& p1,
        const Point& p2,
        int duration = 0,
        SwipeExtraDirection extra_swipe = SwipeExtraDirection::None,
        double slope_in = 1,
        double slope_out = 1,
        bool with_pause = false) override;

    virtual bool inject_input_event(const InputEvent& event) override;

    virtual bool input(const std::string& text) override;

    virtual bool press_esc() override;

    virtual ControlFeat::Feat support_features() const noexcept override;

    WsaController& operator=(const WsaController&) = delete;
    WsaController& operator=(WsaController&&) = delete;

protected:
    virtual std::optional<std::string> reconnect(const std::string& cmd, int64_t timeout, bool recv_by_socket) override;

    virtual void on_display_rotated() override;

    virtual void clear_info() noexcept override;

private:
    bool use_wsa_win32_input() const noexcept;
    Point map_to_win32_client(const Point& p) const;
    Point map_to_task_display(const Point& p);
    bool refresh_task_bounds_if_needed();
    std::optional<WsaWindowRect> query_task_bounds();

    bool m_wsa_win32_input_ready = false;
    bool m_adb_touch_fallback = false;
    std::pair<int, int> m_wsa_frame_res { 0, 0 };
    std::optional<WsaWindowRect> m_cached_task_bounds;
    std::optional<WsaWindowRect> m_last_win32_rect;
};
} // namespace asst

#endif // _WIN32
