#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kTubeCount = 12;
constexpr int kCapacity = 4;
constexpr int kColorCount = 10;
constexpr int kMaxMoves = kTubeCount * (kTubeCount - 1);

using Color = std::uint8_t;
using Tube = std::uint16_t;
using GameState = std::array<Tube, kTubeCount>;

static_assert(sizeof(GameState) == 24);

struct Move {
    std::uint8_t from = 0;
    std::uint8_t to = 0;
};

struct TubeInfo {
    bool empty = true;
    bool full = false;
    bool mono = true;
    bool complete = false;
    Color bottom = 0;
    std::uint8_t len = 0;
    std::uint8_t run = 0;
    std::uint16_t color_mask = 0;
};

struct SearchResult {
    bool found = false;
    int next_bound = std::numeric_limits<int>::max();
};

struct Key {
    GameState tubes{};

    bool operator==(const Key& other) const {
        return tubes == other.tubes;
    }
};

struct KeyHash {
    std::size_t operator()(const Key& key) const {
        std::uint64_t h = 1469598103934665603ULL;
        for (Tube tube : key.tubes) {
            h ^= static_cast<std::uint64_t>(tube & 0xffU);
            h *= 1099511628211ULL;
            h ^= static_cast<std::uint64_t>(tube >> 8U);
            h *= 1099511628211ULL;
        }
        return static_cast<std::size_t>(h);
    }
};

struct Candidate {
    Move move{};
    int h = 0;
    std::uint8_t amount = 0;
    bool to_empty = false;
};

struct CandidateList {
    std::array<Candidate, kMaxMoves> items{};
    int count = 0;
};

Color slot(Tube tube, int pos) {
    return static_cast<Color>((tube >> (pos * 4)) & 0x0fU);
}

void set_slot(Tube& tube, int pos, Color color) {
    Tube mask = static_cast<Tube>(0x0fU << (pos * 4));
    tube = static_cast<Tube>((tube & ~mask) | (static_cast<Tube>(color) << (pos * 4)));
}

Tube make_tube(const std::array<Color, kCapacity>& colors, int len) {
    Tube tube = 0;
    for (int i = 0; i < len; ++i) {
        set_slot(tube, i, colors[static_cast<std::size_t>(i)]);
    }
    return tube;
}

TubeInfo inspect_tube(Tube tube) {
    TubeInfo info;
    Color first = 0;

    for (int i = 0; i < kCapacity; ++i) {
        Color color = slot(tube, i);
        if (color == 0) break;
        if (info.len == 0) first = color;
        if (color != first) info.mono = false;
        info.color_mask = static_cast<std::uint16_t>(info.color_mask | (1U << color));
        ++info.len;
    }

    info.empty = info.len == 0;
    info.full = info.len == kCapacity;
    if (info.empty) return info;

    info.bottom = slot(tube, info.len - 1);
    for (int i = info.len - 1; i >= 0 && slot(tube, i) == info.bottom; --i) {
        ++info.run;
    }
    info.complete = info.full && info.mono;
    return info;
}

std::array<TubeInfo, kTubeCount> inspect_state(const GameState& state) {
    std::array<TubeInfo, kTubeCount> info{};
    for (int t = 0; t < kTubeCount; ++t) {
        info[static_cast<std::size_t>(t)] = inspect_tube(state[static_cast<std::size_t>(t)]);
    }
    return info;
}

bool is_goal(const GameState& state) {
    std::uint16_t seen = 0;
    for (Tube tube : state) {
        TubeInfo info = inspect_tube(tube);
        if (info.empty) continue;
        if (!info.mono) return false;
        std::uint16_t bit = static_cast<std::uint16_t>(1U << info.bottom);
        if ((seen & bit) != 0) return false;
        seen = static_cast<std::uint16_t>(seen | bit);
    }
    return true;
}

int heuristic(const GameState& state) {
    int non_mono = 0;
    std::array<std::uint8_t, kColorCount + 1> tubes_containing_color{};

    for (Tube tube : state) {
        TubeInfo info = inspect_tube(tube);
        if (!info.mono) ++non_mono;

        std::uint16_t mask = info.color_mask;
        while (mask != 0) {
            int color = __builtin_ctz(mask);
            ++tubes_containing_color[static_cast<std::size_t>(color)];
            mask = static_cast<std::uint16_t>(mask & (mask - 1));
        }
    }

    int split_color_lower_bound = 0;
    for (int color = 1; color <= kColorCount; ++color) {
        int count = tubes_containing_color[static_cast<std::size_t>(color)];
        if (count > 1) split_color_lower_bound += count - 1;
    }
    return std::max(non_mono, split_color_lower_bound);
}

Key encode_state(const GameState& state) {
    Key key{state};
    std::sort(key.tubes.begin(), key.tubes.end());
    return key;
}

bool can_move(const TubeInfo& src, const TubeInfo& dst) {
    if (src.empty || dst.full) return false;
    return dst.empty || dst.bottom == src.bottom;
}

void apply_move(GameState& state, const std::array<TubeInfo, kTubeCount>& info, int from, int to, int amount) {
    Tube& src = state[static_cast<std::size_t>(from)];
    Tube& dst = state[static_cast<std::size_t>(to)];
    int from_len = info[static_cast<std::size_t>(from)].len;
    int to_len = info[static_cast<std::size_t>(to)].len;

    for (int i = 0; i < amount; ++i) {
        set_slot(dst, to_len + i, slot(src, from_len - 1 - i));
        set_slot(src, from_len - 1 - i, 0);
    }
}

void apply_move(GameState& state, int from, int to, int amount) {
    TubeInfo src_info = inspect_tube(state[static_cast<std::size_t>(from)]);
    TubeInfo dst_info = inspect_tube(state[static_cast<std::size_t>(to)]);
    Tube& src = state[static_cast<std::size_t>(from)];
    Tube& dst = state[static_cast<std::size_t>(to)];

    for (int i = 0; i < amount; ++i) {
        set_slot(dst, dst_info.len + i, slot(src, src_info.len - 1 - i));
        set_slot(src, src_info.len - 1 - i, 0);
    }
}

bool contains_generated(const std::array<Key, kMaxMoves>& generated, int count, const Key& key) {
    for (int i = 0; i < count; ++i) {
        if (generated[static_cast<std::size_t>(i)] == key) return true;
    }
    return false;
}

CandidateList next_moves(const GameState& state, const std::optional<Key>& previous_key) {
    CandidateList result;
    std::array<Key, kMaxMoves> generated{};
    int generated_count = 0;
    std::array<TubeInfo, kTubeCount> info = inspect_state(state);

    for (int s = 0; s < kTubeCount; ++s) {
        const TubeInfo& src = info[static_cast<std::size_t>(s)];
        if (src.empty || src.complete) continue;

        for (int d = 0; d < kTubeCount; ++d) {
            if (s == d) continue;
            const TubeInfo& dst = info[static_cast<std::size_t>(d)];
            if (!can_move(src, dst)) continue;

            int space = kCapacity - dst.len;
            int amount = std::min<int>(src.run, space);
            if (amount <= 0) continue;

            bool whole_source = amount == src.len;
            if (dst.empty && src.mono && whole_source) continue;

            GameState child = state;
            apply_move(child, info, s, d, amount);

            Key key = encode_state(child);
            if (previous_key && key == *previous_key) continue;
            if (contains_generated(generated, generated_count, key)) continue;
            generated[static_cast<std::size_t>(generated_count++)] = key;

            result.items[static_cast<std::size_t>(result.count++)] = Candidate{
                Move{static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(d)},
                heuristic(child),
                static_cast<std::uint8_t>(amount),
                dst.empty
            };
        }
    }

    std::sort(result.items.begin(), result.items.begin() + result.count, [](const Candidate& a, const Candidate& b) {
        if (a.h != b.h) return a.h < b.h;
        if (a.to_empty != b.to_empty) return !a.to_empty && b.to_empty;
        return a.amount > b.amount;
    });

    return result;
}

SearchResult dfs_ida(
    GameState& state,
    int g,
    int bound,
    const std::optional<Key>& previous_key,
    std::unordered_map<Key, std::uint8_t, KeyHash>& best_depth,
    std::vector<Move>& path
) {
    int h = heuristic(state);
    int f = g + h;
    if (f > bound) return SearchResult{false, f};
    if (is_goal(state)) return SearchResult{true, bound};

    Key key = encode_state(state);
    auto it = best_depth.find(key);
    if (it != best_depth.end() && it->second <= g) {
        return SearchResult{false, std::numeric_limits<int>::max()};
    }
    best_depth[key] = static_cast<std::uint8_t>(g);

    int min_exceeded = std::numeric_limits<int>::max();
    CandidateList candidates = next_moves(state, previous_key);
    for (int i = 0; i < candidates.count; ++i) {
        const Candidate& candidate = candidates.items[static_cast<std::size_t>(i)];
        Tube old_from = state[static_cast<std::size_t>(candidate.move.from)];
        Tube old_to = state[static_cast<std::size_t>(candidate.move.to)];
        apply_move(state, candidate.move.from, candidate.move.to, candidate.amount);

        path.push_back(candidate.move);
        SearchResult r = dfs_ida(state, g + 1, bound, key, best_depth, path);
        if (r.found) return r;
        path.pop_back();
        state[static_cast<std::size_t>(candidate.move.from)] = old_from;
        state[static_cast<std::size_t>(candidate.move.to)] = old_to;
        min_exceeded = std::min(min_exceeded, r.next_bound);
    }

    return SearchResult{false, min_exceeded};
}

std::optional<std::vector<Move>> solve(GameState initial) {
    if (is_goal(initial)) return std::vector<Move>{};

    int bound = heuristic(initial);
    std::vector<Move> path;

    while (bound < std::numeric_limits<int>::max()) {
        std::unordered_map<Key, std::uint8_t, KeyHash> best_depth;
        SearchResult r = dfs_ida(initial, 0, bound, std::nullopt, best_depth, path);
        if (r.found) return path;
        if (r.next_bound == std::numeric_limits<int>::max()) break;
        bound = r.next_bound;
        path.clear();
    }

    return std::nullopt;
}

bool read_input(GameState& state) {
    int n = 0;
    int capacity = 0;
    if (!(std::cin >> n >> capacity)) return false;
    if (n != kTubeCount || capacity != kCapacity) return false;

    std::array<std::array<int, kCapacity>, kTubeCount> raw{};
    std::array<int, kTubeCount> lengths{};
    std::array<int, kTubeCount * kCapacity> seen_colors{};
    std::array<int, kTubeCount * kCapacity> seen_counts{};
    int distinct_colors = 0;

    for (int t = 0; t < kTubeCount; ++t) {
        int k = -1;
        if (!(std::cin >> k)) return false;
        if (k < 0 || k > kCapacity) return false;
        lengths[static_cast<std::size_t>(t)] = k;

        for (int i = 0; i < k; ++i) {
            int color = -1;
            if (!(std::cin >> color)) return false;
            if (color < 0) return false;
            raw[static_cast<std::size_t>(t)][static_cast<std::size_t>(i)] = color;

            int index = -1;
            for (int c = 0; c < distinct_colors; ++c) {
                if (seen_colors[static_cast<std::size_t>(c)] == color) {
                    index = c;
                    break;
                }
            }
            if (index == -1) {
                if (distinct_colors == kColorCount) return false;
                index = distinct_colors++;
                seen_colors[static_cast<std::size_t>(index)] = color;
            }
            ++seen_counts[static_cast<std::size_t>(index)];
        }
    }

    if (distinct_colors != kColorCount) return false;
    for (int c = 0; c < distinct_colors; ++c) {
        if (seen_counts[static_cast<std::size_t>(c)] != kCapacity) return false;
    }

    std::array<int, kColorCount> colors{};
    for (int c = 0; c < kColorCount; ++c) {
        colors[static_cast<std::size_t>(c)] = seen_colors[static_cast<std::size_t>(c)];
    }
    std::sort(colors.begin(), colors.end());

    state = {};
    for (int t = 0; t < kTubeCount; ++t) {
        std::array<Color, kCapacity> remapped{};
        int len = lengths[static_cast<std::size_t>(t)];
        for (int i = 0; i < len; ++i) {
            int color = raw[static_cast<std::size_t>(t)][static_cast<std::size_t>(i)];
            auto it = std::lower_bound(colors.begin(), colors.end(), color);
            if (it == colors.end() || *it != color) return false;
            remapped[static_cast<std::size_t>(i)] = static_cast<Color>((it - colors.begin()) + 1);
        }
        state[static_cast<std::size_t>(t)] = make_tube(remapped, len);
    }

    return true;
}

}  // namespace

int main() {
    GameState initial{};

    if (!read_input(initial)) {
        std::cout << "UNSOLVABLE\n";
        return 0;
    }

    std::optional<std::vector<Move>> solution = solve(initial);
    if (!solution) {
        std::cout << "UNSOLVABLE\n";
        return 0;
    }

    std::cout << "SOLVABLE\n";
    std::cout << solution->size() << '\n';
    for (const Move& move : *solution) {
        std::cout << static_cast<int>(move.from + 1) << ' ' << static_cast<int>(move.to + 1) << '\n';
    }

    return 0;
}
