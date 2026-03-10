export module xlings.agent.llm_config;

import std;
import xlings.libs.json;

namespace xlings::agent {

export struct LlmConfig {
    std::string provider;       // "anthropic" / "openai"
    std::string model;
    std::string api_key;
    std::string base_url;
    float temperature { 0.3f };
    int max_tokens { 8192 };
};

// API format option (openai-compatible or anthropic-compatible)
export struct ApiFormat {
    std::string name;       // "openai" / "anthropic"
    std::string base_url;   // base URL for this format
};

// Provider presets for first-run setup
export struct ProviderPreset {
    std::string name;           // display name
    std::string id;             // "anthropic" / "openai" / "deepseek" / "minimax"
    std::string default_model;
    std::string env_var;        // env var for API key
    std::vector<ApiFormat> formats;  // available API formats
};

export auto infer_provider(std::string_view model) -> std::string {
    if (model.starts_with("claude"))    return "anthropic";
    if (model.starts_with("gpt"))       return "openai";
    if (model.starts_with("deepseek"))  return "openai";
    if (model.starts_with("MiniMax") || model.starts_with("minimax"))  return "openai";
    return "openai";  // default to OpenAI-compatible protocol
}

export auto default_llm_config() -> LlmConfig {
    return LlmConfig{
        .provider = "anthropic",
        .model = "claude-sonnet-4-6",
        .temperature = 0.3f,
        .max_tokens = 8192,
    };
}

// Load LlmConfig from a JSON object (llm.json default or profile)
auto config_from_json_(const nlohmann::json& j) -> LlmConfig {
    LlmConfig cfg;
    cfg.model = j.value("model", "claude-sonnet-4-6");
    cfg.base_url = j.value("base_url", "");
    cfg.api_key = j.value("api_key", "");
    cfg.temperature = j.value("temperature", 0.3f);
    cfg.max_tokens = j.value("max_tokens", 8192);
    // Use explicit provider if saved, otherwise infer from model name
    cfg.provider = j.value("provider", infer_provider(cfg.model));
    return cfg;
}

// Read llm.json from a path, optionally select a profile
export auto load_llm_json(
    const std::filesystem::path& llm_json_path,
    std::string_view profile = ""
) -> std::optional<LlmConfig> {
    namespace fs = std::filesystem;
    if (!fs::exists(llm_json_path)) return std::nullopt;

    std::ifstream in(llm_json_path);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) return std::nullopt;

    // If profile requested, look in "profiles" object
    if (!profile.empty() && j.contains("profiles") && j["profiles"].is_object()) {
        std::string pkey(profile);
        if (j["profiles"].contains(pkey)) {
            return config_from_json_(j["profiles"][pkey]);
        }
    }

    // Use "default" section or top-level
    if (j.contains("default") && j["default"].is_object()) {
        return config_from_json_(j["default"]);
    }

    // Top-level is a config directly
    if (j.contains("model")) {
        return config_from_json_(j);
    }

    return std::nullopt;
}

// 4-level priority: flags > env > llm.json > default
export auto resolve_llm_config(
    std::string_view flag_model,
    std::string_view flag_base_url,
    const std::filesystem::path& llm_json_path = {},
    std::string_view profile = ""
) -> LlmConfig {
    auto cfg = default_llm_config();

    // L4: llm.json file
    if (!llm_json_path.empty()) {
        if (auto file_cfg = load_llm_json(llm_json_path, profile)) {
            cfg = *file_cfg;
        }
    }

    // L3: env vars
    if (auto* v = std::getenv("XLINGS_LLM_MODEL"))    cfg.model = v;
    if (auto* v = std::getenv("XLINGS_LLM_BASE_URL")) cfg.base_url = v;

    // L2: env var API key (by provider) — overrides file key if present
    // Only infer provider if not explicitly set in config file
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

    // L1: CLI flags (highest priority)
    if (!flag_model.empty()) {
        cfg.model = std::string(flag_model);
        cfg.provider = infer_provider(cfg.model);
    }
    if (!flag_base_url.empty()) cfg.base_url = std::string(flag_base_url);

    return cfg;
}

// ---- First-run setup ----

// Provider presets for setup wizard
// NOTE: Explicit push_back to avoid GCC 15 module aggregate-init issues with nested vectors
export auto provider_presets() -> std::vector<ProviderPreset> {
    std::vector<ProviderPreset> v;

    ProviderPreset anthropic;
    anthropic.name = "Anthropic (Claude)";
    anthropic.id = "anthropic";
    anthropic.default_model = "claude-sonnet-4-6";
    anthropic.env_var = "ANTHROPIC_API_KEY";
    anthropic.formats.push_back({"anthropic", ""});
    v.push_back(std::move(anthropic));

    ProviderPreset openai;
    openai.name = "OpenAI (GPT)";
    openai.id = "openai";
    openai.default_model = "gpt-5.4";
    openai.env_var = "OPENAI_API_KEY";
    openai.formats.push_back({"openai", ""});
    v.push_back(std::move(openai));

    ProviderPreset deepseek;
    deepseek.name = "DeepSeek";
    deepseek.id = "deepseek";
    deepseek.default_model = "deepseek-chat";
    deepseek.env_var = "DEEPSEEK_API_KEY";
    deepseek.formats.push_back({"openai", "https://api.deepseek.com/v1"});
    v.push_back(std::move(deepseek));

    ProviderPreset minimax;
    minimax.name = "MiniMax";
    minimax.id = "minimax";
    minimax.default_model = "MiniMax-M2.5";
    minimax.env_var = "MINIMAX_API_KEY";
    minimax.formats.push_back({"openai", "https://api.minimaxi.com/v1"});
    minimax.formats.push_back({"anthropic", "https://api.minimaxi.com/anthropic/v1"});
    v.push_back(std::move(minimax));

    return v;
}

// Check if first-run setup is needed (no llm.json or no providers configured)
export bool needs_setup(const std::filesystem::path& llm_json_path) {
    namespace fs = std::filesystem;
    if (!fs::exists(llm_json_path)) return true;
    std::ifstream in(llm_json_path);
    if (!in) return true;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) return true;
    if (j.contains("default") && j["default"].is_object()) return false;
    if (j.contains("profiles") && !j["profiles"].empty()) return false;
    if (j.contains("model")) return false;
    return true;
}

// Save setup result to llm.json
export void save_setup_config(
    const std::filesystem::path& llm_json_path,
    const nlohmann::json& profiles,
    std::string_view default_profile
) {
    nlohmann::json llm_json;
    llm_json["default_profile"] = default_profile;
    std::string dp(default_profile);
    if (profiles.contains(dp)) {
        llm_json["default"] = profiles[dp];
    }
    llm_json["profiles"] = profiles;

    namespace fs = std::filesystem;
    fs::create_directories(llm_json_path.parent_path());
    std::ofstream out(llm_json_path);
    out << llm_json.dump(2);
    out.close();
}

} // namespace xlings::agent
