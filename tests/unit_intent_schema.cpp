#include <iostream>

#include "Intent.h"
#include "TestHelpers.h"

int main() {
    try {
        iee::Intent intent;
        intent.id = "intent-unit";
        intent.name = "set_value";
        intent.action = iee::IntentAction::SetValue;
        intent.target.type = iee::TargetType::UiElement;
        intent.target.label = L"Title";
        intent.params.values["value"] = L"Hello";
        intent.confidence = 0.95F;
        intent.source = "unit-test";

        iee::IntentValidator validator;
        std::string validationError;
        AssertTrue(validator.Validate(intent, &validationError), "Intent should validate: " + validationError);

        const std::string payload = intent.Serialize();
        const auto decoded = iee::Intent::Deserialize(payload);
        AssertTrue(decoded.has_value(), "Deserialization should succeed");
        AssertTrue(decoded->action == iee::IntentAction::SetValue, "Decoded action mismatch");
        AssertTrue(decoded->params.Get("value") == L"Hello", "Decoded value mismatch");

        std::cout << "unit_intent_schema: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_intent_schema: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
