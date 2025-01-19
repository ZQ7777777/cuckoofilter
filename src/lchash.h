#ifndef LCHASH_H
#define LCHASH_H

#include <cstdint>

// Constants
constexpr uint64_t a = 6364136223846793005ULL;
constexpr uint64_t b = 1442695040888963407ULL;

constexpr uint32_t a_32 = 134775813;
constexpr uint32_t b_32 = 1103515245;
// Hash function
constexpr uint64_t LC64(uint64_t x) {
    return a * x + b; // No need for % n since it wraps around naturally
}
constexpr uint32_t LC32(uint32_t x) {
    return a_32 * x + b_32; // No need for % n since it wraps around naturally
}

#endif // LCHASH_H