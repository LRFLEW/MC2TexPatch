#include "fix_block.hpp"

#include <cstddef>

#include <array>
#include <utility>

#include "fix_dxt.hpp"
#include "mc2_exception.hpp"

template<class T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) { return v < lo ? lo : hi < v ? hi : v; }

struct color {
    union { struct { int r, g, b; }; int v[3]; };
    constexpr int &operator[](std::size_t i) { return v[i]; };
    constexpr const int &operator[](std::size_t i) const { return v[i]; };
    constexpr bool isValid() const { return r >= 0 && r < 0x20 && g >= 0 && g < 0x40 && b >= 0 && b < 0x20; }
    constexpr color clamp() const { return { ::clamp(r, 0, 0x1F), ::clamp(g, 0, 0x3F), ::clamp(b, 0, 0x1F) }; }

    template<class T> static constexpr color mix(color a, color b, T t) {
        return { t(a.r, b.r), t(a.g, b.g), t(a.b, b.b) };
    }
    template<class T> static constexpr color complex(color cs0, color cs1, color x, T t) {
        return { t(cs0.r, cs1.r, x.r), t(cs0.g, cs1.g, x.g), t(cs0.b, cs1.b, x.b) };
    }

    static constexpr color from16(std::uint16_t c) {
        return { (c >> 11) & 0x1F, (c >> 5) & 0x3F, (c >> 0) & 0x1F };
    }
    constexpr std::uint16_t to16() const {
        return static_cast<std::uint16_t>(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | ((b & 0x1F) << 0));
    }
};

constexpr std::pair<color, color> dxt5_chunk::getColors() const { return { color::from16(cs0), color::from16(cs1) }; }
constexpr void dxt5_chunk::setColors(std::pair<color, color> cs) { cs0 = cs.first.to16(), cs1 = cs.second.to16(); }

namespace mixer {
    constexpr color h0(color a1, color a2) { return {6 * a1.r, 6 * a1.g, 6 * a1.b}; }
    constexpr color h1(color a1, color a2) { return {6 * a2.r, 6 * a2.g, 6 * a2.b}; }
    constexpr color h2(color a1, color a2) { return {3 * a1.r + 3 * a2.r, 3 * a1.g + 3 * a2.g, 3 * a1.b + 3 * a2.b}; }
    constexpr color t0(color b1, color b2) { return {6 * b1.r, 6 * b1.g, 6 * b1.b}; }
    constexpr color t1(color b1, color b2) { return {6 * b2.r, 6 * b2.g, 6 * b2.b}; }
    constexpr color t2(color b1, color b2) { return {4 * b1.r + 2 * b2.r, 4 * b1.g + 2 * b2.g, 4 * b1.b + 2 * b2.b}; }
    constexpr color t3(color b1, color b2) { return {2 * b1.r + 4 * b2.r, 2 * b1.g + 4 * b2.g, 2 * b1.b + 4 * b2.b}; }
}

// inputs are in mixer
constexpr std::int_fast32_t single_error2(color x, color y) {
    const std::int_fast32_t r = y.r - x.r, g = y.g - x.g, b = y.b - x.b;
    return 2 * r * r + /* 4 x 1/4 */ g * g + 3 * b * b;
}

constexpr std::int_fast32_t total_error2(const std::array<std::int_fast8_t, 4> w,
                               std::int_fast32_t e0, std::int_fast32_t e1, std::int_fast32_t e2) {
    return w[0] * e0 + w[1] * e1 + w[2] * e2;
}

constexpr std::int_fast32_t total_error2_p2(std::int_fast8_t w2, std::int_fast32_t e0, std::int_fast32_t e2) {
    return (16 - w2) * e0 + w2 * e2;
}

struct pre_eval_p2 {
    const std::int_fast8_t w2;
    const color h0, h2;
    
    constexpr pre_eval_p2(std::int_fast8_t w2, color a1, color a2) :
    w2(w2), h0(mixer::h0(a1, a2)), h2(mixer::h2(a1, a2)) {  }
};

struct pre_eval_p3 {
    const std::array<std::int_fast8_t, 4> w;
    const color h0, h1, h2;
    
    constexpr pre_eval_p3(const std::array<std::int_fast8_t, 4> w, color a1, color a2) :
    w(w), h0(mixer::h0(a1, a2)), h1(mixer::h1(a1, a2)), h2(mixer::h2(a1, a2)) {  }
    constexpr pre_eval_p3 invert() const { return { w, h1, h0, h2 }; }

private:
    constexpr pre_eval_p3(const std::array<std::int_fast8_t, 4> w, color h0, color h1, color h2) :
        w(w), h0(h0), h1(h1), h2(h2) {  }
};

struct eval {
private:
    typedef color (&mixer)(color, color);
    static constexpr std::uint_fast32_t error2(color b1, color b2, pre_eval_p3 pre, mixer x0, mixer x1, mixer x2) {
        return total_error2(pre.w,
                            single_error2(x0(b1, b2), pre.h0),
                            single_error2(x1(b1, b2), pre.h1),
                            single_error2(x2(b1, b2), pre.h2)
                            );
    }
    static constexpr std::uint_fast32_t error2(color b1, color b2, pre_eval_p2 pre, mixer x0, mixer x2) {
        if (!b1.isValid() || !b2.isValid()) return std::numeric_limits<std::int_fast32_t>::max();
        return total_error2_p2(pre.w2,
                               single_error2(x0(b1, b2), pre.h0),
                               single_error2(x2(b1, b2), pre.h2)
                               );
    }
    
public:
    const color b1, b2;
    const std::uint32_t cv;
    const std::int_fast32_t err2;
    
    constexpr eval(std::pair<color, color> b, std::uint32_t cv, pre_eval_p3 pre, mixer x0, mixer x1, mixer x2) :
        b1(b.first), b2(b.second), cv(cv), err2(error2(b1, b2, pre, x0, x1, x2)) {  }
    constexpr eval(color b1, color b2, std::uint32_t cv, pre_eval_p2 pre, mixer x0, mixer x1) :
        b1(b1), b2(b2), cv(cv), err2(error2(b1, b2, pre, x0, x1)) {  }
    
    static constexpr bool compare_err(eval a, eval b) { return a.err2 < b.err2; }
};

constexpr std::array<std::int_fast8_t, 4> invertCs(const std::array<std::int_fast8_t, 4> w) {
    return { w[1], w[0], w[2], w[3] };
}

constexpr int rdiv(int Num, int Den, int DenH) { return (Num + DenH) / Den; }

constexpr int rdiv(int Num, int Den) { return (Num + Den / 2) / Den; }

static std::pair<color, color> iiix(color cs0, color cs1, const std::array<std::int_fast8_t, 4> w) {
    color b = color::mix(cs0, cs1, [](int a, int b) -> int { return b + (b - a) / 2; });
    if (b.isValid()) {
        return { cs0, b };
    }

    b = b.clamp();
    color a = color::complex(cs0, cs1, b, [w](int a1, int a2, int x) -> int {
        return rdiv(3 * (3 * w[0] + w[2])*a1 + 3 * (w[1] + w[2])*a2 - 2 * (w[1] + w[2])*x,
                    9 * w[0] + w[1] + 4 * w[2]);
    });

    return { a, b };
}

class iixi_store {
public:
    const int w0w1, w0w2, w1w2, K, Kh;

    constexpr iixi_store(const std::array<std::int_fast8_t, 4> w) :
        w0w1(w[0] * w[1]), w0w2(w[0] * w[2]), w1w2(w[1] * w[2]),
        K(18 * w0w1 + 2 * w0w2 + 8 * w1w2), Kh(K / 2) { }
    constexpr iixi_store invert() const { return iixi_store(w0w1, w1w2, w0w2); }

private:
    constexpr iixi_store(int w0w1, int w0w2, int w1w2) :
        w0w1(w0w1), w0w2(w0w2), w1w2(w1w2),
        K(18 * w0w1 + 2 * w0w2 + 8 * w1w2), Kh(K / 2) { }
};

static std::pair<color, color> iixi(color cs0, color cs1, const std::array<std::int_fast8_t, 4> w, iixi_store s) {
    color a, b = color::mix(cs0, cs1, [s](int a1, int a2) -> int {
        return rdiv(s.K * a2 + s.w0w2 * (a2 - a1), s.K, s.Kh);
    });
    if (b.isValid()) {
        a = color::mix(cs0, cs1, [s](int a1, int a2) -> int {
            return rdiv(s.K * a1 + 2 * s.w1w2 * (a2 - a1), s.K, s.Kh);
        });
    } else {
        b = b.clamp();
        a = color::complex(cs0, cs1, b, [w](int a1, int a2, int x) -> int {
            return rdiv(9 * w[0] * a1 + 3 * w[2] * (a1 + a2) - 2 * w[2] * x,
                        9 * w[0] + 4 * w[2]);
        });
    }

    return { a, b };
}

static void handle1(color &cs0, color &cs1, std::uint32_t &cv) {
    // Squeeze Method
    color lower = color::mix(cs0, cs1, [](int a, int b) -> int { return (a + b) / 2; });
    color upper = color::mix(cs0, cs1, [](int a, int b) -> int { return (a + b + 1) / 2; });
    cs0 = upper, cs1 = lower;
}

static void handle2(color &cs0, color &cs1, std::uint32_t &cv, std::int_fast8_t w2) {
    // Outer
    // iixx
    color x = color::mix(cs0, cs1, [](int a, int b) -> int { return b + (b - a) / 2; });
    if (x.isValid()) {
        cs1 = x;
        return; // (00b, 10b) to (00b, 10b)
    }
    
    // ixxi, ixix, and xiix (last one Outer) are all co-non-superior.
    // Just test for which one is best in this scenario.
    // (I tried pre-computing this, but to make changes to
    //  single_error2 easier, I didn't keep that code)
    pre_eval_p2 pre(w2, cs0, cs1);
    eval best = std::min({
        eval(cs0, color::mix(cs0, cs1, [](int a, int b) -> int { return (b + a) / 2; }),
             cv >> 1 /* (00b, 10b) to (00b, 01b) */, pre, mixer::t0, mixer::t1), // ixxi
        eval(cs0, color::mix(cs0, cs1, [](int a, int b) -> int { return (3*b + a + 1) / 4; }),
             cv | (cv >> 1) /* (00b, 10b) to (00b, 11b) */, pre, mixer::t0, mixer::t3), // ixix
        eval(color::mix(cs0, cs1, [](int a, int b) -> int { return a - (b - a) / 2; }), cs1,
             (cv >> 1) | 0xAAAAAAAA /* (00b, 10b) to (10b, 11b) */, pre, mixer::t2, mixer::t3), // xiix
    }, eval::compare_err);
    
    cs0 = best.b1, cs1 = best.b2, cv = best.cv;
}

static void handle3(color &cs0, color &cs1, std::uint32_t &cv, const std::array<std::int_fast8_t, 4> w) {
    iixi_store s(w);
    pre_eval_p3 p3(w, cs0, cs1);

    eval best = std::min({
        eval(iiix(cs0, cs1, w), cv | ((cv << 1) & 0xAAAAAAAA) /* (00b, 01b, 10b) to (00b, 11b, 10b) */,
            p3, mixer::t0, mixer::t3, mixer::t2), // iiix
        eval(iiix(cs1, cs0, invertCs(w)), (cv ^ 0x55555555) | ((cv << 1) & 0xAAAAAAAA) /* (00b, 01b, 10b) to (01b, 10b, 11b) */,
            p3.invert(), mixer::t2, mixer::t1, mixer::t3), // xiii
        eval(iixi(cs0, cs1, w, s), cv /* (00b, 01b, 10b) to (00b, 01b, 10b) */,
            p3, mixer::t0, mixer::t1, mixer::t2), // iixi
        eval(iixi(cs1, cs0, invertCs(w), s.invert()), cv ^ 0x55555555 /* (00b, 01b, 10b) to (01b, 00b, 11b) */,
            p3.invert(), mixer::t0, mixer::t1, mixer::t3), // ixii
    }, eval::compare_err);

    cs0 = best.b1, cs1 = best.b2, cv = best.cv;
}

void fix_block(dxt5_chunk &chunk) {
    
    std::array<std::int_fast8_t, 4> dist{};
    for (std::int_fast8_t i=0; i < 16; ++i)
        ++dist[(chunk.cv >> (i * 2)) & 0x3];
    if (dist[3] != 0) throw mc2_exception("Invalid DXT5 color encoding");
    std::int_fast8_t count = (dist[0] != 0) + (dist[1] != 0) + (dist[2] != 0);
    
    std::pair<color, color> cs = chunk.getColors();
    switch(count) {
        case 1:
            handle1(cs.first, cs.second, chunk.cv);
            break;
        case 2:
            // Make sure w1 == 0, and swap if not
            if (dist[0] == 0) {
                std::swap(cs.first, cs.second);
                chunk.cv &= 0xAAAAAAAA; // (01b, 10b) to (00b, 10b)
            }
            handle2(cs.first, cs.second, chunk.cv, dist[2]);
            break;
        case 3:
            handle3(cs.first, cs.second, chunk.cv, dist);
            break;
        default:
            throw mc2_exception("Invalid DXT5 color encoding");
    }
    chunk.setColors(cs);
}
