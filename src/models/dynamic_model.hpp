#pragma once

#include <vector>
#include <glm/glm.hpp> 

using glm::vec3;

class DynamicModel {
public:
    virtual ~DynamicModel() = default;

    DynamicModel(const DynamicModel&) = delete;
    DynamicModel& operator=(const DynamicModel&) = delete;

    DynamicModel(DynamicModel&&) = default;
    DynamicModel& operator=(DynamicModel&&) = default;

    virtual std::vector<float> evalF(const std::vector<float>& state) = 0;

    const std::vector<float>& getState() const noexcept {
        return m_state;
    }

    void setState(std::vector<float> newState) noexcept {
        m_state = std::move(newState);
    }

protected:
    DynamicModel() = default;

    std::vector<float> m_state;
};