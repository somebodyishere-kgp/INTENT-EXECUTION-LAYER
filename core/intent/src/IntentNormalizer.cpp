#include "IntentNormalizer.h"

namespace iee {
namespace {

IntentAction ToIntentAction(CapabilityAction action) {
    switch (action) {
    case CapabilityAction::Activate:
        return IntentAction::Activate;
    case CapabilityAction::SetValue:
        return IntentAction::SetValue;
    case CapabilityAction::Select:
        return IntentAction::Select;
    case CapabilityAction::Create:
        return IntentAction::Create;
    case CapabilityAction::Delete:
        return IntentAction::Delete;
    case CapabilityAction::Move:
        return IntentAction::Move;
    }

    return IntentAction::Unknown;
}

}  // namespace

Intent IntentNormalizer::Normalize(const Capability& capability) const {
    Intent intent;
    intent.action = ToIntentAction(capability.action);
    intent.name = ToString(intent.action);
    intent.target.type =
        (intent.action == IntentAction::Create || intent.action == IntentAction::Delete || intent.action == IntentAction::Move)
        ? TargetType::FileSystemPath
        : TargetType::UiElement;

    if (intent.target.type == TargetType::FileSystemPath) {
        intent.target.path = capability.target;
        intent.target.label = capability.target;
    } else {
        intent.target.label = capability.target;
    }

    intent.params.values = capability.parameters;
    intent.confidence = static_cast<float>(capability.confidence);
    intent.source = capability.source;
    return intent;
}

}  // namespace iee
