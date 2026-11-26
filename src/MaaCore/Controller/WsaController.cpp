#ifdef _WIN32

#include "WsaController.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <regex>
#include <thread>

#include "Config/GeneralConfig.h"
#include "SwipeHelper.hpp"
#include "Utils/Logger.hpp"

namespace asst
{
bool WsaController::connect(const std::string& adb_path, const std::string& address, const std::string& config)
{
    LogTraceFunction;

    m_wsa_win32_input_ready = false;
    m_adb_touch_fallback = false;
    m_cached_task_bounds.reset();
    m_last_win32_rect.reset();
    release_minitouch();

#ifdef ASST_DEBUG
    if (config == "DEBUG") {
        m_inited = true;
        return true;
    }
#endif

    if (!AdbController::connect(adb_path, address, config)) {
        return false;
    }

    if (m_wsa_extras.inited() && m_wsa_extras.input_available()) {
        m_wsa_win32_input_ready = true;
        LogInfo << "WsaController Win32 input ready" << VAR(m_width) << VAR(m_height);
        return true;
    }

    // 未启用 Win32 输入时，优先探测 MaaTouch；若 Android 15/16 等高版本 WSA 上 MaaTouch 不可用，
    // 仍允许回落到 ADB input（避免连接直接中断，且保留 WsaExtras WGC 截图加速）。
    m_minitouch_available = probe_minitouch();
    if (!m_minitouch_available) {
        LogWarn << "WsaController MaaTouch probe failed, falling back to ADB input";
        m_adb_touch_fallback = true;
    }

    return true;
}

bool WsaController::use_wsa_win32_input() const noexcept
{
    return m_wsa_win32_input_ready && m_wsa_extras.inited() && m_wsa_extras.input_available() &&
           m_wsa_extras.has_target_window();
}

Point WsaController::map_to_win32_client(const Point& p) const
{
    auto size_opt = m_wsa_extras.client_size();
    if (!size_opt || m_width <= 0 || m_height <= 0) {
        return p;
    }
    const auto [cw, ch] = *size_opt;
    int x = static_cast<int>(std::llround(static_cast<double>(p.x) * cw / m_width));
    int y = static_cast<int>(std::llround(static_cast<double>(p.y) * ch / m_height));
    return Point(std::clamp(x, 0, cw - 1), std::clamp(y, 0, ch - 1));
}

std::optional<WsaWindowRect> WsaController::query_task_bounds()
{
    if (m_conn_ctx.package_name.empty()) {
        return std::nullopt;
    }

    std::string cmd = m_conn_ctx.replace_cmd("[Adb] -s [AdbSerial] shell dumpsys activity activities");
    auto ret = call_command(cmd, 10'000, false);
    if (!ret || ret->empty()) {
        return std::nullopt;
    }

    static const std::regex kTaskHeaderRegex(R"(\* Task\{[^#]*#\d+[^}]*A=\d+:([^\s\}]+)[^}]*?mode=(\w+))");
    static const std::regex kBoundsRegex(R"(mBounds=Rect\((-?\d+),\s*(-?\d+)\s*-\s*(-?\d+),\s*(-?\d+)\))");

    bool matched_pkg = false;
    bool is_freeform = false;
    std::istringstream iss(*ret);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::smatch m;
        if (std::regex_search(line, m, kTaskHeaderRegex)) {
            matched_pkg = (m[1].str() == m_conn_ctx.package_name);
            is_freeform = (m[2].str() == "freeform");
            continue;
        }
        if (!matched_pkg) {
            continue;
        }
        if (std::regex_search(line, m, kBoundsRegex)) {
            if (!is_freeform) {
                return std::nullopt;
            }
            WsaWindowRect rect {
                std::stoi(m[1].str()),
                std::stoi(m[2].str()),
                std::stoi(m[3].str()),
                std::stoi(m[4].str()),
            };
            if (rect.valid()) {
                LogInfo << "WsaController task bounds" << VAR(m_conn_ctx.package_name) << VAR(rect.left)
                        << VAR(rect.top) << VAR(rect.right) << VAR(rect.bottom);
                return rect;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool WsaController::refresh_task_bounds_if_needed()
{
    if (m_adb.screencap_method != AdbProperty::ScreencapMethod::WsaExtras) {
        return false;
    }

    // 仅当 Windows 侧 WSA 窗口外框发生拖动/缩放，或尚未缓存时，才查询一次 dumpsys。
    auto cur_win_rect = m_wsa_extras.window_rect();
    if (m_cached_task_bounds && cur_win_rect && m_last_win32_rect && *cur_win_rect == *m_last_win32_rect) {
        return true;
    }

    m_last_win32_rect = cur_win_rect;
    m_cached_task_bounds = query_task_bounds();
    return m_cached_task_bounds.has_value();
}

Point WsaController::map_to_task_display(const Point& p)
{
    if (m_width <= 0 || m_height <= 0) {
        return p;
    }
    if (!refresh_task_bounds_if_needed() || !m_cached_task_bounds) {
        return p;
    }

    const auto& r = *m_cached_task_bounds;
    int x = r.left + static_cast<int>(std::llround(static_cast<double>(p.x) * r.width() / m_width));
    int y = r.top + static_cast<int>(std::llround(static_cast<double>(p.y) * r.height() / m_height));
    return Point(std::clamp(x, r.left, r.right - 1), std::clamp(y, r.top, r.bottom - 1));
}

bool WsaController::click(const Point& p)
{
    if (use_wsa_win32_input()) {
        Point cp = map_to_win32_client(p);
        LogTrace << "wsa win32 click" << VAR(p.x) << VAR(p.y) << VAR(cp.x) << VAR(cp.y);
        bool down = m_wsa_extras.touch_down(0, cp.x, cp.y);
        std::this_thread::sleep_for(std::chrono::milliseconds(TouchHoldMs));
        bool up = m_wsa_extras.touch_up(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(TouchHoldMs));
        return down && up;
    }

    Point mapped = map_to_task_display(p);
    if (m_adb_touch_fallback) {
        return AdbController::click(mapped);
    }
    return MaatouchController::click(mapped);
}

bool WsaController::swipe(
    const Point& p1,
    const Point& p2,
    int duration,
    SwipeExtraDirection extra_swipe,
    double slope_in,
    double slope_out,
    bool with_pause)
{
    if (use_wsa_win32_input()) {
        Point c1 = map_to_win32_client(p1);
        Point c2 = map_to_win32_client(p2);
        auto size_opt = m_wsa_extras.client_size();
        const int cw = size_opt ? size_opt->first : m_width;
        const int ch = size_opt ? size_opt->second : m_height;

        if (!m_wsa_extras.touch_down(0, c1.x, c1.y)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(TouchHoldMs));

        const auto& opt = Config.get_options();
        auto tick_start = std::chrono::steady_clock::now();
        int move_step = 0;
        auto move_func = [this, &tick_start, &move_step](int x, int y) {
            bool ret = m_wsa_extras.touch_move(0, x, y);
            high_res_sleep_until(tick_start + ++move_step * std::chrono::milliseconds(SwipeIntervalMs));
            return ret;
        };
        auto bounds_check = [cw, ch](int x, int y) { return x >= 0 && x < cw && y >= 0 && y < ch; };

        bool need_pause = with_pause && use_swipe_with_pause();
        std::future<void> pause_future;
        auto pause_check = [&opt](int cur_x, int cur_y, int start_x, int start_y) {
            return std::sqrt(std::pow(cur_x - start_x, 2) + std::pow(cur_y - start_y, 2)) >
                   opt.swipe_with_pause_required_distance;
        };
        auto pause_action = [this, &pause_future]() {
            pause_future = std::async(std::launch::async, [this]() { press_esc(); });
        };

        auto do_swipe = [&](int sx, int sy, int ex, int ey, int dur) -> bool {
            tick_start = std::chrono::steady_clock::now();
            move_step = 0;
            if (need_pause) {
                return interpolate_swipe_with_pause(
                    sx,
                    sy,
                    ex,
                    ey,
                    dur,
                    SwipeIntervalMs,
                    slope_in,
                    slope_out,
                    move_func,
                    bounds_check,
                    pause_check,
                    [&]() {
                        need_pause = false;
                        pause_action();
                    });
            }
            return interpolate_swipe(
                sx,
                sy,
                ex,
                ey,
                dur,
                SwipeIntervalMs,
                slope_in,
                slope_out,
                move_func,
                bounds_check);
        };

        const int actual_duration = duration <= 0 ? opt.minitouch_swipe_default_duration : duration;
        if (!do_swipe(c1.x, c1.y, c2.x, c2.y, actual_duration)) {
            LogWarn << "WsaController Win32 swipe failed during movement";
            m_wsa_extras.touch_up(0);
            return false;
        }

        if (extra_swipe != SwipeExtraDirection::None && opt.minitouch_extra_swipe_duration > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(opt.minitouch_swipe_extra_end_delay));
            const auto offset = extra_swipe_offset(extra_swipe, opt.minitouch_extra_swipe_dist);
            if (!do_swipe(c2.x, c2.y, c2.x + offset.x, c2.y + offset.y, opt.minitouch_extra_swipe_duration)) {
                LogWarn << "WsaController Win32 extra swipe failed";
            }
        }

        bool up = m_wsa_extras.touch_up(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(TouchHoldMs));
        return up;
    }

    Point m1 = map_to_task_display(p1);
    Point m2 = map_to_task_display(p2);
    if (m_adb_touch_fallback) {
        return AdbController::swipe(m1, m2, duration, extra_swipe, slope_in, slope_out, with_pause);
    }
    return MaatouchController::swipe(m1, m2, duration, extra_swipe, slope_in, slope_out, with_pause);
}

bool WsaController::inject_input_event(const InputEvent& event)
{
    if (use_wsa_win32_input()) {
        switch (event.type) {
        case InputEvent::Type::TOUCH_DOWN: {
            Point cp = map_to_win32_client(event.point);
            return m_wsa_extras.touch_down(event.pointerId, cp.x, cp.y);
        }
        case InputEvent::Type::TOUCH_MOVE: {
            Point cp = map_to_win32_client(event.point);
            return m_wsa_extras.touch_move(event.pointerId, cp.x, cp.y);
        }
        case InputEvent::Type::TOUCH_UP:
            return m_wsa_extras.touch_up(event.pointerId);
        case InputEvent::Type::KEY_DOWN:
            return m_wsa_extras.key_down(event.keycode);
        case InputEvent::Type::KEY_UP:
            return m_wsa_extras.key_up(event.keycode);
        case InputEvent::Type::WAIT_MS:
            std::this_thread::sleep_for(std::chrono::milliseconds(event.milisec));
            return true;
        case InputEvent::Type::TOUCH_RESET:
        case InputEvent::Type::COMMIT:
            return true;
        default:
            return false;
        }
    }

    if (m_adb_touch_fallback) {
        return false;
    }

    InputEvent mapped = event;
    if (event.type == InputEvent::Type::TOUCH_DOWN || event.type == InputEvent::Type::TOUCH_MOVE) {
        mapped.point = map_to_task_display(event.point);
    }
    return MaatouchController::inject_input_event(mapped);
}

bool WsaController::input(const std::string& text)
{
    if (use_wsa_win32_input() && m_wsa_extras.input_text(text)) {
        return true;
    }
    return MaatouchController::input(text);
}

bool WsaController::press_esc()
{
    if (use_wsa_win32_input() && m_wsa_extras.click_key(VK_ESCAPE)) {
        return true;
    }
    if (m_adb_touch_fallback) {
        return AdbController::press_esc();
    }
    return MaatouchController::press_esc();
}

ControlFeat::Feat WsaController::support_features() const noexcept
{
    if (use_wsa_win32_input()) {
        return ControlFeat::PRECISE_SWIPE | ControlFeat::SWIPE_WITH_PAUSE;
    }
    if (m_adb_touch_fallback) {
        return ControlFeat::NONE;
    }
    return MaatouchController::support_features();
}

std::optional<std::string> WsaController::reconnect(const std::string& cmd, int64_t timeout, bool recv_by_socket)
{
    LogTraceFunction;

    m_cached_task_bounds.reset();
    m_last_win32_rect.reset();

    if (use_wsa_win32_input() || m_adb_touch_fallback) {
        return AdbController::reconnect(cmd, timeout, recv_by_socket);
    }
    return MaatouchController::reconnect(cmd, timeout, recv_by_socket);
}

void WsaController::on_display_rotated()
{
    m_cached_task_bounds.reset();
    m_last_win32_rect.reset();
    if (!use_wsa_win32_input() && !m_adb_touch_fallback) {
        MaatouchController::on_display_rotated();
    }
}

void WsaController::clear_info() noexcept
{
    m_wsa_win32_input_ready = false;
    m_adb_touch_fallback = false;
    m_cached_task_bounds.reset();
    m_last_win32_rect.reset();
    MaatouchController::clear_info();
}
} // namespace asst

#endif // _WIN32
