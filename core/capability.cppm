export module xlings.capability;

import std;

import xlings.event;
import xlings.event_stream;

namespace xlings::capability {

export using Params = std::string;
export using Result = std::string;

export struct CapabilitySpec {
    std::string name;
    std::string description;
    std::string inputSchema;      // JSON Schema
    std::string outputSchema;     // JSON Schema
    bool destructive { false };
    bool asyncCapable { true };
};

export struct Capability {
    virtual ~Capability() = default;
    virtual auto spec() const -> CapabilitySpec = 0;
    virtual auto execute(Params params, EventStream& stream) -> Result = 0;
};

export class Registry {
private:
    std::unordered_map<std::string, std::unique_ptr<Capability>> capabilities_;

public:
    void register_capability(std::unique_ptr<Capability> cap) {
        auto name = cap->spec().name;
        capabilities_[std::move(name)] = std::move(cap);
    }

    auto get(std::string_view name) -> Capability* {
        auto it = capabilities_.find(std::string(name));
        return it != capabilities_.end() ? it->second.get() : nullptr;
    }

    auto list_all() -> std::vector<CapabilitySpec> {
        std::vector<CapabilitySpec> specs;
        specs.reserve(capabilities_.size());
        for (auto& [_, cap] : capabilities_) {
            specs.push_back(cap->spec());
        }
        return specs;
    }
};

}  // namespace xlings::capability
