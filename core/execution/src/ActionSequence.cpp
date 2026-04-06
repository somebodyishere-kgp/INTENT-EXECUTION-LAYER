#include "ActionSequence.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <optional>
#include <sstream>
#include <thread>

#include "DecisionInterfaces.h"

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

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

bool ParseInt(const std::string& value, int* output) {
    if (value.empty() || output == nullptr) {
        return false;
    }

    int parsed = 0;
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || ptr != value.data() + value.size()) {
        return false;
    }

    *output = parsed;
    return true;
}

bool ParseUnsigned(const std::string& value, std::size_t* output) {
    if (value.empty() || output == nullptr) {
        return false;
    }

    std::size_t parsed = 0;
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || ptr != value.data() + value.size()) {
        return false;
    }

    *output = parsed;
    return true;
}

void ApplyIntentTimingToken(const std::string& token, Intent* intent) {
    if (intent == nullptr) {
        return;
    }

    const std::size_t separator = token.find('=');
    if (separator == std::string::npos || separator == 0 || separator + 1U >= token.size()) {
        return;
    }

    const std::string key = LowerAscii(Trim(token.substr(0, separator)));
    const std::string value = Trim(token.substr(separator + 1U));
    if (value.empty()) {
        return;
    }

    if (key == "delay" || key == "delay_ms") {
        intent->params.values["delay_ms"] = Wide(value);
    } else if (key == "hold" || key == "hold_ms") {
        intent->params.values["hold_ms"] = Wide(value);
    } else if (key == "sequence" || key == "sequence_ms") {
        intent->params.values["sequence_ms"] = Wide(value);
    } else if (key == "timeout" || key == "timeout_ms") {
        int timeoutMs = 0;
        if (ParseInt(value, &timeoutMs) && timeoutMs > 0) {
            intent->constraints.timeoutMs = timeoutMs;
        }
    } else if (key == "retries" || key == "max_retries") {
        int retries = 0;
        if (ParseInt(value, &retries) && retries >= 0) {
            intent->constraints.maxRetries = retries;
        }
    }
}

bool BuildIntentFromActionArgs(
    const Intent& templateIntent,
    const std::vector<std::string>& args,
    Intent* intent,
    std::string* error) {
    if (intent == nullptr || args.empty()) {
        if (error != nullptr) {
            *error = "Action step is empty";
        }
        return false;
    }

    Intent parsed = templateIntent;
    parsed.action = IntentActionFromString(Trim(args[0]));
    if (parsed.action == IntentAction::Unknown) {
        if (error != nullptr) {
            *error = "Unknown action: " + Trim(args[0]);
        }
        return false;
    }

    parsed.name = ToString(parsed.action);
    parsed.source = templateIntent.source.empty() ? "stream" : templateIntent.source;

    std::size_t requiredArgs = 0;
    switch (parsed.action) {
    case IntentAction::Activate:
    case IntentAction::Select: {
        requiredArgs = 2;
        if (args.size() < requiredArgs) {
            if (error != nullptr) {
                *error = "UI action requires target";
            }
            return false;
        }

        parsed.target.type = TargetType::UiElement;
        parsed.target.label = Wide(args[1]);
        break;
    }
    case IntentAction::SetValue: {
        requiredArgs = 3;
        if (args.size() < requiredArgs) {
            if (error != nullptr) {
                *error = "set_value requires target and value";
            }
            return false;
        }

        parsed.target.type = TargetType::UiElement;
        parsed.target.label = Wide(args[1]);
        parsed.params.values["value"] = Wide(args[2]);
        break;
    }
    case IntentAction::Create:
    case IntentAction::Delete: {
        requiredArgs = 2;
        if (args.size() < requiredArgs) {
            if (error != nullptr) {
                *error = "Filesystem action requires path";
            }
            return false;
        }

        parsed.target.type = TargetType::FileSystemPath;
        parsed.target.path = Wide(args[1]);
        parsed.target.label = parsed.target.path;
        parsed.params.values["path"] = parsed.target.path;
        break;
    }
    case IntentAction::Move: {
        requiredArgs = 3;
        if (args.size() < requiredArgs) {
            if (error != nullptr) {
                *error = "move requires path and destination";
            }
            return false;
        }

        parsed.target.type = TargetType::FileSystemPath;
        parsed.target.path = Wide(args[1]);
        parsed.target.label = parsed.target.path;
        parsed.params.values["path"] = parsed.target.path;
        parsed.params.values["destination"] = Wide(args[2]);
        break;
    }
    case IntentAction::Unknown:
    default:
        if (error != nullptr) {
            *error = "Unsupported action";
        }
        return false;
    }

    for (std::size_t i = requiredArgs; i < args.size(); ++i) {
        ApplyIntentTimingToken(args[i], &parsed);
    }

    *intent = std::move(parsed);
    return true;
}

void ApplyStepToken(const std::string& token, ActionStep* step) {
    if (step == nullptr) {
        return;
    }

    const std::size_t separator = token.find('=');
    if (separator == std::string::npos || separator == 0 || separator + 1U >= token.size()) {
        return;
    }

    const std::string key = LowerAscii(Trim(token.substr(0, separator)));
    const std::string value = Trim(token.substr(separator + 1U));

    if (key == "after" || key == "delay_after_ms") {
        int parsed = 0;
        if (ParseInt(value, &parsed) && parsed >= 0) {
            step->delayAfterMs = parsed;
        }
        return;
    }

    if (key == "required") {
        const std::string normalizedValue = LowerAscii(value);
        step->required = !(normalizedValue == "false" || normalizedValue == "0" || normalizedValue == "no");
        return;
    }

    if (key == "repeat") {
        std::size_t parsed = 0;
        if (ParseUnsigned(value, &parsed) && parsed > 0) {
            step->repeatCount = std::min<std::size_t>(parsed, 128U);
        }
    }
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

        const std::string head = LowerAscii(Trim(fields[0]));
        ActionStep step;

        if (head == "if_visible") {
            if (fields.size() < 3U) {
                if (error != nullptr) {
                    *error = "if_visible requires target and then-expression in step " + std::to_string(index + 1U);
                }
                return false;
            }

            step.ifTargetVisible = Wide(Trim(fields[1]));

            Intent thenIntent;
            std::string parseError;
            if (!BuildIntentFromActionArgs(templateIntent, Split(Trim(fields[2]), ':'), &thenIntent, &parseError)) {
                if (error != nullptr) {
                    *error = "Invalid then-expression in step " + std::to_string(index + 1U) + ": " + parseError;
                }
                return false;
            }
            step.intent = std::move(thenIntent);

            if (fields.size() > 3U && !Trim(fields[3]).empty()) {
                Intent elseIntent;
                if (!BuildIntentFromActionArgs(templateIntent, Split(Trim(fields[3]), ':'), &elseIntent, &parseError)) {
                    if (error != nullptr) {
                        *error = "Invalid else-expression in step " + std::to_string(index + 1U) + ": " + parseError;
                    }
                    return false;
                }
                step.elseIntent = std::move(elseIntent);
            }

            for (std::size_t tokenIndex = 4U; tokenIndex < fields.size(); ++tokenIndex) {
                ApplyStepToken(Trim(fields[tokenIndex]), &step);
            }

            step.label = "if_visible";
            sequence->steps.push_back(std::move(step));
            continue;
        }

        if (head == "loop") {
            if (fields.size() < 3U) {
                if (error != nullptr) {
                    *error = "loop requires count and expression in step " + std::to_string(index + 1U);
                }
                return false;
            }

            std::size_t repeat = 0;
            if (!ParseUnsigned(Trim(fields[1]), &repeat) || repeat == 0U) {
                if (error != nullptr) {
                    *error = "Invalid loop count in step " + std::to_string(index + 1U);
                }
                return false;
            }

            Intent loopIntent;
            std::string parseError;
            if (!BuildIntentFromActionArgs(templateIntent, Split(Trim(fields[2]), ':'), &loopIntent, &parseError)) {
                if (error != nullptr) {
                    *error = "Invalid loop expression in step " + std::to_string(index + 1U) + ": " + parseError;
                }
                return false;
            }

            step.intent = std::move(loopIntent);
            step.repeatCount = std::min<std::size_t>(repeat, 128U);
            step.label = "loop";

            for (std::size_t tokenIndex = 3U; tokenIndex < fields.size(); ++tokenIndex) {
                ApplyStepToken(Trim(fields[tokenIndex]), &step);
            }

            sequence->steps.push_back(std::move(step));
            continue;
        }

        Intent stepIntent;
        std::string parseError;
        std::vector<std::string> actionArgs;
        actionArgs.reserve(fields.size());
        for (const std::string& field : fields) {
            actionArgs.push_back(Trim(field));
        }

        if (!BuildIntentFromActionArgs(templateIntent, actionArgs, &stepIntent, &parseError)) {
            if (error != nullptr) {
                *error = "Invalid sequence step " + std::to_string(index + 1U) + ": " + parseError;
            }
            return false;
        }

        step.intent = std::move(stepIntent);
        step.label = ToString(step.intent.action);
        for (const std::string& field : fields) {
            ApplyStepToken(Trim(field), &step);
        }
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

    bool stopRequested = false;

    for (const ActionStep& step : sequence.steps) {
        if (stopRequested) {
            break;
        }

        bool conditionSatisfied = true;
        if (!step.ifTargetVisible.empty()) {
            conditionSatisfied = synchronizedState != nullptr && IsTargetVisible(*synchronizedState, step.ifTargetVisible);
        }

        std::optional<Intent> selectedIntent;
        if (conditionSatisfied) {
            selectedIntent = step.intent;
        } else if (step.elseIntent.has_value()) {
            selectedIntent = step.elseIntent;
        }

        if (!selectedIntent.has_value()) {
            continue;
        }

        const std::size_t repeatCount = std::max<std::size_t>(1U, step.repeatCount);
        for (std::size_t repetition = 0; repetition < repeatCount; ++repetition) {
            Intent intent = *selectedIntent;
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
                stopRequested = true;
                break;
            }

            if (step.delayAfterMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(step.delayAfterMs));
            }
        }
    }

    result.success = (result.attemptedSteps == 0U) || (result.completedSteps == result.attemptedSteps);
    const auto finishedAt = std::chrono::steady_clock::now();
    result.totalDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt - startedAt);

    if (result.message.empty()) {
        result.message = result.success ? "sequence_completed" : "sequence_completed_with_failures";
    }

    return result;
}

}  // namespace iee
