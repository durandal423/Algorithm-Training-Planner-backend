#pragma once

#include <string>

namespace atp {

struct OutputComparison {
    bool accepted{};
    std::string message;
};

OutputComparison compareOutputs(
    const std::string& expected,
    const std::string& actual,
    const std::string& mode,
    double epsilon
);

} // namespace atp
