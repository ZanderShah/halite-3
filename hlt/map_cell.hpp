#pragma once

#include "dropoff.hpp"
#include "position.hpp"
#include "ship.hpp"
#include "types.hpp"

namespace hlt {

struct MapCell {
    Position position;
    Halite halite;
    std::shared_ptr<Ship> ship;
    std::shared_ptr<Ship> closest_ship;
    // Only has dropoffs and shipyards. If id is -1,
    // then it's a shipyard, otherwise it's a dropoff.
    std::shared_ptr<Entity> structure;

    Position closest_base;
    int close_allies = 0;
    int close_enemies = 0;
    bool really_there = false;
    std::unordered_map<Direction, size_t> close_ships;

    MapCell(int x, int y, Halite halite)
        : position(x, y), halite(halite), closest_base(position) {}

    bool is_empty() const { return !ship && !structure; }

    bool is_occupied() const { return static_cast<bool>(ship); }

    bool has_structure() const { return static_cast<bool>(structure); }

    bool inspired() const {
        return close_enemies >= constants::INSPIRATION_SHIP_COUNT;
    }

    void mark_unsafe(std::shared_ptr<Ship>& ship) {
        if (!this->ship || this->ship->halite > ship->halite) this->ship = ship;
    }
};

}  // namespace hlt
