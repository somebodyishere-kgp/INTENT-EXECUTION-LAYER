#include "ActionInterface.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace iee {
namespace {

std::mutex g_actionMemoryMutex;
std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>> g_actionMemory;

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 16U);

    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

std::string ToAsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return L"";
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
    result.pop_back();
    return result;
}

std::string Narrow(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 1) {
        return "";
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredBytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool SkipWhitespace(std::string_view text, std::size_t* index) {
    if (index == nullptr) {
        return false;
    }

    while (*index < text.size()) {
        const char ch = text[*index];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++(*index);
            continue;
        }
        break;
    }

    return *index < text.size();
}

bool ParseJsonString(std::string_view text, std::size_t* index, std::string* output) {
    if (index == nullptr || output == nullptr || *index >= text.size() || text[*index] != '"') {
        return false;
    }

    ++(*index);
    std::string value;

    while (*index < text.size()) {
        const char ch = text[*index];
        ++(*index);

        if (ch == '\\') {
            if (*index >= text.size()) {
                return false;
            }

            const char escaped = text[*index];
            ++(*index);
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                return false;
            }
            continue;
        }

        if (ch == '"') {
            *output = std::move(value);
            return true;
        }

        value.push_back(ch);
    }

    return false;
}

bool ExtractJsonStringField(std::string_view payload, const std::string& key, std::string* output) {
    const std::string marker = "\"" + key + "\"";
    std::size_t keyPos = payload.find(marker);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t index = keyPos + marker.size();
    if (!SkipWhitespace(payload, &index) || payload[index] != ':') {
        return false;
    }

    ++index;
    if (!SkipWhitespace(payload, &index)) {
        return false;
    }

    return ParseJsonString(payload, &index, output);
}

bool ExtractJsonObjectField(std::string_view payload, const std::string& key, std::string* output) {
    const std::string marker = "\"" + key + "\"";
    const std::size_t keyPos = payload.find(marker);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t index = keyPos + marker.size();
    if (!SkipWhitespace(payload, &index) || payload[index] != ':') {
        return false;
    }

    ++index;
    if (!SkipWhitespace(payload, &index) || payload[index] != '{') {
        return false;
    }

    const std::size_t start = index;
    std::size_t depth = 0;
    bool inString = false;

    while (index < payload.size()) {
        const char ch = payload[index];

        if (ch == '"') {
            bool escaped = false;
            std::size_t back = index;
            while (back > start && payload[back - 1] == '\\') {
                escaped = !escaped;
                --back;
            }
            if (!escaped) {
                inString = !inString;
            }
        }

        if (!inString) {
            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                if (depth == 0) {
                    return false;
                }
                --depth;
                if (depth == 0) {
                    *output = std::string(payload.substr(start, index - start + 1U));
                    return true;
                }
            }
        }

        ++index;
    }

    return false;
}

std::vector<std::string> Tokenize(std::string_view value) {
    std::vector<std::string> tokens;
    std::string current;

    for (const char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            continue;
        }

        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

double TokenCoverageScore(const std::vector<std::string>& sourceTokens, const std::vector<std::string>& queryTokens) {
    if (sourceTokens.empty() || queryTokens.empty()) {
        return 0.0;
    }

    std::size_t matches = 0;
    for (const std::string& token : queryTokens) {
        if (std::find(sourceTokens.begin(), sourceTokens.end(), token) != sourceTokens.end()) {
            ++matches;
        }
    }

    return static_cast<double>(matches) / static_cast<double>(queryTokens.size());
}

double EditSimilarity(const std::string& leftValue, const std::string& rightValue) {
    const std::string left = ToAsciiLower(leftValue);
    const std::string right = ToAsciiLower(rightValue);

    if (left.empty() || right.empty()) {
        return 0.0;
    }

    const std::size_t m = left.size();
    const std::size_t n = right.size();
    std::vector<std::size_t> prev(n + 1U);
    std::vector<std::size_t> cur(n + 1U);

    for (std::size_t j = 0; j <= n; ++j) {
        prev[j] = j;
    }

    for (std::size_t i = 1; i <= m; ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= n; ++j) {
            const std::size_t cost = left[i - 1U] == right[j - 1U] ? 0U : 1U;
            cur[j] = std::min({
                prev[j] + 1U,
                cur[j - 1U] + 1U,
                prev[j - 1U] + cost});
        }
        prev.swap(cur);
    }

    const double distance = static_cast<double>(prev[n]);
    const double normalizer = static_cast<double>(std::max(m, n));
    if (normalizer <= 0.0) {
        return 0.0;
    }

    return std::clamp(1.0 - (distance / normalizer), 0.0, 1.0);
}

double LabelSimilarity(const std::string& query, const std::string& label) {
    if (query.empty() || label.empty()) {
        return 0.0;
    }

    const std::string lowerQuery = ToAsciiLower(query);
    const std::string lowerLabel = ToAsciiLower(label);

    if (lowerQuery == lowerLabel) {
        return 1.0;
    }

    if (lowerLabel.find(lowerQuery) != std::string::npos || lowerQuery.find(lowerLabel) != std::string::npos) {
        return 0.92;
    }

    const auto queryTokens = Tokenize(lowerQuery);
    const auto labelTokens = Tokenize(lowerLabel);
    const double tokenScore = TokenCoverageScore(labelTokens, queryTokens);
    const double editScore = EditSimilarity(lowerQuery, lowerLabel);

    return std::clamp((0.55 * tokenScore) + (0.45 * editScore), 0.0, 1.0);
}

double VisibilityWeight(const TaskPlanCandidate& candidate) {
    double weight = candidate.hidden ? 0.60 : 1.0;
    if (candidate.requiresReveal) {
        weight -= 0.10;
    }
    return std::clamp(weight, 0.0, 1.0);
}

double ContextAffinity(
    const ActionContextHints& context,
    const ObserverSnapshot& snapshot,
    const TaskPlanCandidate& candidate) {
    const std::string lowerAppHint = ToAsciiLower(context.app);
    const std::string lowerProcess = ToAsciiLower(Narrow(snapshot.activeProcessPath));
    const std::string lowerTitle = ToAsciiLower(Narrow(snapshot.activeWindowTitle));

    double appScore = 1.0;
    if (!lowerAppHint.empty()) {
        const bool appMatch = lowerProcess.find(lowerAppHint) != std::string::npos ||
            lowerTitle.find(lowerAppHint) != std::string::npos;
        appScore = appMatch ? 1.0 : 0.2;
    }

    const std::string lowerDomain = ToAsciiLower(context.domain);
    double domainScore = 0.6;

    if (lowerDomain == "browser") {
        const std::string corpus = ToAsciiLower(candidate.label + " " + candidate.action);
        domainScore =
            (corpus.find("address") != std::string::npos || corpus.find("url") != std::string::npos ||
             corpus.find("search") != std::string::npos || corpus.find("browser") != std::string::npos)
            ? 1.0
            : 0.35;
    } else if (lowerDomain == "presentation") {
        const std::string corpus = ToAsciiLower(candidate.label + " " + candidate.action);
        domainScore =
            (corpus.find("slide") != std::string::npos || corpus.find("present") != std::string::npos ||
             corpus.find("deck") != std::string::npos || corpus.find("export") != std::string::npos)
            ? 1.0
            : 0.35;
    }

    return std::clamp((0.65 * appScore) + (0.35 * domainScore), 0.0, 1.0);
}

double MemoryScore(const std::string& memoryKey, const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(g_actionMemoryMutex);
    const auto memoryIt = g_actionMemory.find(memoryKey);
    if (memoryIt == g_actionMemory.end() || memoryIt->second.empty()) {
        return 0.0;
    }

    std::size_t total = 0;
    for (const auto& entry : memoryIt->second) {
        total += entry.second;
    }

    if (total == 0U) {
        return 0.0;
    }

    const auto nodeIt = memoryIt->second.find(nodeId);
    if (nodeIt == memoryIt->second.end()) {
        return 0.0;
    }

    return static_cast<double>(nodeIt->second) / static_cast<double>(total);
}

IntentAction ResolveActionForRanking(const std::string& action) {
    const std::string normalized = ToAsciiLower(action);
    if (normalized == "navigate") {
        return IntentAction::SetValue;
    }

    const IntentAction mapped = IntentActionFromString(normalized);
    return mapped == IntentAction::Unknown ? IntentAction::Activate : mapped;
}

struct RankedCandidate {
    TaskPlanCandidate candidate;
    double confidence{0.0};
    double labelScore{0.0};
};

}  // namespace

TargetResolver::TargetResolver(IntentRegistry& registry)
    : registry_(registry) {}

TargetResolution TargetResolver::Resolve(
    const std::string& query,
    const ActionContextHints& context,
    const std::string& action,
    std::size_t maxCandidates) const {
    TargetResolution result;

    const std::size_t boundedCandidates = std::clamp<std::size_t>(maxCandidates, 1U, 8U);
    if (query.empty()) {
        return result;
    }

    registry_.Refresh();
    const ObserverSnapshot snapshot = registry_.LastSnapshot();
    if (!snapshot.valid) {
        return result;
    }

    const InteractionGraph graph = InteractionGraphBuilder::Build(snapshot.uiElements, snapshot.sequence);
    if (!graph.valid) {
        return result;
    }

    TaskRequest taskRequest;
    taskRequest.goal = action + " " + query;
    taskRequest.targetHint = query;
    taskRequest.domain = TaskPlanner::ParseDomain(context.domain);
    taskRequest.allowHidden = true;
    taskRequest.maxPlans = boundedCandidates;

    TaskPlanner planner;
    const TaskPlanResult plan = planner.Plan(taskRequest, graph);
    if (plan.candidates.empty()) {
        return result;
    }

    const ResolutionResult recencyResolution =
        registry_.Resolve(ResolveActionForRanking(action), Wide(query));
    std::unordered_map<std::string, double> recencyByNode;
    recencyByNode.reserve(recencyResolution.ranked.size());
    for (const IntentMatch& match : recencyResolution.ranked) {
        const std::string& nodeId = match.intent.target.nodeId;
        if (nodeId.empty()) {
            continue;
        }

        const double score = std::clamp<double>(
            std::max<double>(match.score, match.recencyScore),
            0.0,
            1.0);
        auto it = recencyByNode.find(nodeId);
        if (it == recencyByNode.end() || score > it->second) {
            recencyByNode[nodeId] = score;
        }
    }

    const std::string memoryKey = BuildMemoryKey(query, context);

    std::vector<RankedCandidate> ranked;
    ranked.reserve(plan.candidates.size());

    for (const TaskPlanCandidate& candidate : plan.candidates) {
        RankedCandidate rankedCandidate;
        rankedCandidate.candidate = candidate;
        rankedCandidate.labelScore = LabelSimilarity(query, candidate.label);

        const double plannerScore = std::clamp<double>(candidate.planScore.total > 0.0 ? candidate.planScore.total : candidate.score, 0.0, 1.0);
        const double visibilityScore = VisibilityWeight(candidate);
        const double contextScore = ContextAffinity(context, snapshot, candidate);
        const double recencyScore = recencyByNode.count(candidate.nodeId) > 0U ? recencyByNode[candidate.nodeId] : 0.0;
        const double memoryScore = MemoryScore(memoryKey, candidate.nodeId);

        rankedCandidate.confidence = std::clamp(
            (0.40 * plannerScore) +
                (0.25 * rankedCandidate.labelScore) +
                (0.15 * visibilityScore) +
                (0.10 * contextScore) +
                (0.10 * std::max(recencyScore, memoryScore)),
            0.0,
            1.0);

        ranked.push_back(std::move(rankedCandidate));
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedCandidate& left, const RankedCandidate& right) {
        if (std::abs(left.confidence - right.confidence) > 0.0001) {
            return left.confidence > right.confidence;
        }
        return left.candidate.nodeId < right.candidate.nodeId;
    });

    if (ranked.size() > boundedCandidates) {
        ranked.resize(boundedCandidates);
    }

    for (std::size_t index = 0; index < ranked.size(); ++index) {
        ActionResolutionCandidate candidate;
        candidate.nodeId = ranked[index].candidate.nodeId;
        candidate.label = ranked[index].candidate.label;
        candidate.confidence = ranked[index].confidence;
        result.alternatives.push_back(std::move(candidate));
    }

    if (ranked.empty()) {
        return result;
    }

    result.matched = true;
    result.nodeId = ranked.front().candidate.nodeId;
    result.confidence = ranked.front().confidence;

    if (ranked.size() > 1U) {
        const double delta = ranked.front().confidence - ranked[1U].confidence;
        const bool nearTie = delta <= 0.03;
        const bool bothStrong = ranked.front().labelScore >= 0.85 && ranked[1U].labelScore >= 0.85;
        if (nearTie && bothStrong) {
            result.ambiguous = true;
        }
    }

    return result;
}

void TargetResolver::RecordSuccessfulResolution(
    const std::string& query,
    const ActionContextHints& context,
    const std::string& nodeId) {
    if (query.empty() || nodeId.empty()) {
        return;
    }

    const std::string key = BuildMemoryKey(query, context);
    std::lock_guard<std::mutex> lock(g_actionMemoryMutex);
    ++g_actionMemory[key][nodeId];
}

std::string TargetResolver::BuildMemoryKey(const std::string& query, const ActionContextHints& context) {
    return ToAsciiLower(query) + "|" + ToAsciiLower(context.domain) + "|" + ToAsciiLower(context.app);
}

ActionExecutor::ActionExecutor(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry)
    : registry_(registry),
      executionEngine_(executionEngine),
      telemetry_(telemetry),
      resolver_(registry) {}

ActionExecutionResult ActionExecutor::Act(const ActionRequest& request) {
    ActionExecutionResult result;
    result.traceId = telemetry_.NewTraceId();

    IntentAction mappedAction = IntentAction::Unknown;
    bool navigateAction = false;
    std::string normalizedAction;
    if (!ParseActionName(request.action, &mappedAction, &navigateAction, &normalizedAction)) {
        result.reason = "unsupported_action";
        return result;
    }

    if (request.target.empty()) {
        result.reason = "missing_target";
        return result;
    }

    std::string resolverQuery = request.target;
    std::string navigationValue = request.value;

    if (navigateAction && navigationValue.empty()) {
        if (request.target.find('.') != std::string::npos || request.target.find('/') != std::string::npos) {
            navigationValue = request.target;
            resolverQuery = "address bar";
        } else {
            navigationValue = request.target;
        }
    }

    TargetResolution resolution = resolver_.Resolve(resolverQuery, request.context, normalizedAction, 8U);
    result.candidates = resolution.alternatives;

    if (!resolution.matched) {
        result.reason = "target_not_found";
        return result;
    }

    if (resolution.ambiguous) {
        result.reason = "ambiguous_target";
        result.resolvedNodeId = resolution.nodeId;
        return result;
    }

    registry_.Refresh();
    const ObserverSnapshot snapshot = registry_.LastSnapshot();
    if (!snapshot.valid) {
        result.reason = "state_unavailable";
        return result;
    }

    const InteractionGraph graph = InteractionGraphBuilder::Build(snapshot.uiElements, snapshot.sequence);
    if (!graph.valid) {
        result.reason = "graph_unavailable";
        return result;
    }

    const auto resolvedNode = InteractionGraphBuilder::FindNode(graph, resolution.nodeId);
    if (!resolvedNode.has_value()) {
        result.reason = "resolved_node_missing";
        return result;
    }

    result.resolvedNodeId = resolvedNode->id;

    TaskRequest taskRequest;
    taskRequest.goal = normalizedAction + " " + resolverQuery;
    taskRequest.targetHint = resolverQuery;
    taskRequest.domain = TaskPlanner::ParseDomain(request.context.domain);
    taskRequest.allowHidden = true;
    taskRequest.maxPlans = 4;

    TaskPlanner planner;
    const TaskPlanResult plan = planner.Plan(taskRequest, graph);
    for (const TaskPlanCandidate& candidate : plan.candidates) {
        if (candidate.nodeId == resolvedNode->id) {
            result.planUsed = candidate.executionPlan;
            result.hasPlan = true;
            break;
        }
    }
    if (!result.hasPlan && !plan.candidates.empty()) {
        result.planUsed = plan.candidates.front().executionPlan;
        result.hasPlan = true;
    }

    std::string valueForAction = request.value;
    if (navigateAction) {
        valueForAction = navigationValue;
    }

    if (mappedAction == IntentAction::SetValue && valueForAction.empty()) {
        result.reason = "missing_value";
        return result;
    }

    const Intent intent = BuildIntentForAction(
        request,
        *resolvedNode,
        snapshot,
        navigateAction,
        valueForAction,
        mappedAction);

    ExecutionContract contract(executionEngine_, registry_);
    const ExecutionContractResult contractResult = contract.Execute(intent, resolvedNode->id);

    if (!contractResult.execution.traceId.empty()) {
        result.traceId = contractResult.execution.traceId;
    }

    result.revealUsed = contractResult.reveal.attempted;
    result.verified = contractResult.execution.verified;
    result.contractSatisfied = contractResult.contractSatisfied;
    result.executionStatus = ToString(contractResult.execution.status);
    result.executionMethod = contractResult.execution.method;
    result.executionMessage = contractResult.execution.message;
    result.usedFallback = contractResult.execution.usedFallback;

    if (contractResult.contractSatisfied) {
        result.status = "success";
        result.reason.clear();
        TargetResolver::RecordSuccessfulResolution(resolverQuery, request.context, resolvedNode->id);
        return result;
    }

    result.status = "failure";
    if (contractResult.stage == "reveal") {
        result.reason = contractResult.message.empty() ? "reveal_failed" : contractResult.message;
    } else if (contractResult.stage == "verify") {
        result.reason = "verification_failed";
    } else if (!contractResult.execution.message.empty()) {
        result.reason = contractResult.execution.message;
    } else {
        result.reason = "execution_failed";
    }

    return result;
}

bool ActionExecutor::ParseActionName(
    const std::string& action,
    IntentAction* mappedAction,
    bool* navigateAction,
    std::string* normalizedAction) {
    if (mappedAction == nullptr || navigateAction == nullptr || normalizedAction == nullptr) {
        return false;
    }

    *navigateAction = false;
    *normalizedAction = NormalizeAction(action);

    if (*normalizedAction == "navigate") {
        *mappedAction = IntentAction::SetValue;
        *navigateAction = true;
        return true;
    }

    *mappedAction = IntentActionFromString(*normalizedAction);
    return *mappedAction != IntentAction::Unknown;
}

std::string ActionExecutor::NormalizeAction(const std::string& action) {
    const std::string normalized = ToAsciiLower(action);
    if (normalized == "open" || normalized == "click" || normalized == "press" || normalized == "tap") {
        return "activate";
    }
    if (normalized == "type" || normalized == "enter") {
        return "set_value";
    }
    if (normalized == "goto" || normalized == "go_to" || normalized == "go") {
        return "navigate";
    }
    return normalized;
}

Intent ActionExecutor::BuildIntentForAction(
    const ActionRequest& request,
    const InteractionNode& resolvedNode,
    const ObserverSnapshot& snapshot,
    bool navigateAction,
    const std::string& navigationValue,
    IntentAction mappedAction) const {
    Intent intent = InteractionGraphBuilder::GenerateIntent(resolvedNode);

    intent.action = mappedAction;
    intent.name = ToString(mappedAction);
    intent.source = "act";
    intent.target.nodeId = resolvedNode.id;

    if (intent.target.label.empty()) {
        intent.target.label = Wide(resolvedNode.label);
    }

    intent.context.windowTitle = snapshot.activeWindowTitle;
    intent.context.cursor = snapshot.cursorPosition;
    intent.context.snapshotVersion = snapshot.sequence;
    intent.context.application = request.context.app.empty() ? snapshot.activeProcessPath : Wide(request.context.app);

    intent.constraints.allowFallback = true;
    intent.constraints.requiresVerification = true;

    if (mappedAction == IntentAction::SetValue) {
        const std::string value = navigateAction ? navigationValue : request.value;
        intent.params.values["value"] = Wide(value);
    }

    return intent;
}

bool ParseActionRequestJson(std::string_view payload, ActionRequest* request, std::string* error) {
    if (request == nullptr) {
        if (error != nullptr) {
            *error = "request_out_null";
        }
        return false;
    }

    ActionRequest parsed;

    if (!ExtractJsonStringField(payload, "action", &parsed.action) || parsed.action.empty()) {
        if (error != nullptr) {
            *error = "missing_action";
        }
        return false;
    }

    if (!ExtractJsonStringField(payload, "target", &parsed.target) || parsed.target.empty()) {
        if (error != nullptr) {
            *error = "missing_target";
        }
        return false;
    }

    std::string value;
    if (ExtractJsonStringField(payload, "value", &value)) {
        parsed.value = value;
    }

    std::string contextObject;
    if (ExtractJsonObjectField(payload, "context", &contextObject)) {
        std::string app;
        std::string domain;

        if (ExtractJsonStringField(contextObject, "app", &app)) {
            parsed.context.app = app;
        }
        if (ExtractJsonStringField(contextObject, "domain", &domain)) {
            parsed.context.domain = domain;
        }
    } else {
        std::string app;
        std::string domain;
        if (ExtractJsonStringField(payload, "app", &app)) {
            parsed.context.app = app;
        }
        if (ExtractJsonStringField(payload, "domain", &domain)) {
            parsed.context.domain = domain;
        }
    }

    *request = std::move(parsed);
    return true;
}

std::string SerializeActionExecutionResultJson(const ActionExecutionResult& result) {
    std::ostringstream json;
    json << "{";
    json << "\"status\":\"" << EscapeJson(result.status) << "\",";
    json << "\"trace_id\":\"" << EscapeJson(result.traceId) << "\",";
    json << "\"resolved_node_id\":\"" << EscapeJson(result.resolvedNodeId) << "\",";

    if (result.hasPlan) {
        json << "\"plan_used\":" << InteractionGraphBuilder::SerializeExecutionPlanJson(result.planUsed) << ",";
    } else {
        json << "\"plan_used\":null,";
    }

    json << "\"reveal_used\":" << (result.revealUsed ? "true" : "false") << ",";
    json << "\"verified\":" << (result.verified ? "true" : "false");

    if (!result.reason.empty()) {
        json << ",\"reason\":\"" << EscapeJson(result.reason) << "\"";
    }

    json << ",\"execution\":{";
    json << "\"status\":\"" << EscapeJson(result.executionStatus) << "\",";
    json << "\"method\":\"" << EscapeJson(result.executionMethod) << "\",";
    json << "\"message\":\"" << EscapeJson(result.executionMessage) << "\",";
    json << "\"contract_satisfied\":" << (result.contractSatisfied ? "true" : "false") << ",";
    json << "\"used_fallback\":" << (result.usedFallback ? "true" : "false");
    json << "}";

    if (!result.candidates.empty()) {
        json << ",\"candidates\":[";
        for (std::size_t index = 0; index < result.candidates.size(); ++index) {
            if (index > 0) {
                json << ",";
            }
            json << "{";
            json << "\"label\":\"" << EscapeJson(result.candidates[index].label) << "\",";
            json << "\"node_id\":\"" << EscapeJson(result.candidates[index].nodeId) << "\",";
            json << "\"confidence\":" << result.candidates[index].confidence;
            json << "}";
        }
        json << "]";
    }

    json << "}";
    return json.str();
}

}  // namespace iee
