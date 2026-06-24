#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using Color = std::uint16_t;

struct Move {
    int from = -1;
    int to = -1;
};

struct State {
    int n = 0;
    int capacity = 0;
    std::vector<int> len;
    std::vector<Color> cells;

    Color at(int tube, int pos) const {
        return cells[static_cast<std::size_t>(tube * capacity + pos)];
    }

    Color& at(int tube, int pos) {
        return cells[static_cast<std::size_t>(tube * capacity + pos)];
    }
};

struct TubeInfo {
    bool empty = true;
    bool full = false;
    bool mono = true;
    bool complete = false;
    Color bottom = 0;
    int run = 0;
};

struct SearchResult {
    bool found = false;
    int next_bound = std::numeric_limits<int>::max();
};

struct Successor {
    State state;
    Move move;
    int h = 0;
    int amount = 0;
    bool to_empty = false;
};

namespace {

int color_count_g = 0;

State make_state(int n, int capacity) {
    State st;
    st.n = n;
    st.capacity = capacity;
    st.len.assign(static_cast<std::size_t>(n), 0);
    st.cells.assign(static_cast<std::size_t>(n * capacity), 0);
    return st;
}

TubeInfo inspect_tube(const State& state, int tube) {
    TubeInfo info;
    int length = state.len[static_cast<std::size_t>(tube)];
    info.empty = length == 0;
    info.full = length == state.capacity;
    if (info.empty) return info;

    Color first = state.at(tube, 0);
    info.bottom = state.at(tube, length - 1);
    info.mono = true;
    for (int i = 1; i < length; ++i) {
        if (state.at(tube, i) != first) {
            info.mono = false;
            break;
        }
    }
    for (int i = length - 1; i >= 0 && state.at(tube, i) == info.bottom; --i) {
        ++info.run;
    }
    info.complete = info.full && info.mono;
    return info;
}

bool is_goal(const State& state) {
    std::vector<bool> seen(static_cast<std::size_t>(color_count_g), false);
    for (int t = 0; t < state.n; ++t) {
        TubeInfo info = inspect_tube(state, t);
        if (info.empty) continue;
        if (!info.mono) return false;
        Color color = state.at(t, 0);
        if (seen[static_cast<std::size_t>(color)]) return false;
        seen[static_cast<std::size_t>(color)] = true;
    }
    return true;
}

int heuristic(const State& state) {
    int non_mono = 0;
    std::vector<bool> color_in_tube(static_cast<std::size_t>(color_count_g), false);
    std::vector<int> tubes_containing_color(static_cast<std::size_t>(color_count_g), 0);

    for (int t = 0; t < state.n; ++t) {
        TubeInfo info = inspect_tube(state, t);
        if (!info.mono) ++non_mono;

        std::fill(color_in_tube.begin(), color_in_tube.end(), false);
        int length = state.len[static_cast<std::size_t>(t)];
        for (int i = 0; i < length; ++i) {
            Color color = state.at(t, i);
            if (!color_in_tube[static_cast<std::size_t>(color)]) {
                color_in_tube[static_cast<std::size_t>(color)] = true;
                ++tubes_containing_color[static_cast<std::size_t>(color)];
            }
        }
    }

    int split_color_lower_bound = 0;
    for (int count : tubes_containing_color) {
        if (count > 1) split_color_lower_bound += count - 1;
    }
    return std::max(non_mono, split_color_lower_bound);
}

void append_u16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xffU));
    out.push_back(static_cast<char>((value >> 8U) & 0xffU));
}

void append_u32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xffU));
    out.push_back(static_cast<char>((value >> 8U) & 0xffU));
    out.push_back(static_cast<char>((value >> 16U) & 0xffU));
    out.push_back(static_cast<char>((value >> 24U) & 0xffU));
}

std::string tube_key(const State& state, int tube) {
    std::string out;
    int length = state.len[static_cast<std::size_t>(tube)];
    out.reserve(static_cast<std::size_t>(4 + 2 * length));
    append_u32(out, static_cast<std::uint32_t>(length));
    for (int i = 0; i < length; ++i) {
        append_u16(out, state.at(tube, i));
    }
    return out;
}

std::string encode_state(const State& state) {
    std::vector<std::string> tubes;
    tubes.reserve(static_cast<std::size_t>(state.n));
    for (int t = 0; t < state.n; ++t) {
        tubes.push_back(tube_key(state, t));
    }
    std::sort(tubes.begin(), tubes.end());

    std::string out;
    out.reserve(static_cast<std::size_t>(state.n * (5 + 2 * state.capacity)));
    for (const std::string& tube : tubes) {
        out.append(tube);
        out.push_back('\xff');
    }
    return out;
}

bool can_move(const TubeInfo& src, const TubeInfo& dst) {
    if (src.empty || dst.full) return false;
    return dst.empty || dst.bottom == src.bottom;
}

void apply_move(State& state, int from, int to, int amount) {
    int from_len = state.len[static_cast<std::size_t>(from)];
    int to_len = state.len[static_cast<std::size_t>(to)];
    for (int i = 0; i < amount; ++i) {
        state.at(to, to_len + i) = state.at(from, from_len - 1 - i);
    }
    state.len[static_cast<std::size_t>(from)] = from_len - amount;
    state.len[static_cast<std::size_t>(to)] = to_len + amount;
}

std::vector<Successor> next_states(const State& state, const std::string& previous_key) {
    std::vector<Successor> result;
    std::unordered_set<std::string> generated;
    std::vector<TubeInfo> info(static_cast<std::size_t>(state.n));
    for (int t = 0; t < state.n; ++t) {
        info[static_cast<std::size_t>(t)] = inspect_tube(state, t);
    }

    for (int s = 0; s < state.n; ++s) {
        const TubeInfo& src = info[static_cast<std::size_t>(s)];
        if (src.empty || src.complete) continue;

        for (int d = 0; d < state.n; ++d) {
            if (s == d) continue;
            const TubeInfo& dst = info[static_cast<std::size_t>(d)];
            if (!can_move(src, dst)) continue;

            int space = state.capacity - state.len[static_cast<std::size_t>(d)];
            int amount = std::min(src.run, space);
            if (amount <= 0) continue;

            bool whole_source = amount == state.len[static_cast<std::size_t>(s)];
            if (dst.empty && src.mono && whole_source) continue;

            State child = state;
            apply_move(child, s, d, amount);

            std::string key = encode_state(child);
            if (!previous_key.empty() && key == previous_key) continue;
            if (!generated.insert(key).second) continue;

            result.push_back(Successor{std::move(child), Move{s, d}, 0, amount, dst.empty});
            result.back().h = heuristic(result.back().state);
        }
    }

    std::sort(result.begin(), result.end(), [](const Successor& a, const Successor& b) {
        if (a.h != b.h) return a.h < b.h;
        if (a.to_empty != b.to_empty) return !a.to_empty && b.to_empty;
        return a.amount > b.amount;
    });

    return result;
}

SearchResult dfs_ida(
    const State& state,
    int g,
    int bound,
    const std::string& previous_key,
    std::unordered_map<std::string, int>& best_depth,
    std::vector<Move>& path
) {
    int h = heuristic(state);
    int f = g + h;
    if (f > bound) return SearchResult{false, f};
    if (is_goal(state)) return SearchResult{true, bound};

    std::string key = encode_state(state);
    auto it = best_depth.find(key);
    if (it != best_depth.end() && it->second <= g) {
        return SearchResult{false, std::numeric_limits<int>::max()};
    }
    best_depth[key] = g;

    int min_exceeded = std::numeric_limits<int>::max();
    std::vector<Successor> successors = next_states(state, previous_key);
    for (const Successor& succ : successors) {
        path.push_back(succ.move);
        SearchResult r = dfs_ida(succ.state, g + 1, bound, key, best_depth, path);
        if (r.found) return r;
        path.pop_back();
        min_exceeded = std::min(min_exceeded, r.next_bound);
    }

    return SearchResult{false, min_exceeded};
}

std::optional<std::vector<Move>> solve(const State& initial) {
    if (is_goal(initial)) return std::vector<Move>{};

    int bound = heuristic(initial);
    std::vector<Move> path;

    while (bound < std::numeric_limits<int>::max()) {
        std::unordered_map<std::string, int> best_depth;
        SearchResult r = dfs_ida(initial, 0, bound, "", best_depth, path);
        if (r.found) return path;
        if (r.next_bound == std::numeric_limits<int>::max()) break;
        bound = r.next_bound;
        path.clear();
    }

    return std::nullopt;
}

bool read_input(State& state) {
    int n = 0;
    int capacity = 0;
    if (!(std::cin >> n >> capacity)) return false;
    if (n <= 0 || capacity <= 0) return false;
    if (static_cast<long long>(n) * capacity > std::numeric_limits<int>::max()) return false;

    std::vector<std::vector<int>> raw(static_cast<std::size_t>(n));
    std::unordered_map<int, int> counts;

    for (int i = 0; i < n; ++i) {
        int k = -1;
        if (!(std::cin >> k)) return false;
        if (k < 0 || k > capacity) return false;

        raw[static_cast<std::size_t>(i)].reserve(static_cast<std::size_t>(k));
        for (int j = 0; j < k; ++j) {
            int color = -1;
            if (!(std::cin >> color)) return false;
            if (color < 0) return false;
            raw[static_cast<std::size_t>(i)].push_back(color);
            ++counts[color];
        }
    }

    int expected_count = -1;
    for (const auto& [color, count] : counts) {
        (void)color;
        if (count <= 0 || count > capacity) return false;
        if (expected_count == -1) expected_count = count;
        if (count != expected_count) return false;
    }

    std::vector<int> colors;
    colors.reserve(counts.size());
    for (const auto& [color, count] : counts) {
        (void)count;
        colors.push_back(color);
    }
    std::sort(colors.begin(), colors.end());
    if (colors.size() > std::numeric_limits<Color>::max()) return false;

    std::unordered_map<int, Color> remap;
    remap.reserve(colors.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
        remap[colors[i]] = static_cast<Color>(i);
    }
    color_count_g = static_cast<int>(colors.size());

    state = make_state(n, capacity);
    for (int t = 0; t < n; ++t) {
        const std::vector<int>& tube = raw[static_cast<std::size_t>(t)];
        state.len[static_cast<std::size_t>(t)] = static_cast<int>(tube.size());
        for (std::size_t i = 0; i < tube.size(); ++i) {
            state.at(t, static_cast<int>(i)) = remap[tube[i]];
        }
    }

    return true;
}

}  // namespace

int main() {
    State initial;

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
        std::cout << (move.from + 1) << ' ' << (move.to + 1) << '\n';
    }

    return 0;
}
