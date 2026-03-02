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

    virtual std::vector<vec3> evalF(const std::vector<vec3>& state) const = 0;

    const std::vector<vec3>& getState() const noexcept {
        return m_state;
    }

    void setState(std::vector<vec3> newState) noexcept {
        m_state = std::move(newState);
    }

protected:
    DynamicModel() = default;

    std::vector<vec3> m_state;
};