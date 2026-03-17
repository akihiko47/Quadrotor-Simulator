#pragma once

#include <algorithm>
#include <quadrotor_model.hpp>

void mixer(float thrust, float pidRoll, float pidPitch, float pidYaw, Quadrotor& model) {
	float base = std::clamp(thrust * model.getMaxRotorAngVel(), model.getMinRotorAngVel(), model.getMaxRotorAngVel());
	model.setMotorAngVel(
		base + pidPitch + pidYaw,
		base - pidRoll - pidYaw,
		base - pidPitch + pidYaw,
		base + pidRoll - pidYaw
	);
}