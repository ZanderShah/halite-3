#pragma once

#include "direction.hpp"
#include "types.hpp"

#include <iostream>

namespace hlt {

struct Position {
    int x, y;

    Position() : x(0), y(0) {}
    Position(const Position& p) : x(p.x), y(p.y) {}
    Position(int x, int y) : x(x), y(y) {}

    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }
    bool operator!=(const Position& other) const {
        return x != other.x || y != other.y;
    }

    Position doff(Direction d) const {
        auto dx = 0;
        auto dy = 0;
        switch (d) {
            case Direction::NORTH:
                dy = -1;
                break;
            case Direction::SOUTH:
                dy = 1;
                break;
            case Direction::EAST:
                dx = 1;
                break;
            case Direction::WEST:
                dx = -1;
                break;
            case Direction::STILL:
                // No move
                break;
            default:
                log::log(
                    std::string("Error: invert_direction: unknown direction ") +
                    static_cast<char>(d));
                exit(1);
        }
        return Position{x + dx, y + dy};
    }

    std::array<Position, 4> get_surrounding_cardinals() {
        return {{doff(Direction::NORTH), doff(Direction::SOUTH),
                 doff(Direction::EAST), doff(Direction::WEST)}};
    }
};

static bool operator<(Position u, Position v) {
    return u.x == v.x ? u.y < v.y : u.x < v.x;
}

static std::ostream& operator<<(std::ostream& out, const Position& position) {
    out << position.x << ' ' << position.y;
    return out;
}
static std::istream& operator>>(std::istream& in, Position& position) {
    in >> position.x >> position.y;
    return in;
}

}  // namespace hlt

namespace std {

template <>
struct hash<hlt::Position> {
    std::size_t operator()(const hlt::Position& position) const {
        return ((position.x + position.y) * (position.x + position.y + 1) / 2) +
               position.y;
    }
};

}  // namespace std
