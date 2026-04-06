#include "ActionSequence.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <sstream>
#include <thread>

namespace iee {
namespace {

std::string Trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }

    return value.substr(start, end - start);
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::size_t begin = 0;

    while (begin <= value.size()) {
        const std::size_t separator = value.find(delimiter, begin);
        if (separator == std::string::npos) {
            parts.push_back(value.substr(begin));
            break;
        }

        parts.push_back(value.substr(begin, separator - begin));
        begin = separator + 1U;
    }

    return parts;
}

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return L"";
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), required);
    wide.pop_back();
    return wide;
}

}  // namespace

ActionSequence BuildRepeatedSequence(const Intent& templateIntent, std::size_t repeat, const std::string& sequenceId) {
    ActionSequence sequence;
    sequence.id = sequenceId.empty() ? "sequence-repeat" : sequenceId;

    const std::size_t stepCount = std::max<std::size_t>(1, repeat);
    sequence.steps.reserve(stepCount);

    for (std::size_t index = 0; index < stepCount; ++index) {
        ActionStep step;
        step.intent = templateIntent;
        step.label = templateIntent.name.empty() ? ToString(templateIntent.action) : templateIntent.name;
        sequence.steps.push_back(std::move(step));
    }

    return sequence;
}

bool ParseActionSequenceDsl(
    const std::string& sequenceDsl,
    const Intent& templateIntent,
    ActionSequence* sequence,
    std::string* error) {
    if (sequence == nullptr) {
        if (error != nullptr) {
            *error = "Sequence output is null";
        }
        return false;
    }

    sequence->steps.clear();
    sequence->id = "sequence-dsl";
    sequence->stopOnFailure = true;

    if (Trim(sequenceDsl).empty()) {
        if (error != nullptr) {
            *error = "sequence DSL is empty";
        }
        return false;
    }

    const std::vector<std::string> rawSteps = Split(sequenceDsl, ';');
    for (std::size_t index = 0; index < rawSteps.size(); ++index) {
        const std::string rawStep = Trim(rawSteps[index]);
        if (rawStep.empty()) {
            continue;
        }

        const std::vector<std::string> fields = Split(rawStep, '|');
        if (fields.empty()) {
            continue;
        }

        Intent stepIntent = templateIntent;
        stepIntent.action = IntentActionFromString(Trim(fields[0]));
        if (stepIntent.action == IntentAction::Unknown) {
            if (error != nullptr) {
                *error = "Unknown action in sequence step " + std::to_string(index + 1U);
            }
            return false;
        }

        stepIntent.name = ToString(stepIntent.action);
        stepIntent.source = templateIntent.source.empty() ? "stream" : templateIntent.source;

        const auto field = [&fields](std::size_t at) {
            return at < fields.size() ? Trim(fields[at]) : std::string();
        };

        if (stepIntent.action == IntentAction::Activate || stepIntent.action == IntentAction::Select) {
            const std::string target = field(1);
            if (target.empty()) {
                if (error != nullptr) {
                    *error = "Missing target in sequence step " + std::to_string(index + 1U);
                }
                return false;
            }
            stepIntent.target.type = TargetType::UiElement;
            stepIntent.target.label = Wide(target);
        } else if (stepIntent.action == IntentAction::SetValue) {
            const std::string target = field(1);
            const std::string value = field(2);
            if (target.empty() || value.empty()) {
                if (error != nullptr) {
                    *error = "set_value requires target and value in sequence step " + std::to_string(index + 1U);
                }
                return false;
            }

            stepIntent.target.type = TargetType::UiElement;
            stepIntent.target.label = Wide(target);
            stepIntent.params.values["value"] = Wide(value);
        } else if (stepIntent.action == IntentAction::Create || stepIntent.action == IntentAction::Delete) {
            const std::string path = field(1);
            if (path.empty()) {
                if (error != nullptr) {
                    *error = "Filesystem action requires path in sequence step " + std::to_string(index + 1U);
                }
                return false;
            }

            stepIntent.target.type = TargetType::FileSystemPath;
            stepIntent.target.path = Wide(path);
            stepIntent.target.label = stepIntent.target.path;
            stepIntent.params.values["path"] = stepIntent.target.path;
        } else if (stepIntent.action == IntentAction::Move) {
            const std::string path = field(1);
            const std::string destination = field(2);
            if (path.empty() || destination.empty()) {
                if (error != nullptr) {
                    *error = "move requires path and destination in sequence step " + std::to_string(index + 1U);
                }
                return false;
            }

            stepIntent.target.type = TargetType::FileSystemPath;
            stepIntent.target.path = Wide(path);
            stepIntent.target.label = stepIntent.target.path;
            stepIntent.params.values["path"] = stepIntent.target.path;
            stepIntent.params.values["destination"] = Wide(destination);
        }

        ActionStep step;
        step.intent = std::move(stepIntent);
        step.label = ToString(step.intent.action);
        sequence->steps.push_back(std::move(step));
    }

    if (sequence->steps.empty()) {
        if (error != nullptr) {
            *error = "No valid steps parsed from sequence DSL";
        }
        return false;
    }

    return true;
}

MacroExecutor::MacroExecutor(ExecutionEngine& executionEngine)
    : executionEngine_(executionEngine) {}

ActionSequenceResult MacroExecutor::Execute(const ActionSequence& sequence, const EnvironmentState* synchronizedState) {
    ActionSequenceResult result;
    result.sequenceId = sequence.id;

    if (sequence.steps.empty()) {
        result.message = "Sequence has no steps";
        return result;
    }

    const auto startedAt = std::chrono::steady_clock::now();
    result.stepResults.reserve(sequence.steps.size());

    for (const ActionStep& step : sequence.steps) {
        Intent intent = step.intent;
        if (intent.name.empty()) {
            intent.name = ToString(intent.action);
        }
        if (intent.source.empty()) {
            intent.source = "stream";
        }

        if (synchronizedState != nullptr) {
            intent.context.snapshotVersion = synchronizedState->sourceSnapshotVersion == 0
                ? synchronizedState->sequence
                : synchronizedState->sourceSnapshotVersion;
            intent.context.snapshotTicks = synchronizedState->sequence;
            intent.context.windowTitle = synchronizedState->activeWindowTitle;
            intent.context.application = synchronizedState->activeProcessPath;
            intent.context.cursor = synchronizedState->cursorPosition;
        }

        ++result.attemptedSteps;

        ExecutionResult stepResult = executionEngine_.Execute(intent);
        result.stepResults.push_back(stepResult);

        const bool success = stepResult.status == ExecutionStatus::SUCCESS || stepResult.status == ExecutionStatus::PARTIAL;
        if (success) {
            ++result.completedSteps;
        }

        if (!success && sequence.stopOnFailure && step.required) {
            result.message = stepResult.message;
            break;
        }

        if (step.delayAfterMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(step.delayAfterMs));
        }
    }

    result.success = result.completedSteps == sequence.steps.size();
    const auto finishedAt = std::chrono::steady_clock::now();
    result.totalDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt - startedAt);

    if (result.message.empty()) {
        result.message = result.success ? "sequence_completed" : "sequence_completed_with_failures";
    }

    return result;
}

}  // namespace iee
