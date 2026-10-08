#include "arps/receiver.h"

#ifdef _WIN32
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

struct Args {
    std::string host = "127.0.0.1";
    std::uint16_t port = 27183;
    std::string compression = "lz4_block";
    std::uint32_t max_fps = 30;
    std::uint32_t display_id = 0;
    bool keep_screen_on = true;
    std::string capture_mode = "auto";
    std::string exit_power_mode = "restore_previous";
    std::string stream_mode = "push";
    std::string serial;
    std::string adb = "adb";
    std::string apk;
    std::string remote_apk = "/data/local/tmp/arps-device.apk";
};

struct Stats {
    bool have_frame = false;
    arps::ArpsFrameMeta meta;
    std::size_t payload_len = 0;
    double packet_read_ms = -1.0;
    double decode_ms = -1.0;
    double render_ms = -1.0;
    arps::ArpsDeviceTimings device_timings;
    double fps = 0.0;
    int frames_since_tick = 0;
    std::chrono::steady_clock::time_point fps_tick = std::chrono::steady_clock::now();
    std::string state = "WAITING";
};

bool ParseBool(const std::string& value, bool* out) {
    if (value == "true" || value == "1") {
        *out = true;
        return true;
    }
    if (value == "false" || value == "0") {
        *out = false;
        return true;
    }
    return false;
}

bool ParseU16(const std::string& value, std::uint16_t* out) {
    char* end = nullptr;
    long parsed = strtol(value.c_str(), &end, 10);
    if (*end != '\0' || parsed <= 0 || parsed > 65535) {
        return false;
    }
    *out = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseU32(const std::string& value, std::uint32_t* out) {
    char* end = nullptr;
    unsigned long parsed = strtoul(value.c_str(), &end, 10);
    if (*end != '\0' || parsed > 0xfffffffful) {
        return false;
    }
    *out = static_cast<std::uint32_t>(parsed);
    return true;
}

void PrintUsage(const char* argv0) {
    std::cerr
            << "Usage: " << argv0 << " [--host=127.0.0.1] [--port=27183]\n"
            << "       [--compression=raw|lz4_block] [--max-fps=30]\n"
            << "       [--display-id=0]\n"
            << "       [--keep-screen-on=true|false]\n"
            << "       [--capture-mode=auto|hardware|bitmap]\n"
            << "       [--stream-mode=push|pull]\n"
            << "       [--exit-power-mode=restore_previous|keep_on|turn_off]\n"
            << "       [--serial=<adb-serial>] [--apk=<path>] [--adb=adb]\n";
}

bool ParseArgs(int argc, char** argv, Args* args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        }
        if (arg.rfind("--", 0) != 0 || arg.find('=') == std::string::npos) {
            std::cerr << "Invalid argument: " << arg << "\n";
            PrintUsage(argv[0]);
            return false;
        }
        std::size_t split = arg.find('=');
        std::string key = arg.substr(2, split - 2);
        std::string value = arg.substr(split + 1);
        if (key == "host") {
            args->host = value;
        } else if (key == "port") {
            if (!ParseU16(value, &args->port)) {
                std::cerr << "Invalid --port\n";
                return false;
            }
        } else if (key == "compression") {
            if (value != "raw" && value != "lz4_block") {
                std::cerr << "Invalid --compression\n";
                return false;
            }
            args->compression = value;
        } else if (key == "max-fps") {
            if (!ParseU32(value, &args->max_fps) || args->max_fps > 240) {
                std::cerr << "Invalid --max-fps\n";
                return false;
            }
        } else if (key == "display-id") {
            if (!ParseU32(value, &args->display_id)) {
                std::cerr << "Invalid --display-id\n";
                return false;
            }
        } else if (key == "keep-screen-on") {
            if (!ParseBool(value, &args->keep_screen_on)) {
                std::cerr << "Invalid --keep-screen-on\n";
                return false;
            }
        } else if (key == "capture-mode") {
            if (value != "auto" && value != "hardware" && value != "bitmap") {
                std::cerr << "Invalid --capture-mode\n";
                return false;
            }
            args->capture_mode = value;
        } else if (key == "exit-power-mode") {
            if (value != "restore_previous" && value != "keep_on" && value != "turn_off") {
                std::cerr << "Invalid --exit-power-mode\n";
                return false;
            }
            args->exit_power_mode = value;
        } else if (key == "stream-mode") {
            if (value != "push" && value != "pull") {
                std::cerr << "Invalid --stream-mode\n";
                return false;
            }
            args->stream_mode = value;
        } else if (key == "serial") {
            args->serial = value;
        } else if (key == "adb") {
            args->adb = value;
        } else if (key == "apk") {
            args->apk = value;
        } else if (key == "remote-apk") {
            args->remote_apk = value;
        } else {
            std::cerr << "Unknown argument: --" << key << "\n";
            return false;
        }
    }
    return true;
}

std::string PosixShellQuote(const std::string& value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

#ifdef _WIN32
std::string WindowsShellQuote(const std::string& value) {
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char ch : value) {
        if (ch == '\\') {
            ++backslashes;
        } else if (ch == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            backslashes = 0;
            out.push_back(ch);
        }
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
            static_cast<int>(value.size()), nullptr, 0);
    if (len <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
            static_cast<int>(value.size()), out.data(), len);
    return out;
}

std::string WideToUtf8(const wchar_t* value) {
    if (!value || *value == L'\0') {
        return std::string();
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), len, nullptr, nullptr);
    out.pop_back();
    return out;
}

std::string WindowsErrorMessage(DWORD error) {
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (len == 0 || !buffer) {
        return "Windows error " + std::to_string(error);
    }
    std::string message = WideToUtf8(buffer);
    LocalFree(buffer);
    while (!message.empty()
            && (message.back() == '\r' || message.back() == '\n' || message.back() == '.')) {
        message.pop_back();
    }
    return message + " (" + std::to_string(error) + ")";
}

std::string WindowsCommandLine(const std::vector<std::string>& argv) {
    std::string command;
    for (const auto& arg : argv) {
        if (!command.empty()) {
            command.push_back(' ');
        }
        command += WindowsShellQuote(arg);
    }
    return command;
}
#endif

std::string HostCommandLine(const std::vector<std::string>& argv) {
#ifdef _WIN32
    return WindowsCommandLine(argv);
#else
    std::string command;
    for (const auto& arg : argv) {
        if (!command.empty()) {
            command.push_back(' ');
        }
        command += PosixShellQuote(arg);
    }
    return command;
#endif
}

std::string AndroidShellQuote(const std::string& value) {
    return PosixShellQuote(value);
}

std::string DefaultApkPath(const char* argv0) {
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(),
                static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            break;
        }
        if (static_cast<std::size_t>(len) < buffer.size()) {
            std::error_code ec;
            std::filesystem::path exe = std::filesystem::canonical(
                    std::filesystem::path(buffer.data()), ec);
            if (ec) {
                exe = std::filesystem::absolute(std::filesystem::path(buffer.data()), ec);
            }
            if (!ec) {
                return (exe.parent_path() / "arps-device.apk").string();
            }
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size > 0) {
        std::vector<char> buffer(size + 1, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            std::error_code ec;
            std::filesystem::path exe = std::filesystem::canonical(buffer.data(), ec);
            if (!ec) {
                return (exe.parent_path() / "arps-device.apk").string();
            }
        }
    }
#endif
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::absolute(argv0, ec);
    if (ec) {
        exe = argv0;
    }
    return (exe.parent_path() / "arps-device.apk").string();
}

int RunHostCommand(const std::vector<std::string>& argv, const std::string& label) {
    std::string command = HostCommandLine(argv);
    std::cout << label << ": " << command << "\n";
#ifdef _WIN32
    std::wstring command_w = Utf8ToWide(command);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command_w.data(), nullptr, nullptr, FALSE, 0, nullptr,
                nullptr, &startup, &process)) {
        std::cerr << label << " failed to start: " << WindowsErrorMessage(GetLastError())
                  << "\n";
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        std::cerr << label << " failed to read exit code: "
                  << WindowsErrorMessage(GetLastError()) << "\n";
        exit_code = 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code > static_cast<DWORD>(INT_MAX)) {
        return 1;
    }
    return static_cast<int>(exit_code);
#else
    return std::system(command.c_str());
#endif
}

bool RunHostCommandOk(const std::vector<std::string>& argv, const std::string& label) {
    int rc = RunHostCommand(argv, label);
    if (rc != 0) {
        std::cerr << label << " failed with exit code " << rc << "\n";
        return false;
    }
    return true;
}

std::vector<std::string> AdbArgs(const Args& args) {
    std::vector<std::string> command{args.adb};
    if (!args.serial.empty()) {
        command.push_back("-s");
        command.push_back(args.serial);
    }
    return command;
}

bool PrepareDevice(const Args& args, const std::string& apk_path) {
    std::vector<std::string> reverse = AdbArgs(args);
    reverse.push_back("reverse");
    reverse.push_back("tcp:" + std::to_string(args.port));
    reverse.push_back("tcp:" + std::to_string(args.port));
    if (!RunHostCommandOk(reverse, "adb reverse")) {
        return false;
    }

    std::vector<std::string> push = AdbArgs(args);
    push.push_back("push");
    push.push_back(apk_path);
    push.push_back(args.remote_apk);
    return RunHostCommandOk(push, "adb push");
}

std::vector<std::string> BuildDeviceCommand(const Args& args, const std::string& session_token) {
    std::ostringstream shell;
    shell << "CLASSPATH=" << AndroidShellQuote(args.remote_apk)
          << " app_process / com.visotc.ARPS.Main"
          << " --connect-host=127.0.0.1"
          << " --connect-port=" << args.port;
    shell << " --stream-mode=" << args.stream_mode;
    shell << " --session-token=" << session_token;

    std::vector<std::string> command = AdbArgs(args);
    command.push_back("shell");
    command.push_back(shell.str());
    return command;
}

void RemoveReverse(const Args& args) {
    if (args.serial.empty()) {
        return;
    }
    std::vector<std::string> command = AdbArgs(args);
    command.push_back("reverse");
    command.push_back("--remove");
    command.push_back("tcp:" + std::to_string(args.port));
    RunHostCommand(command, "adb reverse --remove");
}

const std::uint8_t* Glyph(char ch) {
    static const std::uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const std::uint8_t glyphs[][7] = {
            {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},  // 0
            {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},  // 1
            {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},  // 2
            {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},  // 3
            {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},  // 4
            {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},  // 5
            {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e},  // 6
            {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
            {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},  // 8
            {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e},  // 9
    };
    static const std::uint8_t letters[][7] = {
            {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},  // A
            {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},  // B
            {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},  // C
            {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},  // D
            {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},  // E
            {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},  // F
            {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},  // G
            {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},  // H
            {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},  // I
            {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c},  // J
            {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},  // K
            {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},  // L
            {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},  // M
            {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},  // N
            {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},  // O
            {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},  // P
            {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},  // Q
            {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},  // R
            {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},  // S
            {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // T
            {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},  // U
            {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},  // V
            {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a},  // W
            {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},  // X
            {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},  // Y
            {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},  // Z
    };
    static const std::uint8_t colon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    static const std::uint8_t dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
    static const std::uint8_t dash[7] = {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
    static const std::uint8_t slash[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    static const std::uint8_t percent[7] = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03};
    static const std::uint8_t equal[7] = {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00};
    if (ch >= '0' && ch <= '9') {
        return glyphs[ch - '0'];
    }
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (ch >= 'A' && ch <= 'Z') {
        return letters[ch - 'A'];
    }
    switch (ch) {
        case ':':
            return colon;
        case '.':
            return dot;
        case '-':
        case '_':
            return dash;
        case '/':
            return slash;
        case '%':
            return percent;
        case '=':
            return equal;
        default:
            return blank;
    }
}

void DrawText(SDL_Renderer* renderer, int x, int y, const std::string& text, int scale,
        SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int cursor_x = x;
    for (char ch : text) {
        const std::uint8_t* rows = Glyph(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (rows[row] & (1 << (4 - col))) {
                    SDL_Rect pixel{cursor_x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        cursor_x += 6 * scale;
    }
}

std::string Fixed(double value, int digits = 1) {
    if (value < 0.0 || !std::isfinite(value)) {
        return "-";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(digits) << value;
    return out.str();
}

std::string SizeMb(std::size_t bytes) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << static_cast<double>(bytes) / (1024.0 * 1024.0) << "MB";
    return out.str();
}

std::vector<std::string> OverlayLines(const Stats& stats) {
    std::vector<std::string> lines;
    lines.push_back("ARPS HOST");
    lines.push_back("STATE " + stats.state);
    if (!stats.have_frame) {
        lines.push_back("NO FRAME");
        return lines;
    }
    std::ostringstream line;
    line << "FPS " << Fixed(stats.fps, 1);
    lines.push_back(line.str());
    line.str("");
    line << "FRAME " << stats.meta.frame_no;
    lines.push_back(line.str());
    line.str("");
    line << "SIZE " << stats.meta.width << "X" << stats.meta.height;
    lines.push_back(line.str());
    line.str("");
    double ratio = stats.meta.uncompressed_len > 0
            ? static_cast<double>(stats.payload_len) * 100.0 / stats.meta.uncompressed_len
            : -1.0;
    line << arps::CompressionTypeName(stats.meta.compression_type) << " "
         << SizeMb(stats.payload_len) << "/" << SizeMb(stats.meta.uncompressed_len)
         << " " << Fixed(ratio, 1) << "%";
    lines.push_back(line.str());
    lines.push_back("READ " + Fixed(stats.packet_read_ms, 2) + "MS");
    lines.push_back("DECODE " + Fixed(stats.decode_ms, 2) + "MS");
    lines.push_back("RENDER " + Fixed(stats.render_ms, 2) + "MS");
    lines.push_back("DEV CAP " + Fixed(stats.device_timings.capture_ms, 2) + "MS");
    lines.push_back("DEV COPY " + Fixed(stats.device_timings.copy_ms, 2) + "MS");
    lines.push_back("DEV COMP " + Fixed(stats.device_timings.compress_ms, 2) + "MS");
    double write_ms = stats.device_timings.write_ms >= 0.0
            ? stats.device_timings.write_ms
            : stats.device_timings.previous_write_ms;
    lines.push_back("DEV WRITE " + Fixed(write_ms, 2) + "MS");
    return lines;
}

void UpdateFps(Stats* stats) {
    stats->frames_since_tick++;
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = now - stats->fps_tick;
    if (elapsed.count() >= 1.0) {
        stats->fps = stats->frames_since_tick / elapsed.count();
        stats->frames_since_tick = 0;
        stats->fps_tick = now;
    }
}

SDL_Rect FitRect(int window_w, int window_h, int frame_w, int frame_h) {
    if (frame_w <= 0 || frame_h <= 0) {
        return SDL_Rect{0, 0, window_w, window_h};
    }
    double scale = std::min(static_cast<double>(window_w) / frame_w,
            static_cast<double>(window_h) / frame_h);
    int w = static_cast<int>(frame_w * scale);
    int h = static_cast<int>(frame_h * scale);
    return SDL_Rect{(window_w - w) / 2, (window_h - h) / 2, w, h};
}

void DrawOverlay(SDL_Renderer* renderer, const SDL_Rect& frame_rect, const Stats& stats) {
    std::vector<std::string> lines = OverlayLines(stats);
    int scale = 2;
    int line_h = 9 * scale;
    int text_w = 0;
    for (const auto& line : lines) {
        text_w = std::max(text_w, static_cast<int>(line.size()) * 6 * scale);
    }
    int panel_w = text_w + 16;
    int panel_h = static_cast<int>(lines.size()) * line_h + 14;
    int x = frame_rect.x + frame_rect.w - panel_w - 12;
    int y = frame_rect.y + 12;
    if (x < frame_rect.x + 8) {
        x = frame_rect.x + 8;
    }
    if (panel_w > frame_rect.w - 16) {
        panel_w = frame_rect.w - 16;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 8, 10, 14, 178);
    SDL_Rect panel{x, y, panel_w, panel_h};
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 50);
    SDL_RenderDrawRect(renderer, &panel);
    SDL_Color color{236, 244, 255, 255};
    int text_y = y + 8;
    for (const auto& line : lines) {
        DrawText(renderer, x + 8, text_y, line, scale, color);
        text_y += line_h;
    }
}

bool PumpEvents(bool* running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            *running = false;
        } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            *running = false;
        }
    }
    return *running;
}

int RunGui(arps::ArpsReceiver* receiver, bool pull_mode) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("ARPS host debug",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = nullptr;
    int texture_w = 0;
    int texture_h = 0;
    Stats stats;
    stats.state = pull_mode ? "PULL" : "STREAMING";
    bool running = true;

    if (pull_mode) {
        std::string error;
        if (!receiver->RequestFrame(&error)) {
            std::cerr << "FRAME_REQUEST failed: " << error << "\n";
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    while (running) {
        PumpEvents(&running);
        arps::ArpsReadResult result = receiver->ReadNext(5);
        if (result.status == arps::ArpsReadStatus::Frame) {
            const arps::ArpsFrame& frame = result.frame;
            if (!texture || texture_w != static_cast<int>(frame.meta.width)
                    || texture_h != static_cast<int>(frame.meta.height)) {
                if (texture) {
                    SDL_DestroyTexture(texture);
                }
                texture_w = static_cast<int>(frame.meta.width);
                texture_h = static_cast<int>(frame.meta.height);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                        SDL_TEXTUREACCESS_STREAMING, texture_w, texture_h);
                if (!texture) {
                    std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << "\n";
                    running = false;
                    break;
                }
                SDL_SetWindowSize(window, std::min(texture_w, 1400), std::min(texture_h, 1000));
            }
            SDL_UpdateTexture(texture, nullptr, frame.argb8888,
                    static_cast<int>(frame.meta.row_bytes));
            stats.have_frame = true;
            stats.meta = frame.meta;
            stats.payload_len = frame.bitmap_payload_len;
            stats.packet_read_ms = frame.packet_read_ms;
            stats.decode_ms = frame.decode_ms;
            stats.device_timings = frame.device_timings;
            stats.state = pull_mode ? "PULL" : "STREAMING";
            UpdateFps(&stats);
            if (pull_mode) {
                std::string error;
                if (!receiver->RequestFrame(&error)) {
                    std::cerr << "FRAME_REQUEST failed: " << error << "\n";
                    stats.state = "REQ ERR";
                    running = false;
                }
            }
        } else if (result.status == arps::ArpsReadStatus::Timeout) {
            // Keep rendering the latest complete frame.
        } else if (result.status == arps::ArpsReadStatus::PowerState) {
            const arps::ArpsPowerState& power = result.power_state;
            std::cout << "POWER_STATE request_id=" << power.request_id
                      << " ok=" << (power.ok ? "true" : "false")
                      << " screen_on=" << (power.screen_on ? "true" : "false")
                      << " wake_lock_held_by_arps="
                      << (power.wake_lock_held_by_arps ? "true" : "false");
            if (!power.error.empty()) {
                std::cout << " error=" << power.error;
            }
            std::cout << "\n";
            stats.state = power.ok ? "POWER" : "PWR ERR";
        } else if (result.status == arps::ArpsReadStatus::Error) {
            std::cerr << "Device ERROR: " << result.json << "\n";
            stats.state = "ERROR";
            running = false;
        } else if (result.status == arps::ArpsReadStatus::Stop) {
            stats.state = "STOP";
            running = false;
        } else if (result.status == arps::ArpsReadStatus::Closed) {
            stats.state = "CLOSED";
            running = false;
        } else if (result.status == arps::ArpsReadStatus::ProtocolError) {
            std::cerr << "Protocol error: " << result.message << "\n";
            stats.state = "PROTO ERR";
            running = false;
        }

        auto render_start = std::chrono::steady_clock::now();
        int window_w = 0;
        int window_h = 0;
        SDL_GetRendererOutputSize(renderer, &window_w, &window_h);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_Rect frame_rect{0, 0, window_w, window_h};
        if (texture && stats.have_frame) {
            frame_rect = FitRect(window_w, window_h, texture_w, texture_h);
            SDL_RenderCopy(renderer, texture, nullptr, &frame_rect);
        }
        DrawOverlay(renderer, frame_rect, stats);
        SDL_RenderPresent(renderer);
        std::chrono::duration<double, std::milli> render_elapsed =
                std::chrono::steady_clock::now() - render_start;
        stats.render_ms = render_elapsed.count();
    }

    if (texture) {
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, &args)) {
        return 2;
    }
    if (args.apk.empty()) {
        args.apk = DefaultApkPath(argv[0]);
    }

    arps::ArpsReceiver receiver;
    std::string error;
    if (!receiver.Listen(args.host, args.port, &error)) {
        std::cerr << "Listen failed: " << error << "\n";
        return 1;
    }
    std::cout << "Listening on " << args.host << ":" << args.port << "\n";

    std::thread device_thread;
    if (!args.serial.empty()) {
        if (!std::filesystem::is_regular_file(args.apk)) {
            std::cerr << "APK not found next to arps-host-debug: " << args.apk << "\n"
                      << "Pass --apk=/path/to/arps-device.apk or copy arps-device.apk next to "
                      << "the arps-host-debug binary.\n";
            return 1;
        }
        if (!PrepareDevice(args, args.apk)) {
            RemoveReverse(args);
            return 1;
        }
        std::string session_token = arps::GenerateSessionToken();
        receiver.SetExpectedSessionToken(session_token);
        std::vector<std::string> device_command = BuildDeviceCommand(args, session_token);
        device_thread = std::thread([device_command]() {
            int rc = RunHostCommand(device_command, "adb shell");
            std::cout << "device process exited with code " << rc << "\n";
        });
    }

    if (!receiver.AcceptOnce(-1, &error)) {
        std::cerr << "Accept failed: " << error << "\n";
        RemoveReverse(args);
        if (device_thread.joinable()) {
            device_thread.join();
        }
        return 1;
    }
    std::cout << "Client connected\n";

    arps::ArpsReadResult hello = receiver.ReadNext(5000);
    if (hello.status != arps::ArpsReadStatus::Hello) {
        std::cerr << "Expected HELLO, got status=" << static_cast<int>(hello.status)
                  << " message=" << hello.message << "\n";
        RemoveReverse(args);
        if (device_thread.joinable()) {
            device_thread.join();
        }
        return 1;
    }
    std::cout << "HELLO " << hello.json << "\n";

    arps::ArpsStartOptions start;
    start.display_id = args.display_id;
    start.compression = args.compression;
    start.max_fps = args.max_fps;
    start.keep_screen_on = args.keep_screen_on;
    start.capture_mode = args.capture_mode;
    start.exit_power_mode = args.exit_power_mode;
    start.stream_mode = args.stream_mode;
    if (!receiver.SendStart(start, &error)) {
        std::cerr << "Send START failed: " << error << "\n";
        RemoveReverse(args);
        if (device_thread.joinable()) {
            device_thread.join();
        }
        return 1;
    }
    std::cout << "START " << start.ToJson() << "\n";

    arps::ArpsReadResult ready = receiver.ReadNext(30000);
    if (ready.status != arps::ArpsReadStatus::Ready) {
        std::cerr << "Expected READY, got status=" << static_cast<int>(ready.status)
                  << " message=" << ready.message;
        if (!ready.json.empty()) {
            std::cerr << " json=" << ready.json;
        }
        std::cerr << "\n";
        receiver.Close();
        RemoveReverse(args);
        if (device_thread.joinable()) {
            device_thread.join();
        }
        return 1;
    }
    std::cout << "READY " << ready.json << "\n";

    int rc = RunGui(&receiver, args.stream_mode == "pull");
    receiver.Close();
    RemoveReverse(args);
    if (device_thread.joinable()) {
        device_thread.join();
    }
    return rc;
}
