#pragma once
#include <random>
#include <cstdint>

namespace sim {

class Rng {
public:
  explicit Rng(uint64_t seed = 1) : m_gen(seed) {}

  void Seed(uint64_t s) { m_gen.seed(s); }

  double U01() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(m_gen);
  }

  int UniformInt(int a, int b) {
    return std::uniform_int_distribution<int>(a, b)(m_gen);
  }

  bool Bernoulli(double p) {
    if (p <= 0.0) return false;
    if (p >= 1.0) return true;
    return std::bernoulli_distribution(p)(m_gen);
  }

  double Exp(double lambda) {
    return std::exponential_distribution<double>(lambda)(m_gen);
  }

private:
  std::mt19937_64 m_gen;
};

}
