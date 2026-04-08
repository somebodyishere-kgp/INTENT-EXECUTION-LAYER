#include "TaskInterface.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

namespace iee {
namespace {

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

bool ContainsToken(const std::vector<std::string>& haystack, const std::string& token) {
    return std::find(haystack.begin(), haystack.end(), token) != haystack.end();
}

double KeywordCoverage(const std::vector<std::string>& haystack, const std::vector<std::string>& needles) {
    if (haystack.empty() || needles.empty()) {
        return 0.0;
    }

    std::size_t matches = 0;
    for (const std::string& token : needles) {
        if (ContainsToken(haystack, token)) {
            ++matches;
        }
    }

    return static_cast<double>(matches) / static_cast<double>(needles.size());
}

double DomainAffinity(TaskDomain domain, const std::vector<std::string>& tokens) {
    static constexpr std::array<const char*, 6> kPresentationKeywords{
        "present", "presentation", "slide", "slideshow", "deck", "export"};
    static constexpr std::array<const char*, 7> kBrowserKeywords{
        "browser", "tab", "address", "search", "url", "navigate", "open"};

    if (domain == TaskDomain::Presentation) {
        std::size_t matches = 0;
        for (const char* keyword : kPresentationKeywords) {
            if (ContainsToken(tokens, keyword)) {
                ++matches;
            }
        }
        return static_cast<double>(matches) / static_cast<double>(kPresentationKeywords.size());
    }

    if (domain == TaskDomain::Browser) {
        std::size_t matches = 0;
        for (const char* keyword : kBrowserKeywords) {
            if (ContainsToken(tokens, keyword)) {
                ++matches;
            }
        }
        return static_cast<double>(matches) / static_cast<double>(kBrowserKeywords.size());
    }

    return 0.0;
}

double WeightedRelevance(
    const std::vector<std::string>& tokens,
    const std::vector<std::string>& goalTokens,
    const std::vector<std::string>& targetTokens,
    TaskDomain domain) {
    double relevance = 0.0;
    relevance += 0.45 * KeywordCoverage(tokens, goalTokens);
    relevance += 0.35 * KeywordCoverage(tokens, targetTokens);
    relevance += 0.20 * DomainAffinity(domain, tokens);
    return std::clamp(relevance, 0.0, 1.0);
}

double EstimateExecutionCost(const InteractionNode& node) {
    const double visibilityPenalty =
        (node.hidden ? 0.18 : 0.0) +
        (node.offscreen ? 0.12 : 0.0) +
        (node.collapsed ? 0.12 : 0.0);
    const double revealPenalty = node.revealStrategy.required ? 0.20 : 0.0;
    const double executablePenalty = node.executionPlan.executable ? 0.0 : 0.45;
    const double stepPenalty = std::min(0.25, static_cast<double>(node.executionPlan.steps.size()) * 0.04);

    return std::clamp(0.10 + visibilityPenalty + revealPenalty + executablePenalty + stepPenalty, 0.0, 1.0);
}

double EstimateSuccessProbability(const InteractionNode& node, double relevance, double executionCost) {
    double success = 0.35;
    success += 0.45 * relevance;
    success += 0.20 * (1.0 - executionCost);

    if (node.executionPlan.executable) {
        success += 0.10;
    }

    if (node.hidden || node.offscreen || node.collapsed) {
        success -= 0.08;
    }

    if (node.revealStrategy.required) {
        success += node.revealStrategy.guaranteed ? 0.03 : -0.05;
    }

    return std::clamp(success, 0.0, 1.0);
}

PlanScore ScoreNode(
    const TaskRequest& request,
    const std::vector<std::string>& goalTokens,
    const std::vector<std::string>& targetTokens,
    const InteractionNode& node) {
    PlanScore planScore;

    if (!request.allowHidden && node.hidden) {
        planScore.executionCost = 1.0;
        planScore.total = -1.0;
        return planScore;
    }

    const std::string corpus = ToAsciiLower(node.label + " " + node.shortcut + " " + node.type);
    const std::vector<std::string> tokens = Tokenize(corpus);

    planScore.relevance = WeightedRelevance(tokens, goalTokens, targetTokens, request.domain);
    planScore.executionCost = EstimateExecutionCost(node);
    planScore.successProbability = EstimateSuccessProbability(node, planScore.relevance, planScore.executionCost);
    planScore.total = std::clamp(
        (0.60 * planScore.relevance) +
            (0.25 * planScore.successProbability) +
            (0.15 * (1.0 - planScore.executionCost)),
        0.0,
        1.0);
    return planScore;
}

std::string SerializePlanScoreJson(const PlanScore& score) {
    std::ostringstream json;
    json << "{";
    json << "\"relevance\":" << score.relevance << ",";
    json << "\"execution_cost\":" << score.executionCost << ",";
    json << "\"success_probability\":" << score.successProbability << ",";
    json << "\"total\":" << score.total;
    json << "}";
    return json.str();
}

std::string SerializePlanDescriptorJson(const TaskPlanCandidate& candidate) {
    std::ostringstream json;
    json << "{";
    json << "\"node_id\":\"" << EscapeJson(candidate.nodeId) << "\",";
    json << "\"label\":\"" << EscapeJson(candidate.label) << "\",";
    json << "\"action\":\"" << EscapeJson(candidate.action) << "\",";
    json << "\"hidden\":" << (candidate.hidden ? "true" : "false") << ",";
    json << "\"requires_reveal\":" << (candidate.requiresReveal ? "true" : "false") << ",";
    json << "\"execution_plan\":" << InteractionGraphBuilder::SerializeExecutionPlanJson(candidate.executionPlan) << ",";
    json << "\"reveal_strategy\":" << InteractionGraphBuilder::SerializeRevealStrategyJson(candidate.revealStrategy);
    json << "}";
    return json.str();
}

std::uint64_t StableHash(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char ch : value) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string Hex(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

}  // namespace

TaskPlanResult TaskPlanner::Plan(const TaskRequest& request, const InteractionGraph& graph) const {
    TaskPlanResult result;
    result.goal = request.goal;

    const std::string normalizedGoal = ToAsciiLower(request.goal);
    const std::string normalizedHint = ToAsciiLower(request.targetHint);
    const std::string identity = normalizedGoal + "|" + normalizedHint + "|" + ToString(request.domain) + "|" +
        std::to_string(graph.version);
    result.taskId = "task-" + Hex(StableHash(identity));

    if (!graph.valid) {
        result.summary = "No valid interaction graph is available";
        return result;
    }

    const std::vector<std::string> goalTokens = Tokenize(normalizedGoal);
    const std::vector<std::string> targetTokens = Tokenize(normalizedHint);

    std::vector<const InteractionNode*> orderedNodes;
    orderedNodes.reserve(graph.nodes.size());
    for (const auto& entry : graph.nodes) {
        orderedNodes.push_back(&entry.second);
    }

    std::sort(orderedNodes.begin(), orderedNodes.end(), [](const InteractionNode* left, const InteractionNode* right) {
        if (left == nullptr || right == nullptr) {
            return left != nullptr;
        }
        return left->id < right->id;
    });

    for (const InteractionNode* node : orderedNodes) {
        if (node == nullptr) {
            continue;
        }

        if (node->intentBinding.action == IntentAction::Unknown) {
            continue;
        }

        const PlanScore planScore = ScoreNode(request, goalTokens, targetTokens, *node);
        if (planScore.total <= 0.0) {
            continue;
        }

        TaskPlanCandidate candidate;
        candidate.nodeId = node->id;
        candidate.label = node->label;
        candidate.action = iee::ToString(node->intentBinding.action);
        candidate.score = planScore.total;
        candidate.planScore = planScore;
        candidate.hidden = node->hidden;
        candidate.requiresReveal = node->revealStrategy.required;
        candidate.executionPlan = node->executionPlan;
        candidate.revealStrategy = node->revealStrategy;
        result.candidates.push_back(std::move(candidate));
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const TaskPlanCandidate& left, const TaskPlanCandidate& right) {
        if (left.planScore.total == right.planScore.total) {
            return left.nodeId < right.nodeId;
        }
        return left.planScore.total > right.planScore.total;
    });

    const std::size_t boundedMaxPlans = std::clamp<std::size_t>(request.maxPlans, 1U, 8U);
    if (result.candidates.size() > boundedMaxPlans) {
        result.candidates.resize(boundedMaxPlans);
    }

    if (result.candidates.empty()) {
        result.summary = "No deterministic candidate matched the requested task";
    } else {
        const TaskPlanCandidate& first = result.candidates.front();
        std::ostringstream summary;
        summary << "Best candidate: " << first.nodeId << " (" << first.action << ", score=" << std::fixed
                << std::setprecision(3) << first.planScore.total << ")";
        result.summary = summary.str();
    }

    return result;
}

TaskDomain TaskPlanner::ParseDomain(const std::string& value) {
    const std::string lower = ToAsciiLower(value);
    if (lower == "presentation") {
        return TaskDomain::Presentation;
    }
    if (lower == "browser") {
        return TaskDomain::Browser;
    }
    return TaskDomain::Generic;
}

std::string TaskPlanner::ToString(TaskDomain domain) {
    switch (domain) {
    case TaskDomain::Presentation:
        return "presentation";
    case TaskDomain::Browser:
        return "browser";
    case TaskDomain::Generic:
    default:
        return "generic";
    }
}

std::string TaskPlanner::SerializeJson(const TaskPlanResult& result) {
    std::ostringstream json;
    json << "{";
    json << "\"task_id\":\"" << EscapeJson(result.taskId) << "\",";
    json << "\"goal\":\"" << EscapeJson(result.goal) << "\",";
    json << "\"summary\":\"" << EscapeJson(result.summary) << "\",";
    json << "\"deterministic\":" << (result.deterministic ? "true" : "false") << ",";
    json << "\"candidate_count\":" << result.candidates.size() << ",";
    json << "\"candidates\":[";

    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const TaskPlanCandidate& candidate = result.candidates[index];
        json << "{";
        json << "\"node_id\":\"" << EscapeJson(candidate.nodeId) << "\",";
        json << "\"label\":\"" << EscapeJson(candidate.label) << "\",";
        json << "\"action\":\"" << EscapeJson(candidate.action) << "\",";
        json << "\"score\":" << candidate.score << ",";
        json << "\"plan_score\":" << SerializePlanScoreJson(candidate.planScore) << ",";
        json << "\"hidden\":" << (candidate.hidden ? "true" : "false") << ",";
        json << "\"requires_reveal\":" << (candidate.requiresReveal ? "true" : "false") << ",";
        json << "\"execution_plan\":" << InteractionGraphBuilder::SerializeExecutionPlanJson(candidate.executionPlan) << ",";
        json << "\"reveal_strategy\":" << InteractionGraphBuilder::SerializeRevealStrategyJson(candidate.revealStrategy);
        json << "}";
    }

    json << "],";
    json << "\"plans\":" << SerializeRankedPlansJson(result);
    json << "}";
    return json.str();
}

std::string TaskPlanner::SerializeRankedPlansJson(const TaskPlanResult& result) {
    std::ostringstream json;
    json << "[";

    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const TaskPlanCandidate& candidate = result.candidates[index];
        json << "{";
        json << "\"plan\":" << SerializePlanDescriptorJson(candidate) << ",";
        json << "\"score\":" << SerializePlanScoreJson(candidate.planScore);
        json << "}";
    }

    json << "]";
    return json.str();
}

}  // namespace iee
