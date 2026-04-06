#include "CapabilityExtractor.h"

#include <iomanip>
#include <sstream>

#include "Logger.h"

namespace iee {

std::string ToString(CapabilityAction action) {
    switch (action) {
    case CapabilityAction::Activate:
        return "activate";
    case CapabilityAction::SetValue:
        return "set_value";
    case CapabilityAction::Select:
        return "select";
    case CapabilityAction::Create:
        return "create";
    case CapabilityAction::Delete:
        return "delete";
    case CapabilityAction::Move:
        return "move";
    }

    return "activate";
}

std::vector<Capability> CapabilityExtractor::Extract(const ObserverSnapshot& snapshot) const {
    std::vector<Capability> capabilities;

    auto uiCapabilities = ExtractUiCapabilities(snapshot);
    capabilities.insert(capabilities.end(), uiCapabilities.begin(), uiCapabilities.end());

    auto fileCapabilities = ExtractFileCapabilities(snapshot);
    capabilities.insert(capabilities.end(), fileCapabilities.begin(), fileCapabilities.end());

    for (auto& capability : capabilities) {
        capability.id = BuildCapabilityId(capability);
    }

    Logger::Info("CapabilityExtractor", "Extracted capabilities: " + std::to_string(capabilities.size()));
    return capabilities;
}

std::vector<Capability> CapabilityExtractor::ExtractUiCapabilities(const ObserverSnapshot& snapshot) const {
    std::vector<Capability> capabilities;

    for (const auto& element : snapshot.uiElements) {
        std::wstring target = !element.name.empty() ? element.name : element.automationId;
        if (target.empty()) {
            continue;
        }

        if (element.controlType == UiControlType::Button && element.supportsInvoke) {
            Capability capability;
            capability.action = CapabilityAction::Activate;
            capability.target = target;
            capability.confidence = 0.99;
            capability.source = "uia";
            capabilities.push_back(std::move(capability));
        }

        if (element.controlType == UiControlType::TextBox && element.supportsValue) {
            Capability capability;
            capability.action = CapabilityAction::SetValue;
            capability.target = target;
            capability.parameters["value"] = L"";
            capability.confidence = 0.98;
            capability.source = "uia";
            capabilities.push_back(std::move(capability));
        }

        if ((element.controlType == UiControlType::Menu || element.controlType == UiControlType::MenuItem ||
             element.controlType == UiControlType::ComboBox || element.controlType == UiControlType::ListItem) &&
            (element.supportsSelection || element.supportsInvoke)) {
            Capability capability;
            capability.action = CapabilityAction::Select;
            capability.target = target;
            capability.confidence = 0.96;
            capability.source = "uia";
            capabilities.push_back(std::move(capability));
        }
    }

    return capabilities;
}

std::vector<Capability> CapabilityExtractor::ExtractFileCapabilities(const ObserverSnapshot& snapshot) const {
    std::vector<Capability> capabilities;

    Capability createCapability;
    createCapability.action = CapabilityAction::Create;
    createCapability.target = L"filesystem";
    createCapability.parameters["path"] = L"";
    createCapability.confidence = 1.0;
    createCapability.source = "filesystem";
    capabilities.push_back(std::move(createCapability));

    for (const auto& entry : snapshot.fileSystemEntries) {
        if (entry.isDirectory) {
            continue;
        }

        Capability deleteCapability;
        deleteCapability.action = CapabilityAction::Delete;
        deleteCapability.target = entry.path;
        deleteCapability.parameters["path"] = entry.path;
        deleteCapability.confidence = 1.0;
        deleteCapability.source = "filesystem";
        capabilities.push_back(std::move(deleteCapability));

        Capability moveCapability;
        moveCapability.action = CapabilityAction::Move;
        moveCapability.target = entry.path;
        moveCapability.parameters["path"] = entry.path;
        moveCapability.parameters["destination"] = L"";
        moveCapability.parameters["operation"] = L"rename_or_move";
        moveCapability.confidence = 1.0;
        moveCapability.source = "filesystem";
        capabilities.push_back(std::move(moveCapability));
    }

    return capabilities;
}

std::string CapabilityExtractor::BuildCapabilityId(const Capability& capability) const {
    std::hash<std::wstring> wideHasher;
    std::hash<std::string> narrowHasher;

    const std::size_t seed = narrowHasher(ToString(capability.action)) ^
        (wideHasher(capability.target) << 1U) ^
        (narrowHasher(capability.source) << 2U);

    std::ostringstream stream;
    stream << "cap-" << std::hex << seed;
    return stream.str();
}

}  // namespace iee
