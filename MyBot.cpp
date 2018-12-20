#include "hlt/game.hpp"
#include "hungarian/Hungarian.h"

#include <bits/stdc++.h>

using namespace std;
using namespace hlt;
using namespace constants;
using namespace chrono;

template <typename V>
using position_map = unordered_map<Position, V>;

Game game;
unordered_map<EntityId, Task> tasks;

double HALITE_RETURN;
const size_t MAX_WALKS = 500;

const double ALPHA = 0.35;
double ewma = MAX_HALITE;
bool should_spawn_ewma = true;

bool started_hard_return = false;

unordered_map<EntityId, int> last_moved;

set<EntityId> collide;

inline Halite extracted(Halite h) {
    return (h + EXTRACT_RATIO - 1) / EXTRACT_RATIO;
}

// Fluorine JSON.
stringstream flog;
void message(Position p, string c) {
    flog << "{\"t\": " << game.turn_number << ", \"x\": " << p.x
         << ", \"y\": " << p.y << ", \"color\": \"" << c << "\"}," << endl;
}

inline bool hard_stuck(shared_ptr<Ship> ship) {
    const Halite left = game.game_map->at(ship)->halite;
    return ship->halite < left / MOVE_COST_RATIO;
}

bool safe_to_move(shared_ptr<Ship> ship, Position p) {
    unique_ptr<GameMap>& game_map = game.game_map;

    MapCell* cell = game_map->at(p);
    if (!cell->is_occupied()) return true;
    if (ship->owner == cell->ship->owner) return false;
    if (tasks[ship->id] == HARD_RETURN) return true;

    // Estimate who is closer.
    int ally = 0, evil = 0;
    for (auto& it : game.me->ships) {
        if (it.second->id == ship->id || tasks[it.second->id] != EXPLORE)
            continue;
        int d = game_map->calculate_distance(p, it.second->position);
        ally += pow(2, 4 - d);
    }
    for (auto& it : game.players[cell->ship->owner]->ships) {
        if (it.second->id == cell->ship->id) continue;
        int d = game_map->calculate_distance(p, it.second->position);
        evil += pow(2, 4 - d);
    }
    return (game.players.size() == 2 || collide.count(ship->id)) && ally > evil;
}

void dijkstras(position_map<Halite>& dist, Position source) {
    unique_ptr<GameMap>& game_map = game.game_map;

    for (vector<MapCell>& cell_row : game_map->cells)
        for (MapCell& map_cell : cell_row) dist[map_cell.position] = 1e6;

    priority_queue<pair<Halite, Position>> pq;
    pq.emplace(0, source);
    dist[source] = 0;
    while (!pq.empty()) {
        Position p = pq.top().second;
        pq.pop();
        const Halite cost = game_map->at(p)->halite / MOVE_COST_RATIO;
        for (Position pp : p.get_surrounding_cardinals()) {
            pp = game_map->normalize(pp);
            if (game_map->calculate_distance(source, pp) <=
                game_map->calculate_distance(source, p)) {
                continue;
            }
            if (game_map->at(pp)->is_occupied()) continue;
            if (dist[p] + cost < dist[pp]) {
                dist[pp] = dist[p] + cost;
                pq.emplace(-dist[pp], pp);
            }
        }
    }
}

pair<Direction, double> random_walk(shared_ptr<Ship> ship, Position d) {
    unique_ptr<GameMap>& game_map = game.game_map;

    Position p = ship->position;
    Halite ship_halite = ship->halite;
    Halite map_halite = game_map->at(ship)->halite;
    Direction first_direction = Direction::UNDEFINED;
    Halite burned_halite = 0;

    double t = 1;
    for (; p != d; ++t) {
        auto moves = game_map->get_moves(p, d, ship_halite, map_halite);

        Direction d = moves[rand() % moves.size()];
        if (first_direction == Direction::UNDEFINED) first_direction = d;

        if (d == Direction::STILL) {
            Halite mined = extracted(map_halite);
            mined = min(mined, MAX_HALITE - ship_halite);
            ship_halite += mined;
            if (game.game_map->at(p)->inspired) {
                ship_halite += INSPIRED_BONUS_MULTIPLIER * mined;
                ship_halite = min(ship_halite, MAX_HALITE);
            }
            map_halite -= mined;
        } else {
            const Halite burned = map_halite / MOVE_COST_RATIO;
            ship_halite -= burned;
            p = game_map->normalize(p.directional_offset(d));
            map_halite = game_map->at(p)->halite;

            burned_halite += burned;
        }

        if (tasks[ship->id] == EXPLORE && ship_halite > HALITE_RETURN) break;
    }

    Halite end_mine = 0;
    if (p == d) {
        ++t;
        end_mine = extracted(game_map->at(d)->halite);
        if (game_map->at(p)->inspired)
            end_mine += INSPIRED_BONUS_MULTIPLIER * end_mine;
    }

    const Halite end_halite = ship_halite + end_mine - burned_halite;

    double rate;
    if (tasks[ship->id] == EXPLORE) {
        if (game.players.size() == 2)
            rate = (end_halite - ship->halite) / t;
        else
            rate = end_halite / t;
    } else {
        rate = end_halite / pow(t, 2);
    }
    return {first_direction, rate};
}

position_map<double> generate_costs(shared_ptr<Ship> ship) {
    unique_ptr<GameMap>& game_map = game.game_map;
    Position p = ship->position;

    position_map<double> surrounding_cost;

    // Default values.
    for (Position pp : p.get_surrounding_cardinals())
        surrounding_cost[game_map->normalize(pp)] = 1e5;

    if (p == ship->next) {
        surrounding_cost[p] = 1;
        return surrounding_cost;
    }

    // Optimize values with random walks.
    map<Direction, double> best_walk;
    double best = 1.0;
    for (size_t i = 0; i < MAX_WALKS; ++i) {
        auto walk = random_walk(ship, ship->next);
        best_walk[walk.first] = max(best_walk[walk.first], walk.second);
        best = max(best, walk.second);
    }
    for (auto& it : best_walk) {
        Position pp = game_map->normalize(p.directional_offset(it.first));
        surrounding_cost[pp] = pow(1e3, 1 - it.second / best);
    }

    if (last_moved[ship->id] <= game.turn_number - 5) surrounding_cost[p] = 1e7;

    return surrounding_cost;
}

Halite ideal_dropoff(Position p, Position f) {
    unique_ptr<GameMap>& game_map = game.game_map;

    const int close = max(15, game_map->width / 3);
    bool local_dropoffs = game_map->at(p)->has_structure();
    local_dropoffs |=
        game_map->calculate_distance(p, game.me->shipyard->position) <= close;
    for (auto& it : game.me->dropoffs) {
        local_dropoffs |=
            game_map->calculate_distance(p, it.second->position) <= close;
    }

    // Approximate number of turns saved mining out.
    Halite halite_around = 0;
    const int CLOSE_MINE = 5;
    for (int dy = -CLOSE_MINE; dy <= CLOSE_MINE; ++dy) {
        for (int dx = -CLOSE_MINE; dx <= CLOSE_MINE; ++dx) {
            if (abs(dx) + abs(dy) > CLOSE_MINE) continue;
            MapCell* cell = game_map->at(Position(p.x + dx, p.y + dy));
            if (cell->ship && cell->ship->owner != game.me->id) continue;
            halite_around += cell->halite;
        }
    }
    Halite saved =
        halite_around / MAX_HALITE * ewma *
        game_map->calculate_distance(p, game_map->at(p)->closest_base);
    // Approximate saved by returing.
    for (auto& it : game.me->ships) {
        auto ship = it.second;
        int d = game_map->calculate_distance(ship->position,
                                             game_map->at(ship)->closest_base);
        int dd = game_map->calculate_distance(ship->position, p);
        saved += max(0, d - dd) * ewma;
    }
    saved -= ewma * game_map->calculate_distance(p, f);

    // TODO: Test out dropoffs by EWMA.
    bool ideal = saved >= DROPOFF_COST;
    ideal &= !local_dropoffs;
    ideal &= game.turn_number <= MAX_TURNS - 75;
    ideal &= !started_hard_return;
    ideal &= game.me->ships.size() / (2.0 + game.me->dropoffs.size()) >= 10;

    return ideal * saved;
}
Halite ideal_dropoff(Position p) { return ideal_dropoff(p, p); }

int main(int argc, char* argv[]) {
    game.ready("HaoHaoBot");

    HALITE_RETURN = MAX_HALITE * 0.95;

    Halite total_halite = 0;
    for (vector<MapCell>& cells : game.game_map->cells)
        for (MapCell& cell : cells) total_halite += cell.halite;

    unordered_map<EntityId, Halite> last_halite;

    for (;;) {
        auto begin = steady_clock::now();

        game.update_frame();
        shared_ptr<Player> me = game.me;
        unique_ptr<GameMap>& game_map = game.game_map;

        vector<Command> command_queue;

        log::log("Dropoffs.");
        Halite wanted = 0;
#if 1
        for (auto it = me->ships.begin(); it != me->ships.end();) {
            auto ship = it->second;

            bool ideal = ideal_dropoff(ship->position);
            const Halite delta =
                DROPOFF_COST - game_map->at(ship)->halite - ship->halite;

            if (ideal && delta <= me->halite) {
                me->halite -= max(0, delta);
                command_queue.push_back(ship->make_dropoff());
                game.me->dropoffs[-ship->id] = make_shared<Dropoff>(
                    game.my_id, -ship->id, ship->position.x, ship->position.y);
                log::log("Dropoff created at", ship->position);

                me->ships.erase(it++);
            } else {
                // if (ideal) wanted = wanted ? min(wanted, delta) : delta;
                ++it;
            }
        }
#endif

        Halite current_halite = 0;
        log::log("Inspiration. Closest base.");
        for (vector<MapCell>& cell_row : game_map->cells) {
            for (MapCell& cell : cell_row) {
                Position p = cell.position;

                int close_enemies = 0;
                for (auto& player : game.players) {
                    if (player->id == game.my_id) continue;
                    for (auto& it : player->ships) {
                        Position pp = it.second->position;
                        close_enemies += game_map->calculate_distance(p, pp) <=
                                         INSPIRATION_RADIUS;
                    }
                }
                cell.inspired = close_enemies >= INSPIRATION_SHIP_COUNT;
                cell.close_ships = 0;
                cell.closest_base = me->shipyard->position;
                for (auto& it : me->dropoffs) {
                    if (game_map->calculate_distance(p, it.second->position) <
                        game_map->calculate_distance(p, cell.closest_base)) {
                        cell.closest_base = it.second->position;
                    }
                }

                current_halite += cell.halite;
            }
        }
        for (auto& it : me->ships) {
            ++game_map->at(game_map->at(it.second)->closest_base)->close_ships;
        }

        set<Position> targets;
        for (vector<MapCell>& cell_row : game_map->cells) {
            for (MapCell& cell : cell_row) targets.insert(cell.position);
        }
        for (auto& player : game.players) {
            if (player->id == me->id) continue;
            for (auto& it : player->ships) {
                auto ship = it.second;
                Position p = ship->position;
                MapCell* cell = game_map->at(p);

                if (game_map->calculate_distance(p, cell->closest_base) <= 1)
                    continue;

                if (game.players.size() == 4 && !collide.empty())
                    targets.erase(p);
                cell->mark_unsafe(it.second);

                if (hard_stuck(it.second)) continue;

                for (Position pp : p.get_surrounding_cardinals()) {
                    if (game.players.size() == 4 && !collide.empty())
                        targets.erase(game_map->normalize(pp));
                    game_map->at(pp)->mark_unsafe(it.second);
                }
            }
        }

        log::log("Tasks.");
        vector<shared_ptr<Ship>> returners, explorers;

        bool all_empty = true;
        for (vector<MapCell>& cell_row : game_map->cells)
            for (MapCell& cell : cell_row) all_empty &= !cell.halite;

        for (auto& it : me->ships) {
            shared_ptr<Ship> ship = it.second;
            const EntityId id = ship->id;

            MapCell* cell = game_map->at(ship);

            int closest_base_dist = game_map->calculate_distance(
                ship->position, cell->closest_base);

            // New ship.
            if (!tasks.count(id)) tasks[id] = EXPLORE;

            // Return estimate based on ewma.
            const int return_turn =
                (HALITE_RETURN - ship->halite) / ewma + game.turn_number;
            if (return_turn > MAX_TURNS && ship->halite > MAX_HALITE / 10) {
                tasks[id] = RETURN;
                collide.insert(id);
            }

            // Return estimate if forced.
            const int forced_return_turn =
                game.turn_number + closest_base_dist +
                game_map->at(cell->closest_base)->close_ships * 0.3;
            // TODO: Dry run of return.
            if (all_empty || forced_return_turn > MAX_TURNS) {
                tasks[id] = HARD_RETURN;
                started_hard_return = true;
            }

            switch (tasks[id]) {
                case EXPLORE:
                    if (ship->halite > HALITE_RETURN) tasks[id] = RETURN;
                    break;
                case RETURN:
                    if (!closest_base_dist) {
                        tasks[id] = EXPLORE;
                        last_halite[id] = 0;
                    }
                case HARD_RETURN:
                    break;
            }

            if (hard_stuck(ship)) {
                command_queue.push_back(ship->stay_still());
                targets.erase(ship->position);
                game_map->at(ship)->mark_unsafe(ship);
                continue;
            }

            // Hard return.
            if (tasks[id] == HARD_RETURN && closest_base_dist <= 1) {
                for (Direction d : ALL_CARDINALS) {
                    if (ship->position.directional_offset(d) ==
                        cell->closest_base) {
                        command_queue.push_back(ship->move(d));
                    }
                }
                continue;
            }

            switch (tasks[id]) {
                case EXPLORE:
                    explorers.push_back(ship);
                    break;
                case HARD_RETURN:
                    if (ship->position == cell->closest_base) break;
                case RETURN:
                    ship->next = cell->closest_base;
                    returners.push_back(ship);
            }
        }

        log::log("Explorer cost matrix.");
        if (!explorers.empty()) {
            vector<vector<double>> uncompressed_cost_matrix;
            vector<bool> is_top_target(targets.size());
            vector<double> top_score;

            for (auto& ship : explorers) {
                position_map<Halite> dist;
                dijkstras(dist, ship->position);
                priority_queue<double> pq;

                vector<double> uncompressed_cost;
                for (Position p : targets) {
                    MapCell* cell = game_map->at(p);

                    double d = game_map->calculate_distance(ship->position, p);
                    double dd = sqrt(
                        game_map->calculate_distance(p, cell->closest_base));

                    Halite profit = cell->halite - dist[p];
                    bool should_inspire =
                        game.players.size() == 4 || d <= INSPIRATION_RADIUS;
                    if (cell->inspired && should_inspire)
                        profit += INSPIRED_BONUS_MULTIPLIER * cell->halite;
                    if (d <= 1 && cell->ship && safe_to_move(ship, p))
                        profit += cell->ship->halite - ship->halite;
                    if (collide.count(ship->id) && cell->ship)
                        profit += cell->ship->halite;

                    double rate = profit / max(1.0, d + dd);

                    uncompressed_cost.push_back(-rate + 5e3);
                    pq.push(uncompressed_cost.back());
                    while (pq.size() > explorers.size() + 5) pq.pop();
                }

                for (size_t i = 0; i < uncompressed_cost.size(); ++i) {
                    if (!is_top_target[i] && uncompressed_cost[i] <= pq.top())
                        is_top_target[i] = true;
                }
                top_score.push_back(pq.top());
                uncompressed_cost_matrix.push_back(move(uncompressed_cost));
            }

            // Coordinate compress.
            vector<int> target_space;
            for (size_t i = 0; i < is_top_target.size(); ++i)
                if (is_top_target[i]) target_space.push_back(i);
            vector<vector<double>> cost_matrix;
            for (size_t i = 0; i < explorers.size(); ++i) {
                vector<double>& uncompressed_cost = uncompressed_cost_matrix[i];
                vector<double> cost;
                for (size_t j = 0; j < uncompressed_cost.size(); ++j) {
                    if (!is_top_target[j]) continue;

                    if (game.players.size() == 4) {
                        cost.push_back(uncompressed_cost[j]);
                        continue;
                    }

                    if (uncompressed_cost[j] > top_score[i]) {
                        cost.push_back(1e9);
                        continue;
                    }

                    auto it = targets.begin();
                    advance(it, j);

                    double c = 0;
                    for (size_t k = 0; k < 10; ++k) {
                        auto walk = random_walk(explorers[i], *it);
                        c = max(c, walk.second);
                    }
                    cost.push_back(-c + 5e3);
                }
                cost_matrix.push_back(move(cost));
            }

            vector<int> assignment(explorers.size());
            HungarianAlgorithm ha;
            ha.Solve(cost_matrix, assignment);

            for (size_t i = 0; i < explorers.size(); ++i) {
                auto it = targets.begin();
                advance(it, target_space[assignment[i]]);
                explorers[i]->next = *it;
                // message(explorers[i]->next, "green");
            }
        }

        log::log("Move cost matrix.");
        if (!explorers.empty() || !returners.empty()) {
            explorers.insert(explorers.end(), returners.begin(),
                             returners.end());

            set<Position> local_targets;
            for (auto ship : explorers) {
                Position p = ship->position;
                local_targets.insert(p);
                for (Position pp : p.get_surrounding_cardinals())
                    local_targets.insert(game_map->normalize(pp));
            }

            vector<Position> move_space(local_targets.begin(),
                                        local_targets.end());

            // Coordinate compress.
            position_map<int> move_indices;
            for (size_t i = 0; i < move_space.size(); ++i)
                move_indices[move_space[i]] = i;

            // Fill cost matrix. Optimal direction has low cost.
            vector<vector<double>> cost_matrix;
            for (auto ship : explorers) {
                vector<double> cost(move_space.size(), 1e9);

                position_map<double> surrounding_cost = generate_costs(ship);
                for (auto& it : surrounding_cost) {
                    if (safe_to_move(ship, it.first))
                        cost[move_indices[it.first]] = it.second;
                }

                cost_matrix.push_back(move(cost));
            }

            // Solve and execute moves.
            vector<int> assignment(explorers.size());
            HungarianAlgorithm ha;
            ha.Solve(cost_matrix, assignment);

            for (size_t i = 0; i < assignment.size(); ++i) {
                if (explorers[i]->position == move_space[assignment[i]]) {
                    game_map->at(explorers[i])->mark_unsafe(explorers[i]);
                    command_queue.push_back(explorers[i]->stay_still());
                }
                for (Direction d : ALL_CARDINALS) {
                    Position pp = game_map->normalize(
                        explorers[i]->position.directional_offset(d));
                    if (pp == move_space[assignment[i]]) {
                        command_queue.push_back(explorers[i]->move(d));
                        game_map->at(pp)->mark_unsafe(explorers[i]);
                        last_moved[explorers[i]->id] = game.turn_number;
                        break;
                    }
                }
            }
        }

        // Save for dropoff.
        for (auto ship : explorers) {
            bool ideal = ideal_dropoff(ship->next, ship->position);
            const Halite delta =
                DROPOFF_COST - game_map->at(ship)->halite - ship->halite;
            if (ideal) wanted = wanted ? min(wanted, delta) : delta;
        }

        if (game.turn_number % 5 == 0) {
            Halite h = 0;
            for (auto ship : explorers) {
                if (ship->halite >= last_halite[ship->id])
                    h += ship->halite - last_halite[ship->id];
                last_halite[ship->id] = ship->halite;
            }
            ewma = ALPHA * h / (explorers.size() * 5) + (1 - ALPHA) * ewma;
        }
        should_spawn_ewma =
            game.turn_number + 2 * SHIP_COST / ewma < MAX_TURNS - 75;
        log::log("EWMA:", ewma, "Should spawn ships:", should_spawn_ewma);

        log::log("Spawn ships.");
        size_t ship_lo = 0;
        if (!started_hard_return) {
            ship_lo = 1e3;
            for (auto& player : game.players) {
                if (player->id == game.my_id) continue;
                ship_lo = min(ship_lo, player->ships.size());
            }
        }

        bool should_spawn = me->halite >= SHIP_COST + wanted;
        should_spawn &= !game_map->at(me->shipyard)->is_occupied();
        should_spawn &= !started_hard_return;

        should_spawn &= should_spawn_ewma || me->ships.size() < ship_lo;
        should_spawn &= current_halite * 1.0 / total_halite >= 0.3;

#if 0
        should_spawn &= me->ships.empty();
#endif

        if (should_spawn) {
            command_queue.push_back(me->shipyard->spawn());
            log::log("Spawning ship!");
        }

        if (game.turn_number == MAX_TURNS && game.my_id == 0) {
            log::log("Done!");
            ofstream fout;
            fout.open("replays/__flog.json");
            fout << "[\n" << flog.str();
            fout.close();
        }

        if (!game.end_turn(command_queue)) break;

        auto end = steady_clock::now();
        log::log("Millis: ", duration_cast<milliseconds>(end - begin).count());
    }
}
