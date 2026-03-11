#pragma once

#include <vector>
#include <glm/glm.hpp> 
#include <algorithm>

class Rates final {
public:
    Rates() = delete;
    ~Rates() = delete;

    Rates(const Rates&) = delete;
    Rates& operator=(const Rates&) = delete;
    Rates(Rates&&) = delete;
    Rates& operator=(Rates&&) = delete;

    // Input signal [0, 1] => target angular velocity [-f, f]
    static float evalRate(float x) {
        float sign = (x > 0) ? 1.0f : -1.0f;
        x = std::clamp(std::abs(x), 0.0f, 1.0f);
        float h = x * (std::pow(x, 5.0) * g + x * (1.0f - g));
        return sign * ((d * x) + ((f - d) * h));
    }

private:
    static constexpr float f = 670.0f;
    static constexpr float d = 200.0f;
    static constexpr float g = 0.57f;
};