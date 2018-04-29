#pragma once

#include <cstddef>
#include <cstdint>

#include <utility>
#include <vector>

struct color;

struct tex_header {
    std::uint16_t width, height, type, mmaps;
    std::uint16_t u1, u2, u3; // Unknown parameters
};

struct dxt5_chunk {
    std::uint8_t as[2]; std::uint8_t ax[6];
    std::uint16_t cs0, cs1; std::uint32_t cv;

    constexpr std::pair<color, color> getColors() const;
    constexpr void setColors(std::pair<color, color> cs);
};

constexpr size_t FixingSize = sizeof(tex_header);
bool needs_fixing(const std::vector<char> &texture);
bool fix_dxt(std::vector<char> &texture);
