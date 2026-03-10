export module xlings.agent.approval;

import std;
import xlings.libs.soul;
import xlings.runtime.capability;

namespace xlings::agent {

export enum class ApprovalResult { Approved, Denied, NeedConfirm };

export class ApprovalPolicy {
    const libs::soul::Soul& soul_;

public:
    explicit ApprovalPolicy(const libs::soul::Soul& soul) : soul_(soul) {}

    auto check(const capability::CapabilitySpec& spec, std::string_view params = "") const -> ApprovalResult {
        // Check soul manager rules
        libs::soul::SoulManager* dummy = nullptr; // We just use free functions logic

        // Denied capabilities
        for (auto& d : soul_.denied_capabilities) {
            if (d == spec.name) return ApprovalResult::Denied;
        }

        // Forbidden actions — check params
        if (!params.empty()) {
            for (auto& f : soul_.forbidden_actions) {
                if (std::string_view(params).find(f) != std::string_view::npos) {
                    return ApprovalResult::Denied;
                }
            }
        }

        // Trust level
        if (soul_.trust_level == "auto") {
            return ApprovalResult::Approved;
        }

        if (soul_.trust_level == "readonly") {
            if (spec.destructive) return ApprovalResult::Denied;
            return ApprovalResult::Approved;
        }

        // "confirm" (default)
        if (spec.destructive) return ApprovalResult::NeedConfirm;

        // Check allowed
        bool allowed = false;
        for (auto& a : soul_.allowed_capabilities) {
            if (a == "*" || a == spec.name) { allowed = true; break; }
        }
        return allowed ? ApprovalResult::Approved : ApprovalResult::NeedConfirm;
    }
};

} // namespace xlings::agent
