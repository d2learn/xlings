export module xlings.agent.llm;

import std;
import mcpplibs.llmapi;
import xlings.libs.json;
import xlings.runtime.cancellation;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// ═══════════════════════════════════════════════════════════════
//  LLM Config
// ═══════════════════════════════════════════════════════════════

export struct LlmConfig {
    std::string provider;       // "anthropic" / "openai"
    std::string model;
    std::string api_key;
    std::string base_url;
    float temperature { 0.3f };
    int max_tokens { 8192 };
};

export struct ApiFormat {
    std::string name;
    std::string base_url;
};

export struct ProviderPreset {
    std::string name;
    std::string id;
    std::string default_model;
    std::string env_var;
    std::vector<ApiFormat> formats;
};

export auto infer_provider(std::string_view model) -> std::string {
    if (model.starts_with("claude"))    return "anthropic";
    if (model.starts_with("gpt"))       return "openai";
    if (model.starts_with("deepseek"))  return "openai";
    if (model.starts_with("MiniMax") || model.starts_with("minimax"))  return "openai";
    return "openai";
}

export auto default_llm_config() -> LlmConfig {
    return LlmConfig{
        .provider = "anthropic",
        .model = "claude-sonnet-4-6",
        .temperature = 0.3f,
        .max_tokens = 8192,
    };
}

auto config_from_json_(const nlohmann::json& j) -> LlmConfig {
    LlmConfig cfg;
    cfg.model = j.value("model", "claude-sonnet-4-6");
    cfg.base_url = j.value("base_url", "");
    cfg.api_key = j.value("api_key", "");
    cfg.temperature = j.value("temperature", 0.3f);
    cfg.max_tokens = j.value("max_tokens", 8192);
    cfg.provider = j.value("provider", infer_provider(cfg.model));
    return cfg;
}

export auto load_llm_json(
    const std::filesystem::path& path, std::string_view profile = ""
) -> std::optional<LlmConfig> {
    namespace fs = std::filesystem;
    if (!fs::exists(path)) return std::nullopt;
    std::ifstream in(path);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    if (!profile.empty() && j.contains("profiles") && j["profiles"].is_object()) {
        std::string pkey(profile);
        if (j["profiles"].contains(pkey)) return config_from_json_(j["profiles"][pkey]);
    }
    if (j.contains("default") && j["default"].is_object()) return config_from_json_(j["default"]);
    if (j.contains("model")) return config_from_json_(j);
    return std::nullopt;
}

export auto resolve_llm_config(
    std::string_view flag_model, std::string_view flag_base_url,
    const std::filesystem::path& llm_json_path = {}, std::string_view profile = ""
) -> LlmConfig {
    auto cfg = default_llm_config();
    if (!llm_json_path.empty()) {
        if (auto fc = load_llm_json(llm_json_path, profile)) cfg = *fc;
    }
    if (auto* v = std::getenv("XLINGS_LLM_MODEL"))    cfg.model = v;
    if (auto* v = std::getenv("XLINGS_LLM_BASE_URL")) cfg.base_url = v;
    if (cfg.provider.empty()) cfg.provider = infer_provider(cfg.model);
    if (cfg.provider == "anthropic") {
        if (auto* v = std::getenv("ANTHROPIC_API_KEY")) cfg.api_key = v;
    } else if (cfg.model.starts_with("deepseek")) {
        if (auto* v = std::getenv("DEEPSEEK_API_KEY")) cfg.api_key = v;
        else if (auto* v2 = std::getenv("OPENAI_API_KEY")) cfg.api_key = v2;
    } else if (cfg.model.starts_with("MiniMax") || cfg.model.starts_with("minimax")) {
        if (auto* v = std::getenv("MINIMAX_API_KEY")) cfg.api_key = v;
        else if (auto* v2 = std::getenv("OPENAI_API_KEY")) cfg.api_key = v2;
    } else {
        if (auto* v = std::getenv("OPENAI_API_KEY")) cfg.api_key = v;
    }
    if (!flag_model.empty()) { cfg.model = std::string(flag_model); cfg.provider = infer_provider(cfg.model); }
    if (!flag_base_url.empty()) cfg.base_url = std::string(flag_base_url);
    return cfg;
}

// NOTE: Explicit push_back to avoid GCC 15 module aggregate-init issues
export auto provider_presets() -> std::vector<ProviderPreset> {
    std::vector<ProviderPreset> v;
    ProviderPreset a; a.name="Anthropic (Claude)"; a.id="anthropic"; a.default_model="claude-sonnet-4-6";
    a.env_var="ANTHROPIC_API_KEY"; a.formats.push_back({"anthropic",""}); v.push_back(std::move(a));
    ProviderPreset o; o.name="OpenAI (GPT)"; o.id="openai"; o.default_model="gpt-5.4";
    o.env_var="OPENAI_API_KEY"; o.formats.push_back({"openai",""}); v.push_back(std::move(o));
    ProviderPreset d; d.name="DeepSeek"; d.id="deepseek"; d.default_model="deepseek-chat";
    d.env_var="DEEPSEEK_API_KEY"; d.formats.push_back({"openai","https://api.deepseek.com/v1"}); v.push_back(std::move(d));
    ProviderPreset m; m.name="MiniMax"; m.id="minimax"; m.default_model="MiniMax-M2.5";
    m.env_var="MINIMAX_API_KEY"; m.formats.push_back({"openai","https://api.minimaxi.com/v1"});
    m.formats.push_back({"anthropic","https://api.minimaxi.com/anthropic/v1"}); v.push_back(std::move(m));
    return v;
}

export bool needs_setup(const std::filesystem::path& p) {
    namespace fs = std::filesystem;
    if (!fs::exists(p)) return true;
    std::ifstream in(p); if (!in) return true;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) return true;
    if (j.contains("default") && j["default"].is_object()) return false;
    if (j.contains("profiles") && !j["profiles"].empty()) return false;
    if (j.contains("model")) return false;
    return true;
}

export void save_setup_config(
    const std::filesystem::path& path, const nlohmann::json& profiles, std::string_view default_profile
) {
    nlohmann::json j;
    j["default_profile"] = default_profile;
    std::string dp(default_profile);
    if (profiles.contains(dp)) j["default"] = profiles[dp];
    j["profiles"] = profiles;
    namespace fs = std::filesystem;
    fs::create_directories(path.parent_path());
    std::ofstream out(path); out << j.dump(2); out.close();
}

// ═══════════════════════════════════════════════════════════════
//  LLM call — provider dispatch + retry + cancellation
// ═══════════════════════════════════════════════════════════════

enum class LlmErrorKind { RateLimit, Timeout, ContextOverflow, Auth, Unknown };

auto classify_error(const std::exception& e) -> LlmErrorKind {
    std::string msg = e.what();
    if (msg.find("429") != std::string::npos || msg.find("rate_limit") != std::string::npos ||
        msg.find("Rate limit") != std::string::npos || msg.find("too many requests") != std::string::npos)
        return LlmErrorKind::RateLimit;
    if (msg.find("context_length") != std::string::npos || msg.find("max_tokens") != std::string::npos ||
        msg.find("too long") != std::string::npos || msg.find("context window") != std::string::npos)
        return LlmErrorKind::ContextOverflow;
    if (msg.find("timeout") != std::string::npos || msg.find("Timeout") != std::string::npos ||
        msg.find("timed out") != std::string::npos)
        return LlmErrorKind::Timeout;
    if (msg.find("401") != std::string::npos || msg.find("403") != std::string::npos ||
        msg.find("authentication") != std::string::npos || msg.find("invalid_api_key") != std::string::npos)
        return LlmErrorKind::Auth;
    return LlmErrorKind::Unknown;
}

template<typename Provider, typename ProviderConfig>
auto llm_call_worker(
    ProviderConfig pcfg, std::vector<llm::Message> msgs, llm::ChatParams& params,
    std::function<void(std::string_view)> on_chunk, bool has_stream_cb, CancellationToken* cancel
) -> llm::ChatResponse {
    auto abandoned = std::make_shared<std::atomic<bool>>(false);
    auto done_flag = std::make_shared<std::atomic<bool>>(false);
    auto resp_ptr  = std::make_shared<llm::ChatResponse>();
    auto err_ptr   = std::make_shared<std::exception_ptr>();
    auto cv_mtx    = std::make_shared<std::mutex>();
    auto cv_done   = std::make_shared<std::condition_variable>();

    auto safe_chunk = [abandoned, on_chunk = std::move(on_chunk)](std::string_view chunk) {
        if (abandoned->load(std::memory_order_acquire)) throw CancelledException{};
        if (on_chunk) on_chunk(chunk);
    };

    std::thread worker([done_flag, resp_ptr, err_ptr, cv_mtx, cv_done,
                        provider = Provider(std::move(pcfg)),
                        call_msgs = std::move(msgs), &params,
                        safe_chunk, has_stream_cb]() mutable {
        try {
            *resp_ptr = has_stream_cb
                ? provider.chat_stream(call_msgs, params, safe_chunk)
                : provider.chat(call_msgs, params);
        } catch (...) { *err_ptr = std::current_exception(); }
        done_flag->store(true, std::memory_order_release);
        cv_done->notify_all();
    });

    {
        std::unique_lock lk(*cv_mtx);
        while (!done_flag->load(std::memory_order_acquire)) {
            if (cancel && !cancel->is_active()) {
                abandoned->store(true, std::memory_order_release);
                worker.detach();
                if (cancel->is_paused()) throw PausedException{};
                throw CancelledException{};
            }
            cv_done->wait_for(lk, std::chrono::milliseconds{200});
        }
    }
    worker.join();
    if (*err_ptr) std::rethrow_exception(*err_ptr);
    return std::move(*resp_ptr);
}

export auto do_llm_call(
    const std::vector<llm::Message>& msgs, llm::ChatParams& params,
    const LlmConfig& cfg, std::function<void(std::string_view)> on_chunk,
    CancellationToken* cancel
) -> llm::ChatResponse {
    bool has_stream = static_cast<bool>(on_chunk);
    constexpr int max_retries = 3;
    for (int retry = 0; retry < max_retries; ++retry) {
        try {
            if (cfg.provider == "anthropic") {
                llm::anthropic::Config acfg{.apiKey = cfg.api_key, .model = cfg.model};
                if (!cfg.base_url.empty()) acfg.baseUrl = cfg.base_url;
                return llm_call_worker<llm::anthropic::Anthropic>(
                    std::move(acfg), msgs, params, on_chunk, has_stream, cancel);
            } else {
                llm::openai::Config ocfg{.apiKey = cfg.api_key, .model = cfg.model};
                if (!cfg.base_url.empty()) ocfg.baseUrl = cfg.base_url;
                return llm_call_worker<llm::openai::OpenAI>(
                    std::move(ocfg), msgs, params, on_chunk, has_stream, cancel);
            }
        } catch (const PausedException&) { throw;
        } catch (const CancelledException&) { throw;
        } catch (const std::exception& e) {
            auto kind = classify_error(e);
            if (kind == LlmErrorKind::RateLimit && retry < max_retries - 1) {
                std::this_thread::sleep_for(std::chrono::seconds(1 << (retry + 1)));
                continue;
            }
            if (kind == LlmErrorKind::Timeout && retry < 1) continue;
            throw;
        }
    }
    throw std::runtime_error("LLM call failed after retries");
}

} // namespace xlings::agent
