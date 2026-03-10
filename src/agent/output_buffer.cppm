export module xlings.agent.output_buffer;

import std;

namespace xlings::agent {

export class OutputBuffer {
    std::string content_;
    std::vector<std::string> lines_;
    std::mutex mtx_;

public:
    void set(std::string output) {
        std::lock_guard lock(mtx_);
        content_ = std::move(output);
        lines_.clear();
        // Split content into lines
        std::istringstream iss(content_);
        std::string line;
        while (std::getline(iss, line)) {
            lines_.push_back(std::move(line));
        }
    }

    auto lines(int start, int end) -> std::string {
        std::lock_guard lock(mtx_);
        if (lines_.empty()) return "";
        // Clamp to valid range (1-based input)
        start = std::max(1, start);
        end = std::min(static_cast<int>(lines_.size()), end);
        if (start > end) return "";

        std::string result;
        for (int i = start - 1; i < end; ++i) {
            result += lines_[i];
            result += '\n';
        }
        return result;
    }

    auto search(std::string_view pattern, int max_results) -> std::string {
        std::lock_guard lock(mtx_);
        std::string result;
        int count = 0;
        for (std::size_t i = 0; i < lines_.size() && count < max_results; ++i) {
            if (lines_[i].find(pattern) != std::string::npos) {
                result += std::to_string(i + 1) + ": " + lines_[i] + "\n";
                ++count;
            }
        }
        return result;
    }

    auto line_count() -> std::size_t {
        std::lock_guard lock(mtx_);
        return lines_.size();
    }

    auto content() -> std::string {
        std::lock_guard lock(mtx_);
        return content_;
    }
};

} // namespace xlings::agent
