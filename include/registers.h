#pragma once
#include <stdint.h>
#include <stddef.h>

struct RegisterDefinition {
    uint16_t address;
    uint8_t functionCode;
    float scale;
    const char* name;
    const char* unit;
};
// Intentionally empty: no verified M2430N register mapping available.
// Replace nullptr with an array and update the count after LCD comparison.
constexpr const RegisterDefinition* POWERQUEEN_REGISTERS = nullptr;
constexpr size_t POWERQUEEN_REGISTER_COUNT = 0;

