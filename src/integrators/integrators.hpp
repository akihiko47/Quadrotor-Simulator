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
        const std::vector<float>& X = model.getState();
        std::vector<float> F = model.evalF(X);

        std::vector<float> Xnew;
        Xnew.reserve(X.size());

        for (size_t i = 0; i < X.size(); ++i) {
            Xnew.push_back(X[i] + stepSize * F[i]);
        }

        model.setState(std::move(Xnew));
    }
};

class Trapezoidal : public IIntegrator {
public:
    void takeStep(DynamicModel& model, float stepSize) override {
        const std::vector<float>& X = model.getState();

        // k1 = f(X)
        std::vector<float> k1 = model.evalF(X);

        // X1 = X + stepSize * k1
        std::vector<float> X1;
        X1.reserve(X.size());
        for (size_t i = 0; i < X.size(); ++i) {
            X1.push_back(X[i] + stepSize * k1[i]);
        }

        // k2 = f(X1)
        std::vector<float> k2 = model.evalF(X1);

        // Xnew = X + (stepSize/2) * (k1 + k2)
        std::vector<float> Xnew;
        Xnew.reserve(X.size());
        for (size_t i = 0; i < X.size(); ++i) {
            Xnew.push_back(X[i] + (stepSize / 2.0f) * (k1[i] + k2[i]));
        }

        model.setState(std::move(Xnew));
    }
};

class RungeKutta4 : public IIntegrator {
public:
    void takeStep(DynamicModel& model, float stepSize) override {
        const std::vector<float>& X = model.getState();
        size_t n = X.size();

        std::vector<float> k1 = model.evalF(X);

        std::vector<float> Xtemp(n);
        std::vector<float> k2(n), k3(n), k4(n);

        // k2
        for (size_t i = 0; i < n; ++i) {
            Xtemp[i] = X[i] + 0.5f * stepSize * k1[i];
        }
        k2 = model.evalF(Xtemp);

        // k3
        for (size_t i = 0; i < n; ++i) {
            Xtemp[i] = X[i] + 0.5f * stepSize * k2[i];
        }
        k3 = model.evalF(Xtemp);

        // k4
        for (size_t i = 0; i < n; ++i) {
            Xtemp[i] = X[i] + stepSize * k3[i];
        }
        k4 = model.evalF(Xtemp);

        std::vector<float> Xnew(n);
        for (size_t i = 0; i < n; ++i) {
            Xnew[i] = X[i] + (stepSize / 6.0f) *
                (k1[i] + 2.0f * k2[i] + 2.0f * k3[i] + k4[i]);
        }

        model.setState(std::move(Xnew));
    }
};