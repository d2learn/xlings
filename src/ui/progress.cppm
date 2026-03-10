module;

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/color.hpp"

export module xlings.ui:progress;

import std;
import :theme;

export namespace xlings::ui {

enum class Phase { Pending, Downloading, Extracting, Installing, Configuring, Done, Failed };

struct StatusEntry {
    std::string name;
    Phase phase { Phase::Pending };
    float progress { 0.0f };
    std::string message;
};

std::string phase_icon(Phase p) {
    switch (p) {
        case Phase::Pending:      return theme::icon::pending;
        case Phase::Downloading:  return theme::icon::downloading;
        case Phase::Extracting:   return theme::icon::extracting;
        case Phase::Installing:   return theme::icon::installing;
        case Phase::Configuring:  return theme::icon::configuring;
        case Phase::Done:         return theme::icon::done;
        case Phase::Failed:       return theme::icon::failed;
    }
    return "?";
}

ftxui::Color phase_color(Phase p) {
    switch (p) {
        case Phase::Pending:      return theme::dim_color();
        case Phase::Downloading:  return theme::cyan();
        case Phase::Extracting:   return theme::amber();
        case Phase::Installing:   return theme::amber();
        case Phase::Configuring:  return theme::amber();
        case Phase::Done:         return theme::green();
        case Phase::Failed:       return theme::red();
    }
    return theme::dim_color();
}

std::string phase_label(Phase p) {
    switch (p) {
        case Phase::Pending:      return "pending";
        case Phase::Downloading:  return "downloading";
        case Phase::Extracting:   return "extracting";
        case Phase::Installing:   return "installing";
        case Phase::Configuring:  return "configuring";
        case Phase::Done:         return "done";
        case Phase::Failed:       return "failed";
    }
    return "unknown";
}

// Build a name element where the name itself is the progress bar:
// characters colored from left to right based on progress percentage.
// The frontier character (at splitPos) blinks in orange as active download indicator.
// Progress maps only to the actual name characters; trailing padding is always dim.
// nameWidth: total padded width for alignment.
ftxui::Element name_as_progress(const std::string& name, float progress,
                                ftxui::Color litColor, ftxui::Color dimColor,
                                std::size_t nameWidth, bool isBold,
                                bool showCursor = false) {
    using namespace ftxui;

    // Clamp progress
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    // Split only on actual name length (not padding)
    auto nameLen = name.size();
    auto splitPos = static_cast<std::size_t>(progress * static_cast<float>(nameLen));
    if (splitPos > nameLen) splitPos = nameLen;

    // Padding spaces always in dimColor
    std::size_t padCount = (nameWidth > nameLen) ? (nameWidth - nameLen) : 0;
    std::string padding(padCount, ' ');

    // Three parts: lit | cursor (orange+blink) | dim
    std::string litPart = name.substr(0, splitPos);
    std::string cursorPart;
    std::string dimNamePart;

    if (showCursor && splitPos < nameLen) {
        cursorPart = name.substr(splitPos, 1);
        dimNamePart = name.substr(splitPos + 1);
    } else {
        dimNamePart = name.substr(splitPos);
    }

    Element litEl = text(litPart) | color(litColor);
    Element dimEl = text(dimNamePart + padding) | color(dimColor);
    if (isBold) {
        litEl = litEl | bold;
        dimEl = dimEl | bold;
    }

    Element result;
    if (!cursorPart.empty()) {
        Element cursorEl = text(cursorPart)
            | color(theme::amber()) | bold | blink;
        result = hbox({ litEl, cursorEl, dimEl });
    } else {
        result = hbox({ litEl, dimEl });
    }
    // Enforce exact width for alignment across rows
    return result | size(WIDTH, EQUAL, static_cast<int>(nameWidth));
}

// Render a static snapshot of install progress to stdout
void print_progress(std::span<const StatusEntry> entries) {
    using namespace ftxui;

    // Compute max name width for alignment
    std::size_t nameWidth = 20;
    for (auto& e : entries) {
        if (e.name.size() > nameWidth) nameWidth = e.name.size();
    }
    nameWidth += 2; // padding

    // Status label width for alignment
    constexpr std::size_t statusWidth = 8;

    Elements rows;
    for (auto& e : entries) {
        auto pc = phase_color(e.phase);
        auto icon = text(" " + phase_icon(e.phase) + " ") | color(pc);

        Element nameEl;
        std::string statusStr;

        if (e.phase == Phase::Pending) {
            nameEl = name_as_progress(e.name, 0.0f,
                theme::dim_color(), theme::border_color(), nameWidth, false);
            statusStr = "pending";
        } else if (e.phase == Phase::Downloading || e.phase == Phase::Extracting ||
                   e.phase == Phase::Installing || e.phase == Phase::Configuring) {
            float pct = e.progress;
            nameEl = name_as_progress(e.name, pct,
                pc, theme::border_color(), nameWidth, true, true);
            if (pct > 0.0f) {
                int whole = static_cast<int>(pct * 100.0f);
                int frac = static_cast<int>(pct * 1000.0f) % 10;
                statusStr = std::to_string(whole) + "." + std::to_string(frac) + "%";
            } else {
                statusStr = phase_label(e.phase);
            }
        } else if (e.phase == Phase::Done) {
            nameEl = name_as_progress(e.name, 1.0f,
                theme::green(), theme::green(), nameWidth, true);
            statusStr = "done";
        } else { // Failed
            nameEl = name_as_progress(e.name, 1.0f,
                theme::red(), theme::red(), nameWidth, true);
            statusStr = "failed";
            if (!e.message.empty()) statusStr += ": " + e.message;
        }

        // Right-pad status for alignment
        while (statusStr.size() < statusWidth) statusStr = " " + statusStr;

        auto statusEl = text(" " + statusStr);
        if (e.phase == Phase::Done) statusEl = statusEl | color(theme::green());
        else if (e.phase == Phase::Failed) statusEl = statusEl | color(theme::red()) | bold;
        else if (e.phase == Phase::Pending) statusEl = statusEl | color(theme::dim_color());
        else statusEl = statusEl | color(theme::dim_color());

        rows.push_back(hbox({ icon, nameEl, statusEl }));
    }
    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

// ─── Download progress rendering ───
// Data structure mirroring xim::TaskProgress (avoids circular dependency)
struct DownloadProgressEntry {
    std::string name;
    double totalBytes { 0.0 };
    double downloadedBytes { 0.0 };
    bool started  { false };
    bool finished { false };
    bool success  { false };
};

std::string format_eta(int seconds) {
    if (seconds < 0) return "";
    if (seconds < 60) return std::to_string(seconds) + "s";
    int min = seconds / 60;
    int sec = seconds % 60;
    return std::to_string(min) + "m" + std::to_string(sec) + "s";
}

std::string format_speed(double bytesPerSec) {
    if (bytesPerSec < 1024.0)
        return std::to_string(static_cast<int>(bytesPerSec)) + " B/s";
    if (bytesPerSec < 1024.0 * 1024.0) {
        int kb = static_cast<int>(bytesPerSec / 1024.0 * 10.0);
        return std::to_string(kb / 10) + "." + std::to_string(kb % 10) + " KB/s";
    }
    int mb = static_cast<int>(bytesPerSec / (1024.0 * 1024.0) * 10.0);
    return std::to_string(mb / 10) + "." + std::to_string(mb % 10) + " MB/s";
}

// Render download progress using FTXUI themed elements.
// Called from a TUI refresh thread. Outputs to stdout.
// Render download progress. prevLines > 0 means move cursor up and overwrite.
// All output is batched into a single write to eliminate flicker.
// Returns the number of terminal lines rendered.
int render_download_progress(std::span<const DownloadProgressEntry> progState,
                             std::size_t nameWidth,
                             double elapsedSec,
                             bool sizesReady,
                             int prevLines = 0) {
    using namespace ftxui;
    constexpr std::size_t statusWidth = 8;

    Elements rows;
    double totalBytes = 0.0;
    double totalDownloaded = 0.0;

    for (auto& p : progState) {
        Element icon;
        Element nameEl;
        std::string statusStr;

        totalBytes += p.totalBytes;

        if (!p.started) {
            icon = text("    " + std::string(theme::icon::pending) + " ")
                | color(theme::dim_color());
            nameEl = name_as_progress(p.name, 0.0f,
                theme::dim_color(), theme::border_color(), nameWidth, false);
            statusStr = "pending";
        } else if (!p.finished) {
            float pct = (p.totalBytes > 0)
                ? static_cast<float>(p.downloadedBytes / p.totalBytes)
                : 0.0f;
            totalDownloaded += p.downloadedBytes;
            icon = text("    " + std::string(theme::icon::downloading) + " ")
                | color(theme::cyan());
            nameEl = name_as_progress(p.name, pct,
                theme::cyan(), theme::border_color(), nameWidth, true, true);
            if (pct > 0.0f) {
                int whole = static_cast<int>(pct * 100.0f);
                int frac = static_cast<int>(pct * 1000.0f) % 10;
                statusStr = std::to_string(whole) + "." + std::to_string(frac) + "%";
            } else {
                statusStr = "0.0%";
            }
        } else if (p.success) {
            totalDownloaded += p.totalBytes;
            icon = text("    " + std::string(theme::icon::done) + " ")
                | color(theme::green());
            nameEl = name_as_progress(p.name, 1.0f,
                theme::green(), theme::green(), nameWidth, true);
            statusStr = "done";
        } else {
            totalDownloaded += p.totalBytes;
            icon = text("    " + std::string(theme::icon::failed) + " ")
                | color(theme::red()) | bold;
            nameEl = name_as_progress(p.name, 1.0f,
                theme::red(), theme::red(), nameWidth, true);
            statusStr = "failed";
        }

        while (statusStr.size() < statusWidth) statusStr = " " + statusStr;

        auto statusEl = text(" " + statusStr);
        if (p.finished && p.success) statusEl = statusEl | color(theme::green());
        else if (p.finished) statusEl = statusEl | color(theme::red()) | bold;
        else if (!p.started) statusEl = statusEl | color(theme::dim_color());
        else statusEl = statusEl | color(theme::dim_color());

        rows.push_back(hbox({ icon, nameEl, statusEl }));
    }

    // Overall progress bar
    float overallPct = 0.0f;
    std::string speedStr;
    std::string etaStr;

    if (sizesReady && totalBytes > 0.0) {
        overallPct = static_cast<float>(totalDownloaded / totalBytes);
        if (overallPct > 1.0f) overallPct = 1.0f;

        if (elapsedSec > 0.5 && totalDownloaded > 0.0) {
            double speed = totalDownloaded / elapsedSec;
            speedStr = "  " + format_speed(speed);
        }

        if (overallPct > 0.01f && overallPct < 1.0f && elapsedSec > 1.0) {
            double speed = totalDownloaded / elapsedSec;
            if (speed > 0.0) {
                double remainingBytes = totalBytes - totalDownloaded;
                int remainingSec = static_cast<int>(remainingBytes / speed);
                etaStr = "  ETA " + format_eta(remainingSec);
            }
        }
    }

    int pctWhole = static_cast<int>(overallPct * 100.0f);
    int pctFrac = static_cast<int>(overallPct * 1000.0f) % 10;
    std::string pctStr = std::to_string(pctWhole) + "." + std::to_string(pctFrac) + "%";

    rows.push_back(text(""));
    rows.push_back(hbox({
        text("  " + std::string(theme::icon::arrow) + " ") | color(theme::cyan()),
        gauge(overallPct) | size(WIDTH, EQUAL, 30) | color(theme::cyan()),
        text("  " + pctStr) | bold | color(theme::text_color()),
        text(speedStr) | color(theme::cyan()),
        text(etaStr) | color(theme::dim_color()),
    }));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);

    // Build single output buffer: cursor-up + content + clear trailing + newline
    std::string output;
    if (prevLines > 0) {
        output += "\033[" + std::to_string(prevLines) + "A\r";
    }
    output += screen.ToString();
    output += "\033[J\n";  // clear any leftover lines below, then newline

    std::cout << output << std::flush;

    return screen.dimy();
}

} // namespace xlings::ui
