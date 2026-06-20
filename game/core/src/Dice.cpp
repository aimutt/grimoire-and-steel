#include "gns/Dice.h"
#include <cctype>
#include <cstdlib>

namespace gns {

bool parseDice(const std::string& text, DiceExpr& out) {
    // Strip spaces.
    std::string s;
    s.reserve(text.size());
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += c;
    if (s.empty()) return false;

    // NdS(+/-M) form.
    std::size_t dpos = s.find_first_of("dD");
    if (dpos != std::string::npos) {
        try {
            int count = std::stoi(s.substr(0, dpos));
            std::size_t i = dpos + 1;
            std::size_t signpos = s.find_first_of("+-", i);
            int sides = std::stoi(s.substr(i, (signpos == std::string::npos ? s.size() : signpos) - i));
            int plus = 0;
            if (signpos != std::string::npos)
                plus = std::stoi(s.substr(signpos));
            if (count <= 0 || sides <= 0) return false;
            out = DiceExpr{};
            out.isRange = false;
            out.count = count;
            out.sides = sides;
            out.plus = plus;
            return true;
        } catch (...) { return false; }
    }

    // MIN-MAX range form (note: leading '-' not expected here).
    std::size_t hyphen = s.find('-', 1);
    if (hyphen != std::string::npos) {
        try {
            int lo = std::stoi(s.substr(0, hyphen));
            int hi = std::stoi(s.substr(hyphen + 1));
            if (hi < lo) return false;
            out = DiceExpr{};
            out.isRange = true;
            out.min = lo;
            out.max = hi;
            return true;
        } catch (...) { return false; }
    }

    // Single integer -> fixed range [n,n].
    try {
        int n = std::stoi(s);
        out = DiceExpr{};
        out.isRange = true;
        out.min = n;
        out.max = n;
        return true;
    } catch (...) { return false; }
}

Dice::Dice() {
    std::random_device rd;
    std::uint64_t s = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    rng_.seed(s);
}
Dice::Dice(std::uint64_t s) { rng_.seed(s); }
void Dice::seed(std::uint64_t s) { rng_.seed(s); }

int Dice::roll(int count, int sides, int plus) {
    if (sides <= 0 || count <= 0) return plus;
    std::uniform_int_distribution<int> d(1, sides);
    int total = plus;
    for (int i = 0; i < count; ++i) total += d(rng_);
    return total;
}

int Dice::rollRange(int lo, int hi) {
    if (hi < lo) std::swap(lo, hi);
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng_);
}

int Dice::rollExpr(const DiceExpr& e) {
    return e.isRange ? rollRange(e.min, e.max) : roll(e.count, e.sides, e.plus);
}

int Dice::rollExpr(const std::string& text) {
    DiceExpr e;
    if (!parseDice(text, e)) return 0;
    return rollExpr(e);
}

int Dice::d6() { return rollRange(1, 6); }
int Dice::d20() { return rollRange(1, 20); }
int Dice::d100() { return rollRange(1, 100); }

bool Dice::percent(int chance) {
    if (chance <= 0) return false;
    if (chance >= 100) return true;
    return d100() <= chance;
}

} // namespace gns
