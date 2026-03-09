#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include <vector>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include "dynamic_model.hpp"

struct InputSignal {
    float thrust = 0;
    float yaw = 0;
    float pitch = 0;
    float roll = 0;
};

class Quadrotor : public DynamicModel {
private:
    // Параметры квадрокоптера
    float m = 1.076f;      // общая масса БПЛА
    float mc = 0.692f;     // масса корпуса
    float mr = 0.008f;     // масса одного винта
    float ml = 0.094f;     // масса луча с двигателем, винтом и регулятором
    float r = 0.075f;      // радиус корпуса
    float rr = 0.12f;      // радиус винта
    float l = 0.2f;        // длина луча
    float k = 0.00001f;    // коэффициент аэродинамического сопротивления
    float km = 0.0000001f;  // коэффициент реактивного момента
    float g = 9.81f;       // ускорение свободного падения

    // Угловая скорость винтов
    float m_w1 = 0.0f;
    float m_w2 = 0.0f;
    float m_w3 = 0.0f;
    float m_w4 = 0.0f;

    // Тензор инерции
    float m_Ix, m_Iy, m_Iz;

public:
    Quadrotor() {
        // Инициализируем состояние: 4 вектора по 3 компоненты
        // [0, 1, 2] - позиция (x, y, z)
        // [3, 4, 5, 6] - кватернион вращения (w, x, y, z)
        // [7, 8, 9] - линейная скорость в связанной СК (u, v, w)
        // [10, 11, 12] - угловая скорость (p, q, r)
        m_state.resize(13, 0.0f);
        m_state[3] = 1.0f;  // unit quaternion

        // Вычисляем тензор инерции
        m_Ix = (2.0f / 5.0f) * mc * std::pow(r, 2) + 2 * std::pow(l, 2) * ml;
        m_Iy = (2.0f / 5.0f) * mc * std::pow(r, 2) + 4 * std::pow(l, 2) * ml;
        m_Iz = (2.0f / 5.0f) * mc * std::pow(r, 2) + 2 * std::pow(l, 2) * ml;
    }

    // Установка напряжений на двигателях
    void setInput(InputSignal input) {
        float base = input.thrust * 1000.0f + 20.0f;

        m_w1 = base + input.pitch * 10.0f + input.yaw * 10.0f;
        m_w2 = base - input.roll * 10.0f - input.yaw * 10.0f;
        m_w3 = base - input.pitch * 10.0f + input.yaw * 10.0f;
        m_w4 = base + input.roll * 10.0f - input.yaw * 10.0f;
    }

    // Преобразование из связанной СК в мировую через glm
    glm::vec3 bodyToWorld(const glm::vec3& p) const {
        glm::quat q = glm::quat(m_state[3], m_state[4], m_state[5], m_state[6]);
        return q * p;
    }

    glm::vec3 getPos() {
        return glm::vec3(m_state[0], m_state[1], m_state[2]);
    }

    glm::mat4 getRotatationMatrix() {
        return glm::mat4_cast(glm::quat(m_state[3], m_state[4], m_state[5], m_state[6]));
    }

    // Реализация чисто виртуального метода evalF
    std::vector<float> evalF(const std::vector<float>& state) const override {
        // Текущее состояние
        const glm::vec3 currPos = glm::vec3(state[0], state[1], state[2]);        // позиция
        const glm::quat currQuat = glm::quat(state[3], state[4], state[5], state[6]);  // кватернион
        const glm::vec3 currVel = glm::vec3(state[7], state[8], state[9]);        // линейная скорость
        const glm::vec3 currAngVel = glm::vec3(state[10], state[11], state[12]);  // угловая скорость

        // Расчет сил винтов
        float F1 = k * m_w1 * m_w1;
        float F2 = k * m_w2 * m_w2;
        float F3 = k * m_w3 * m_w3;
        float F4 = k * m_w4 * m_w4;
        float F_total = F1 + F2 + F3 + F4;

        // Сила в связанной СК
        glm::vec3 F_bs(0.0f, F_total, 0.0f);

        // Преобразуем в мировую СК через glm
        glm::vec3 m_Fws = bodyToWorld(F_bs);

        // Реактивные моменты винтов
        float M1 = -km * m_w1 * m_w1;
        float M2 = km * m_w2 * m_w2;
        float M3 = -km * m_w3 * m_w3;
        float M4 = km * m_w4 * m_w4;

        // Моменты управления
        float Mx_pitch_phi = l * (F1 - F3);    // момент тангажа
        float My_yaw_psi = M1 + M2 + M3 + M4;  // момент рыскания
        float Mz_roll_theta = l * (F2 - F4);   // момент крена

        // Производная позиции = скорость
        glm::vec3 posDot = currVel;

        // Производная углов
        glm::quat angVelQuat = glm::quat(0.0f, currAngVel.x, currAngVel.y, currAngVel.z);
        glm::quat quatDot;
        quatDot = 0.5f * currQuat * angVelQuat;

        // Производная линейной скорости
        glm::vec3 velDot;
        velDot.x = m_Fws.x / m;
        velDot.y = (m_Fws.y - g * m) / m;
        velDot.z = m_Fws.z / m;

        // Производная угловой скорости
        glm::vec3 angVelDot;
        angVelDot.x = Mx_pitch_phi / m_Ix;
        angVelDot.y = My_yaw_psi / m_Iy;
        angVelDot.z = Mz_roll_theta / m_Iz;

        // Формируем результат
        std::vector<float> result;
        result.reserve(13);
        result.push_back(posDot.x);
        result.push_back(posDot.y);
        result.push_back(posDot.z);
        result.push_back(quatDot.w);
        result.push_back(quatDot.x);
        result.push_back(quatDot.y);
        result.push_back(quatDot.z);
        result.push_back(velDot.x);
        result.push_back(velDot.y);
        result.push_back(velDot.z);
        result.push_back(angVelDot.x);
        result.push_back(angVelDot.y);
        result.push_back(angVelDot.z);

        return result;
    }
};