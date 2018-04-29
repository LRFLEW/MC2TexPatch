#include "fix_dxt.hpp"

#include "fix_block.hpp"
#include "mc2_exception.hpp"

template<class T> static void helper_read(const std::vector<char> &texture, T &t) {
    if (texture.size() < sizeof(T)) throw mc2_exception("Texture file not large enough");
    t = *reinterpret_cast<const T*>(texture.data());
}

template<class T> static void helper_read(const std::vector<char> &texture, size_t &read_offset, T &t) {
    if (texture.size() < read_offset + sizeof(T)) throw mc2_exception("Texture file not large enough");
    t = *reinterpret_cast<const T*>(texture.data() + read_offset);
    read_offset += sizeof(T);
}

template<class T> static void helper_override(std::vector<char> &texture, size_t &read_offset, const T &t) {
    if (read_offset < sizeof(T)) throw mc2_exception("Overwritten past beginning of texture");
    *reinterpret_cast<T *>(texture.data() + read_offset - sizeof(T)) = t;
}

static bool clean(dxt5_chunk &chunk) {
    if (chunk.cs0 < chunk.cs1) {
        // Reframe for less ambiguity
        std::swap(chunk.cs0, chunk.cs1);
        chunk.cv ^= 0x55555555;
        return true;
    } else if (chunk.cs0 == chunk.cs1) {
        // Reframe for less ambiguity
        if (chunk.cs0 == 0) {
            chunk.cs0 = 1;
            chunk.cv = 0x55555555;
        } else {
            chunk.cs1 = 0;
            chunk.cv = 0x00000000;
        }
        return true;
    }
    return false;
}

bool needs_fixing(const std::vector<char> &texture) {
    tex_header header;
    helper_read(texture, header);

    if (header.type == 26) return true;
    else if (header.type == 22) {
        if (header.width == 0 || header.height == 0) return false;

        std::uint16_t width = header.width, height = header.height, mmaps;
        for (mmaps = 0; mmaps < header.mmaps; ++mmaps) {
            if (width & 3 || height & 3) {
                if (width >= 4 || height >= 4) throw mc2_exception("Unexpected Non-Power-of-Two Texture");
                return true;
            }
            width /= 2, height /= 2;
        }
    }

    return false;
}

bool fix_dxt(std::vector<char> &texture) {
    size_t read_offset = 0;
    bool dirty = false;
    tex_header header;
    helper_read(texture, read_offset, header);
    
    if (header.type != 26 && header.type != 22) return false;
    if (header.width == 0 || header.height == 0) return false;

    size_t bytes = sizeof(tex_header), bdiv = header.type == 26 ? 1 : 2;
    std::uint16_t width = header.width, height = header.height, mmaps;
    for (mmaps = 0; mmaps < header.mmaps; ++mmaps) {
        if (width & 3 || height & 3) {
            if (width >= 4 || height >= 4) throw mc2_exception("Unexpected Non-Power-of-Two Texture");
            break;
        }

        bytes += width * height / bdiv;
        width /= 2, height /= 2;
    }
    if (mmaps == 0) throw mc2_exception("Texture contains no valid MipMap Levels");

    if (header.mmaps == mmaps) {
        if (texture.size() != bytes) throw mc2_exception("Texture file an invalid size");
    } else {
        if (texture.size() < bytes) throw mc2_exception("Texture file is not as large as expected");
        header.mmaps = mmaps;
        helper_override(texture, read_offset, header);
        texture.resize(bytes);
        dirty = true;
    }
    
    if (header.type == 26) {
        size_t blocks = (header.width / 4) * (header.height / 4);
        for (std::uint16_t mmap = 0; mmap < header.mmaps; ++mmap) {
            for (size_t i = 0; i < blocks; ++i) {
                dxt5_chunk chunk;
                helper_read(texture, read_offset, chunk);

                if (chunk.cs0 < chunk.cs1) {
                    if ((chunk.cv & 0xAAAAAAAA) == 0) {
                        // Reframe for less ambiguity
                        std::swap(chunk.cs0, chunk.cs1);
                        chunk.cv ^= 0x55555555;
                    } else {
                        fix_block(chunk);
                        clean(chunk);
                    }
                    helper_override(texture, read_offset, chunk);
                    dirty = true;
                } else if (chunk.cs0 == chunk.cs1) {
                    // Reframe for less ambiguity
                    if (chunk.cs0 == 0) {
                        chunk.cs0 = 1;
                        chunk.cv = 0x55555555;
                    } else {
                        chunk.cs1 = 0;
                        chunk.cv = 0x00000000;
                    }
                    helper_override(texture, read_offset, chunk);
                    dirty = true;
                }
            }
            blocks /= 4;
        }
        if (read_offset != bytes) throw mc2_exception("Texture file not large enough");
    }

    return dirty;
}
