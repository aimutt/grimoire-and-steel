#pragma once
#include <cstdint>
#include <random>
#include <string>

namespace gns {

// Parsed dice expression. Either NdS+M (count/sides/plus) or a flat MIN-MAX range.
struct DiceExpr {
    bool isRange = false;   // true => use min/max; false => count d sides + plus
    int count = 0;
    int sides = 0;
    int plus = 0;
    int min = 0;
    int max = 0;
};

// Parse "1d8", "2d6+1", "3d4-1", "2-13", "1-6". Returns false if unrecognized.
bool parseDice(const std::string& text, DiceExpr& out);

// Seedable RNG + rolls. Default seed is random; pass a fixed seed for tests.
class Dice {
public:
    Dice();
    explicit Dice(std::uint64_t seed);

    void seed(std::uint64_t s);

    int roll(int count, int sides, int plus = 0);   // sum of `count` d`sides` + plus
    int rollRange(int lo, int hi);                  // inclusive uniform
    int rollExpr(const DiceExpr& e);
    int rollExpr(const std::string& text);          // parse + roll (0 if unparseable)

    int d6();
    int d20();
    int d100();                                     // 1..100
    bool percent(int chance);                       // true with `chance`% probability

    std::mt19937_64& engine() { return rng_; }

private:
    std::mt19937_64 rng_;
};

} // namespace gns
