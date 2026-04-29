#include "judge/output_comparator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace atp {
namespace {

std::string normalize_newlines(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r') {
            if (i + 1 < value.size() && value[i + 1] == '\n') {
                continue;
            }
            normalized.push_back('\n');
        } else {
            normalized.push_back(value[i]);
        }
    }
    return normalized;
}

std::vector<std::string> split_lines(const std::string& value) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream input{value};
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    if (!value.empty() && value.back() == '\n') {
        lines.emplace_back();
    }
    return lines;
}

std::string trim_trailing_normalized(std::string value) {
    value = normalize_newlines(std::move(value));
    auto lines = split_lines(value);
    for (auto& line : lines) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }

    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            result.push_back('\n');
        }
        result += lines[i];
    }
    return result;
}

std::vector<std::string> tokens(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream input{value};
    std::string token;
    while (input >> token) {
        result.push_back(token);
    }
    return result;
}

bool parse_number(const std::string& token, double& value) {
    char* end = nullptr;
    value = std::strtod(token.c_str(), &end);
    return end != token.c_str() && *end == '\0';
}

bool nearly_equal(double expected, double actual, double epsilon) {
    const auto diff = std::fabs(expected - actual);
    if (diff <= epsilon) {
        return true;
    }
    return diff <= epsilon * std::max({1.0, std::fabs(expected), std::fabs(actual)});
}

OutputComparison compare_token_streams(
    const std::string& expected,
    const std::string& actual,
    bool float_mode,
    double epsilon
) {
    const auto expected_tokens = tokens(expected);
    const auto actual_tokens = tokens(actual);
    if (expected_tokens.size() != actual_tokens.size()) {
        return {false, "token count differs"};
    }
    for (std::size_t i = 0; i < expected_tokens.size(); ++i) {
        if (!float_mode) {
            if (expected_tokens[i] != actual_tokens[i]) {
                return {false, "token #" + std::to_string(i + 1) + " differs"};
            }
            continue;
        }

        double lhs = 0.0;
        double rhs = 0.0;
        const bool lhs_number = parse_number(expected_tokens[i], lhs);
        const bool rhs_number = parse_number(actual_tokens[i], rhs);
        if (lhs_number && rhs_number) {
            if (!nearly_equal(lhs, rhs, epsilon)) {
                return {false, "numeric token #" + std::to_string(i + 1) + " differs"};
            }
        } else if (expected_tokens[i] != actual_tokens[i]) {
            return {false, "token #" + std::to_string(i + 1) + " differs"};
        }
    }
    return {true, "accepted"};
}

} // namespace

OutputComparison compareOutputs(
    const std::string& expected,
    const std::string& actual,
    const std::string& mode,
    double epsilon
) {
    if (mode == "exact") {
        return expected == actual ? OutputComparison{true, "accepted"} : OutputComparison{false, "bytes differ"};
    }
    if (mode == "trim_trailing") {
        return trim_trailing_normalized(expected) == trim_trailing_normalized(actual)
            ? OutputComparison{true, "accepted"}
            : OutputComparison{false, "normalized lines differ"};
    }
    if (mode == "ignore_whitespace") {
        return compare_token_streams(expected, actual, false, epsilon);
    }
    if (mode == "float_epsilon") {
        return compare_token_streams(expected, actual, true, epsilon);
    }
    return {false, "unsupported compare mode: " + mode};
}

} // namespace atp
