#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Color = int;
using Tube = std::vector<Color>;
using State = std::vector<Tube>;

struct Move {
    int from = -1;
    int to = -1;
};

struct SearchResult {
    bool found = false;
    int next_bound = std::numeric_limits<int>::max();
};

namespace {

int capacity_g = 0;

void canonicalize(State& state) {
    std::sort(state.begin(), state.end());
}

bool is_monochromatic(const Tube& tube) {
    if (tube.empty()) return true;
    return std::all_of(tube.begin(), tube.end(), [&](Color c) { return c == tube.front(); });
}

bool is_complete_tube(const Tube& tube, int capacity) {
    return static_cast<int>(tube.size()) == capacity && is_monochromatic(tube);
}

bool is_goal(const State& state) {
    std::vector<Color> seen;
    for (const Tube& tube : state) {
        if (tube.empty()) continue;
        if (!is_monochromatic(tube)) return false;
        if (std::find(seen.begin(), seen.end(), tube.front()) != seen.end()) return false;
        seen.push_back(tube.front());
    }
    return true;
}

int heuristic(const State& state) {
    int non_mono = 0;
    for (const Tube& tube : state) {
        if (!is_monochromatic(tube)) ++non_mono;
    }
    return non_mono;
}

bool can_move(const Tube& src, const Tube& dst, int capacity) {
    if (src.empty()) return false;
    if (static_cast<int>(dst.size()) >= capacity) return false;

    Color x = src.back();
    return dst.empty() || dst.back() == x;
}

int movable_amount(const Tube& src, const Tube& dst, int capacity) {
    Color x = src.back();

    int run = 0;
    for (int i = static_cast<int>(src.size()) - 1; i >= 0 && src[i] == x; --i) {
        ++run;
    }

    int space = capacity - static_cast<int>(dst.size());
    return std::min(run, space);
}

void apply_move(State& st, int s, int d, int capacity) {
    int amount = movable_amount(st[s], st[d], capacity);
    for (int i = 0; i < amount; ++i) {
        st[d].push_back(st[s].back());
        st[s].pop_back();
    }
}

std::string encode_state(const State& state) {
    State canonical = state;
    canonicalize(canonical);

    std::string out;
    out.reserve(canonical.size() * static_cast<std::size_t>(capacity_g + 1) * 2);
    for (const Tube& tube : canonical) {
        out.append(std::to_string(tube.size()));
        out.push_back(':');
        for (Color c : tube) {
            out.append(std::to_string(c));
            out.push_back(',');
        }
        out.push_back('|');
    }
    return out;
}

struct Successor {
    State state;
    Move move;
    int h = 0;
    int amount = 0;
    bool to_empty = false;
};

std::vector<Successor> next_states(const State& state, int capacity, const std::string& previous_key) {
    std::vector<Successor> result;
    std::unordered_map<std::string, bool> generated;
    int n = static_cast<int>(state.size());

    for (int s = 0; s < n; ++s) {
        const Tube& src = state[s];
        if (src.empty()) continue;
        if (is_complete_tube(src, capacity)) continue;

        for (int d = 0; d < n; ++d) {
            if (s == d) continue;
            const Tube& dst = state[d];
            if (!can_move(src, dst, capacity)) continue;

            int amount = movable_amount(src, dst, capacity);
            if (amount <= 0) continue;

            bool src_all_same = is_monochromatic(src);
            bool whole_source = amount == static_cast<int>(src.size());
            if (dst.empty() && src_all_same && whole_source) continue;

            State child = state;
            apply_move(child, s, d, capacity);

            std::string key = encode_state(child);
            if (!previous_key.empty() && key == previous_key) continue;
            if (generated.find(key) != generated.end()) continue;
            generated[key] = true;

            result.push_back(Successor{std::move(child), Move{s, d}, 0, amount, dst.empty()});
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
    int capacity,
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
    std::vector<Successor> successors = next_states(state, capacity, previous_key);
    for (const Successor& succ : successors) {
        path.push_back(succ.move);
        SearchResult r = dfs_ida(succ.state, g + 1, bound, capacity, key, best_depth, path);
        if (r.found) return r;
        path.pop_back();
        min_exceeded = std::min(min_exceeded, r.next_bound);
    }

    return SearchResult{false, min_exceeded};
}

std::optional<std::vector<Move>> solve(State initial, int capacity) {
    capacity_g = capacity;

    if (is_goal(initial)) return std::vector<Move>{};

    int bound = heuristic(initial);
    std::vector<Move> path;

    while (bound < std::numeric_limits<int>::max()) {
        std::unordered_map<std::string, int> best_depth;
        SearchResult r = dfs_ida(initial, 0, bound, capacity, "", best_depth, path);
        if (r.found) return path;
        if (r.next_bound == std::numeric_limits<int>::max()) break;
        bound = r.next_bound;
        path.clear();
    }

    return std::nullopt;
}

bool read_input(State& state, int& capacity) {
    int n = 0;
    if (!(std::cin >> n >> capacity)) return false;
    if (n <= 0 || capacity <= 0) return false;

    state.assign(static_cast<std::size_t>(n), Tube{});
    std::unordered_map<int, int> counts;

    for (int i = 0; i < n; ++i) {
        int k = -1;
        if (!(std::cin >> k)) return false;
        if (k < 0 || k > capacity) return false;

        state[i].reserve(static_cast<std::size_t>(k));
        for (int j = 0; j < k; ++j) {
            int color = -1;
            if (!(std::cin >> color)) return false;
            if (color < 0) return false;
            state[i].push_back(color);
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

    return true;
}

}  // namespace

int main() {
    State initial;
    int capacity = 0;

    if (!read_input(initial, capacity)) {
        std::cout << "UNSOLVABLE\n";
        return 0;
    }

    std::optional<std::vector<Move>> solution = solve(initial, capacity);
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
