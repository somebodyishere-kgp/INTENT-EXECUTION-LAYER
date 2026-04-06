#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "Adapter.h"
#include "Intent.h"
#include "ObserverEngine.h"
#include "TestHelpers.h"

namespace {

class FakeAdapter final : public iee::Adapter {
public:
    FakeAdapter(std::string name, float reliability, float confidence)
        : name_(std::move(name)),
          reliability_(reliability),
          confidence_(confidence) {}

    std::string Name() const override {
        return name_;
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot&, const iee::CapabilityGraph&) override {
        return {};
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.action == iee::IntentAction::Activate;
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        iee::ExecutionResult result;
        result.status = iee::ExecutionStatus::SUCCESS;
        result.verified = true;
        result.method = name_;
        result.message = "ok";
        result.duration = std::chrono::milliseconds(5);
        return result;
    }

    iee::AdapterScore GetScore() const override {
        iee::AdapterScore score;
        score.reliability = reliability_;
        score.latency = 5.0F;
        score.confidence = confidence_;
        return score;
    }

private:
    std::string name_;
    float reliability_{0.5F};
    float confidence_{0.5F};
};

iee::Intent BuildIntent() {
    iee::Intent intent;
    intent.action = iee::IntentAction::Activate;
    intent.name = "activate";
    intent.target.type = iee::TargetType::UiElement;
    intent.target.label = L"Test";
    intent.confidence = 1.0F;
    intent.source = "unit";
    intent.context.snapshotTicks = 1;
    return intent;
}

}  // namespace

int main() {
    try {
        {
            iee::AdapterRegistry registry;
            registry.RegisterAdapter(std::make_shared<FakeAdapter>("alpha", 0.80F, 0.80F));
            registry.RegisterAdapter(std::make_shared<FakeAdapter>("beta", 0.80F, 0.80F));

            const auto best = registry.ResolveBest(BuildIntent());
            AssertTrue(best != nullptr, "Expected adapter resolution");
            AssertTrue(best->Name() == "alpha", "Deterministic tie-break should pick first registration");
        }

        {
            iee::AdapterRegistry registry;
            const auto primary = std::make_shared<FakeAdapter>("primary", 0.90F, 0.90F);
            const auto secondary = std::make_shared<FakeAdapter>("secondary", 0.75F, 0.80F);
            registry.RegisterAdapter(primary);
            registry.RegisterAdapter(secondary);

            const iee::Intent intent = BuildIntent();
            const auto first = registry.ResolveBest(intent);
            AssertTrue(first != nullptr, "Expected initial adapter resolution");
            AssertTrue(first->Name() == "primary", "Primary should win before scoring updates");

            iee::ExecutionResult failed;
            failed.status = iee::ExecutionStatus::FAILED;
            failed.duration = std::chrono::milliseconds(120);

            iee::ExecutionResult success;
            success.status = iee::ExecutionStatus::SUCCESS;
            success.duration = std::chrono::milliseconds(5);

            for (int i = 0; i < 10; ++i) {
                registry.RecordExecution(*primary, failed);
                registry.RecordExecution(*secondary, success);
            }

            const auto next = registry.ResolveBest(intent);
            AssertTrue(next != nullptr, "Expected post-update adapter resolution");
            AssertTrue(next->Name() == "secondary", "Secondary should win after reliability updates");
        }

        std::cout << "unit_adapter_reliability: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_adapter_reliability: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
