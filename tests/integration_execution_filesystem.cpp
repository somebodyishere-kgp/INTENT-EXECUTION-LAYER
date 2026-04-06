#include <filesystem>
#include <iostream>
#include <memory>

#include "Adapter.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
#include "Intent.h"
#include "Telemetry.h"
#include "TestHelpers.h"

int main() {
    try {
        iee::EventBus eventBus;
        iee::AdapterRegistry adapters;
        adapters.Register(std::make_unique<iee::FileSystemAdapter>());

        iee::IntentValidator validator;
        iee::Telemetry telemetry;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        const std::filesystem::path sourcePath = "integration_file.txt";
        const std::filesystem::path movedPath = std::filesystem::path("docs") / "integration_file.txt";

        iee::Intent create;
        create.id = "create-int";
        create.name = "create";
        create.action = iee::IntentAction::Create;
        create.target.type = iee::TargetType::FileSystemPath;
        create.target.path = sourcePath.wstring();
        create.target.label = sourcePath.wstring();
        create.params.values["path"] = sourcePath.wstring();
        create.source = "integration-test";
        create.confidence = 1.0F;

        iee::Intent move;
        move.id = "move-int";
        move.name = "move";
        move.action = iee::IntentAction::Move;
        move.target.type = iee::TargetType::FileSystemPath;
        move.target.path = sourcePath.wstring();
        move.target.label = sourcePath.wstring();
        move.params.values["path"] = sourcePath.wstring();
        move.params.values["destination"] = movedPath.wstring();
        move.source = "integration-test";
        move.confidence = 1.0F;

        iee::Intent remove;
        remove.id = "delete-int";
        remove.name = "delete";
        remove.action = iee::IntentAction::Delete;
        remove.target.type = iee::TargetType::FileSystemPath;
        remove.target.path = movedPath.wstring();
        remove.target.label = movedPath.wstring();
        remove.params.values["path"] = movedPath.wstring();
        remove.source = "integration-test";
        remove.confidence = 1.0F;

        const auto createResult = engine.Execute(create);
        AssertTrue(createResult.status == iee::ExecutionStatus::SUCCESS, "Create should succeed");

        const auto moveResult = engine.Execute(move);
        AssertTrue(moveResult.status == iee::ExecutionStatus::SUCCESS, "Move should succeed");

        const auto removeResult = engine.Execute(remove);
        AssertTrue(removeResult.status == iee::ExecutionStatus::SUCCESS, "Delete should succeed");

        if (std::filesystem::exists(sourcePath)) {
            std::filesystem::remove(sourcePath);
        }
        if (std::filesystem::exists(movedPath)) {
            std::filesystem::remove(movedPath);
        }

        std::cout << "integration_execution_filesystem: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integration_execution_filesystem: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
