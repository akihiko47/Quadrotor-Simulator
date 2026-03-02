#pragma once

#include "dynamic_model.cpp"

class TestModel : public DynamicModel {
public:
    TestModel() {
        m_state.emplace_back(1.0f, 0.0f, 0.0f);
    }

    std::vector<vec3> evalF(const std::vector<vec3>& state) const override {
        std::vector<vec3> f;
        f.reserve(state.size());

        for (const auto& v : state) {
            f.emplace_back(-v.y, v.x, 0.0f);
        }

        return f;
    }
};
