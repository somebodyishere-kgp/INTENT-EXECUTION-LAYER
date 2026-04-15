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

        iee::Intent uiMove;
        uiMove.id = "intent-ui-move";
        uiMove.name = "move";
        uiMove.action = iee::IntentAction::Move;
        uiMove.target.type = iee::TargetType::UiElement;
        uiMove.target.label = L"cursor";
        uiMove.params.values["move_x"] = L"0.60";
        uiMove.params.values["move_y"] = L"-0.25";
        uiMove.params.values["interact"] = L"false";
        uiMove.confidence = 0.80F;
        uiMove.source = "unit-test";
        AssertTrue(validator.Validate(uiMove, &validationError), "UI move intent should validate: " + validationError);

        iee::Intent fsMove;
        fsMove.id = "intent-fs-move";
        fsMove.name = "move";
        fsMove.action = iee::IntentAction::Move;
        fsMove.target.type = iee::TargetType::FileSystemPath;
        fsMove.target.path = L"from.txt";
        fsMove.params.values["path"] = L"from.txt";
        fsMove.params.values["destination"] = L"to.txt";
        fsMove.confidence = 1.0F;
        fsMove.source = "unit-test";
        AssertTrue(validator.Validate(fsMove, &validationError), "Filesystem move intent should validate: " + validationError);

        iee::Intent invalidUiMove;
        invalidUiMove.id = "intent-ui-move-invalid";
        invalidUiMove.name = "move";
        invalidUiMove.action = iee::IntentAction::Move;
        invalidUiMove.target.type = iee::TargetType::UiElement;
        invalidUiMove.target.label = L"cursor";
        invalidUiMove.confidence = 0.80F;
        invalidUiMove.source = "unit-test";
        AssertTrue(!validator.Validate(invalidUiMove, &validationError), "UI move intent without control params should fail validation");

        std::cout << "unit_intent_schema: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_intent_schema: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
