#pragma once

#include <algorithm> 

class PID {
public:

    PID(float kp, float ki, float kd) noexcept
        : m_kp(kp), m_ki(ki), m_kd(kd), m_integral(0.0f), m_prev_error(0.0f) {
    }

    float compute(float current, float target, float dt) noexcept {
        if (dt < 1e-6f) return 0.0f;

        float error = target - current;
        m_integral += error * dt;
        float derivative = (error - m_prev_error) / dt;
        m_prev_error = error;

        return error * m_kp + m_integral * m_ki + derivative * m_kd;
    }

private:
    float m_kp;
    float m_ki;
    float m_kd;

    float m_integral;
    float m_prev_error;
};