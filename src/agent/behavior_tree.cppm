export module xlings.agent.behavior_tree;

import std;

namespace xlings::agent {

// ─── Time helper ───

export auto steady_now_ms() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

export auto format_duration(std::int64_t ms) -> std::string {
    if (ms < 0) return "0ms";
    if (ms < 1000) return std::to_string(ms) + "ms";
    if (ms < 60000) return std::format("{:.1f}s", ms / 1000.0);
    return std::format("{:.0f}m{:.0f}s", ms / 60000.0, (ms % 60000) / 1000.0);
}

// ─── BehaviorNode — two-type system: Atom + Plan ───

export struct BehaviorNode {
    int id {0};

    // Only 2 types
    inline static constexpr int TypeAtom = 0;  // direct tool call, zero LLM
    inline static constexpr int TypePlan = 1;  // LLM decision node
    int type {TypePlan};

    // Lifecycle state
    inline static constexpr int Pending  = 0;
    inline static constexpr int Running  = 1;
    inline static constexpr int Done     = 2;
    inline static constexpr int Failed   = 3;
    inline static constexpr int Skipped  = 4;
    int state {Pending};

    // Content
    std::string name;            // task title / tool name
    std::string detail;          // description / args summary
    std::string result_summary;  // result after completion

    // Atom: tool + args (non-empty = Atom)
    std::string tool;
    std::string tool_args;

    // Time
    std::int64_t start_ms {0};
    std::int64_t end_ms {0};

    // Tree structure
    std::vector<BehaviorNode> children;

    auto is_terminal() const -> bool {
        return state == Done || state == Failed || state == Skipped;
    }

    auto is_atom() const -> bool {
        return !tool.empty();
    }
};

// ─── Internal helpers ───

auto find_node_impl(BehaviorNode& root, int id) -> BehaviorNode* {
    if (root.id == id) return &root;
    for (auto& child : root.children) {
        if (auto* found = find_node_impl(child, id)) return found;
    }
    return nullptr;
}

void finalize_impl(BehaviorNode& node, std::int64_t end_ms) {
    for (auto& child : node.children) {
        finalize_impl(child, end_ms);
    }
    if (node.state == BehaviorNode::Running) {
        node.state = BehaviorNode::Done;
        if (node.end_ms == 0) node.end_ms = end_ms;
    } else if (node.state == BehaviorNode::Pending) {
        node.state = BehaviorNode::Skipped;
    }
}

// ─── ID allocator (thread-safe, lock-free) ───

export class IdAllocator {
    static constexpr int BASE_ID = 10000;
    std::atomic<int> next_id_{BASE_ID};
public:
    auto alloc() -> int {
        return next_id_.fetch_add(1, std::memory_order_relaxed);
    }
    void reset() {
        next_id_.store(BASE_ID, std::memory_order_relaxed);
    }
};

// ─── ABehaviorTree — system-driven behavior tree with mutex protection ───

export class ABehaviorTree {
    mutable std::mutex mtx_;
    BehaviorNode root_;
    int active_node_id_ {0};
    std::string streaming_text_;

public:
    // ── Node operations ──

    void set_root(int id, const std::string& name, const std::string& detail) {
        std::lock_guard lk(mtx_);
        root_ = BehaviorNode{};
        root_.id = id;
        root_.type = BehaviorNode::TypePlan;
        root_.name = name;
        root_.detail = detail;
        root_.state = BehaviorNode::Running;
        root_.start_ms = steady_now_ms();
        active_node_id_ = id;
    }

    auto add_child(int parent_id, BehaviorNode child) -> int {
        std::lock_guard lk(mtx_);
        auto* parent = find_node_impl(root_, parent_id);
        if (!parent) parent = &root_;
        int child_id = child.id;
        parent->children.push_back(std::move(child));
        return child_id;
    }

    void set_state(int id, int state, std::int64_t now_ms) {
        std::lock_guard lk(mtx_);
        auto* node = find_node_impl(root_, id);
        if (!node) return;
        node->state = state;
        if (state == BehaviorNode::Running && node->start_ms == 0) {
            node->start_ms = now_ms;
        }
        if (node->is_terminal() && node->end_ms == 0) {
            node->end_ms = now_ms;
        }
    }

    void set_result(int id, const std::string& summary) {
        std::lock_guard lk(mtx_);
        auto* node = find_node_impl(root_, id);
        if (node) node->result_summary = summary;
    }

    void set_active(int id) {
        std::lock_guard lk(mtx_);
        active_node_id_ = id;
    }

    void skip_remaining(int parent_id, std::int64_t now_ms) {
        std::lock_guard lk(mtx_);
        auto* parent = find_node_impl(root_, parent_id);
        if (!parent) return;
        for (auto& child : parent->children) {
            if (child.state == BehaviorNode::Pending) {
                child.state = BehaviorNode::Skipped;
                child.end_ms = now_ms;
            }
        }
    }

    // ── Streaming (for TUI thinking animation) ──

    void append_streaming(std::string_view text) {
        std::lock_guard lk(mtx_);
        streaming_text_ += text;
    }

    void clear_streaming() {
        std::lock_guard lk(mtx_);
        streaming_text_.clear();
    }

    // ── Lifecycle ──

    void finalize(std::int64_t end_ms) {
        std::lock_guard lk(mtx_);
        finalize_impl(root_, end_ms);
        active_node_id_ = 0;
    }

    void reset() {
        std::lock_guard lk(mtx_);
        root_ = BehaviorNode{};
        active_node_id_ = 0;
        streaming_text_.clear();
    }

    // ── Read access (main thread, for rendering) ──

    auto snapshot() const -> BehaviorNode {
        std::lock_guard lk(mtx_);
        return root_;
    }

    auto has_streaming() const -> bool {
        std::lock_guard lk(mtx_);
        return !streaming_text_.empty();
    }

    auto streaming_text() const -> std::string {
        std::lock_guard lk(mtx_);
        return streaming_text_;
    }

    auto get_streaming_as_reply() const -> std::string {
        std::lock_guard lk(mtx_);
        return streaming_text_;
    }
};

} // namespace xlings::agent
