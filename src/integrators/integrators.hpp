#pragma once

#include "dynamic_model.hpp"

class IIntegrator {
public:
    virtual ~IIntegrator() = default;

    virtual void takeStep(DynamicModel& system, float stepSize) = 0;

    IIntegrator(const IIntegrator&) = delete;
    IIntegrator& operator=(const IIntegrator&) = delete;

protected:
    IIntegrator() = default;
};

class ExplicitEuler : public IIntegrator {
public:
    void takeStep(DynamicModel& model, float stepSize) override {
        const std::vector<vec3>& X = model.getState();
        std::vector<vec3> F = model.evalF(X);

        std::vector<vec3> Xnew;
        Xnew.reserve(X.size());

        for (size_t i = 0; i < X.size(); ++i) {
            Xnew.push_back(X[i] + stepSize * F[i]);
        }

        model.setState(std::move(Xnew));
    }
};