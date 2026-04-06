#pragma once

#include <map>
#include <string>
#include <vector>

#include "ObserverEngine.h"

namespace iee {

enum class CapabilityAction {
    Activate,
    SetValue,
    Select,
    Create,
    Delete,
    Move
};

std::string ToString(CapabilityAction action);

struct Capability {
    std::string id;
    CapabilityAction action{CapabilityAction::Activate};
    std::wstring target;
    std::map<std::string, std::wstring> parameters;
    double confidence{0.0};
    std::string source;
};

class ICapabilityExtractor {
public:
    virtual ~ICapabilityExtractor() = default;
    virtual std::vector<Capability> Extract(const ObserverSnapshot& snapshot) const = 0;
};

class CapabilityExtractor : public ICapabilityExtractor {
public:
    std::vector<Capability> Extract(const ObserverSnapshot& snapshot) const override;

private:
    std::vector<Capability> ExtractUiCapabilities(const ObserverSnapshot& snapshot) const;
    std::vector<Capability> ExtractFileCapabilities(const ObserverSnapshot& snapshot) const;
    std::string BuildCapabilityId(const Capability& capability) const;
};

}  // namespace iee
