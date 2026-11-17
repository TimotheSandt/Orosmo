#include "Random.h"

PCGRandom::PCGRandom(uint64_t seed) { this->init(seed); }

void PCGRandom::init(uint64_t seed)
{
    state = 0U;
    this->next();
    state += seed;
    this->next();
}

uint64_t PCGRandom::next() { return PCGRandom::Random(state); }

uint64_t PCGRandom::next(uint64_t min, uint64_t max) { return min + this->next() % (max - min); }

float PCGRandom::nextFloat() { return (float)this->next() / (float)UINT64_MAX; }

float PCGRandom::nextFloat(float min, float max) { return min + this->nextFloat() * (max - min); }

uint64_t PCGRandom::Random(uint64_t &state)
{
    state = state * 747796405u + 2891336453u;
    // The 32-bit PCG hash used a shift of (state >> 28) + 4 (0..19); on a 64-bit state that exponent can exceed 63,
    // which is undefined behaviour (UBSan). Masking to 6 bits is what x86 hardware did already, so every existing
    // sequence is unchanged, and it is now defined (and identical) on all platforms.
    const uint64_t shift = ((state >> 28u) + 4u) & 63u;
    uint64_t result = ((state >> shift) ^ state) * 277803737u;
    result = (result >> 22u) ^ result;
    return result;
}

uint64_t PCGRandom::Random(uint64_t &state, uint64_t min, uint64_t max) { return min + PCGRandom::Random(state) % (max - min); }

float PCGRandom::RandomFloat(uint64_t &state) { return (float)PCGRandom::Random(state) / (float)UINT64_MAX; }

float PCGRandom::RandomFloat(uint64_t &state, float min, float max) { return min + PCGRandom::RandomFloat(state) * (max - min); }
