#include "BattleField.h"

#include "Agent.h"
#include "Simulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

// ============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ============================================================================

BattleField::BattleField(double width, double height) :
    width(width),
    height(height),
    visibilityModifier(1.0),
    artilleryModifier(1.0),
    speedModifier(1.0),
    currentWeather("Clear") {}

BattleField::~BattleField() {}

// ============================================================================
// TERRAIN MANAGEMENT
// ============================================================================

void BattleField::setTerrain(double      x,
                             double      y,
                             TerrainType type,
                             double      speed_multiplier,
                             int         health_bonus,
                             int         loss_penalty) {
    TerrainObject obj;
    obj.type                  = type;
    obj.position              = { x, y };
    obj.speed_multiplier      = speed_multiplier;
    obj.health_bonus          = health_bonus;
    obj.loss_penalty          = loss_penalty;
    obj.defense_bonus         = 0;
    obj.artillery_range       = 0;
    obj.power                 = 100;
    obj.stealth_bonus         = 0.0;
    obj.construction_start    = 0;
    obj.construction_complete = 0;
    terrains.push_back(obj);
}

void BattleField::setTerrainName(double              x,
                                 double              y,
                                 const std::string & name,
                                 int                 defense_bonus,
                                 int                 artillery_range,
                                 int                 power) {
    for (auto & obj : terrains) {
        if (std::abs(obj.position.x - x) < 1e-6 && std::abs(obj.position.y - y) < 1e-6) {
            obj.name            = name;
            obj.defense_bonus   = defense_bonus;
            obj.artillery_range = artillery_range;
            obj.power           = power;
            break;
        }
    }
}

void BattleField::addTunnel(const Position &    start,
                            const Position &    end,
                            const std::string & name,
                            double              speed_multiplier,
                            int                 defense_bonus,
                            int                 capacity,
                            double              stealth_bonus,
                            int                 construction_start,
                            int                 construction_complete) {
    TerrainObject obj;
    obj.type                  = TerrainType::Tunnel;
    obj.position              = start;
    obj.end_position          = end;
    obj.name                  = name;
    obj.speed_multiplier      = speed_multiplier;
    obj.health_bonus          = 0;
    obj.loss_penalty          = 0;
    obj.defense_bonus         = defense_bonus;
    obj.artillery_range       = 0;
    obj.power                 = capacity;
    obj.stealth_bonus         = stealth_bonus;
    obj.construction_start    = construction_start;
    obj.construction_complete = construction_complete;
    terrains.push_back(obj);
}

// ============================================================================
// WEATHER CONTROL
// ============================================================================

void BattleField::setWeather(const std::string & type, double visibilityMod, double artilleryMod, double speedMod) {
    this->currentWeather     = type;
    this->visibilityModifier = visibilityMod;
    this->artilleryModifier  = artilleryMod;
    this->speedModifier      = speedMod;
}

nlohmann::json BattleField::getWeather() const {
    nlohmann::json weather;
    weather["type"]               = currentWeather;
    weather["visibilityModifier"] = visibilityModifier;
    weather["artilleryModifier"]  = artilleryModifier;
    weather["speed_modifier"]     = speedModifier;
    weather["airdrop_success"]    = getAirdropSuccess();
    return weather;
}

double BattleField::getAirdropSuccess() const {
    return artilleryModifier * 0.9;
}

// ============================================================================
// TERRAIN QUERIES
// ============================================================================

std::vector<TerrainObject> BattleField::getTerrainObjects() const {
    return terrains;
}

TerrainType BattleField::getTerrainTypeAt(double x, double y) const {
    for (const auto & obj : terrains) {
        if (obj.type == TerrainType::Tunnel) {
            double dist = calculateDistanceToTunnel(obj, x, y);
            if (dist <= 50.0) {
                return TerrainType::Tunnel;
            }
        } else if (std::abs(obj.position.x - x) < 50 && std::abs(obj.position.y - y) < 50) {
            return obj.type;
        }
    }
    return TerrainType::Flat;  // Default terrain type
}

TerrainObject * BattleField::getTerrainObject(double x, double y) const {
    TerrainObject * nearest      = nullptr;
    double          min_distance = std::numeric_limits<double>::max();

    for (const auto & obj : terrains) {
        double distance;

        if (obj.type == TerrainType::Tunnel) {
            distance = calculateDistanceToTunnel(obj, x, y);
        } else {
            distance = std::hypot(x - obj.position.x, y - obj.position.y);
        }

        if (distance < min_distance && distance < 100) {
            min_distance = distance;
            nearest      = const_cast<TerrainObject *>(&obj);
        }
    }

    return nearest;
}

double BattleField::getSpeedMultiplier(TerrainType type) const {
    for (const auto & obj : terrains) {
        if (obj.type == type) {
            return obj.speed_multiplier;
        }
    }
    return 1.0;
}

int BattleField::getHealthBonus(TerrainType type) const {
    for (const auto & obj : terrains) {
        if (obj.type == type) {
            return obj.health_bonus;
        }
    }
    return 0;
}

int BattleField::getLossPenalty(TerrainType type) const {
    for (const auto & obj : terrains) {
        if (obj.type == type) {
            return obj.loss_penalty;
        }
    }
    return 0;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

std::string BattleField::terrainTypeToString(TerrainType t) const {
    switch (t) {
        case TerrainType::Flat:
            return "flat";
        case TerrainType::Hills:
            return "hills";
        case TerrainType::Valley:
            return "valley";
        case TerrainType::River:
            return "river";
        case TerrainType::Forest:
            return "forest";
        case TerrainType::Stronghold:
            return "stronghold";
        case TerrainType::Airfield:
            return "airfield";
        case TerrainType::Tunnel:
            return "tunnel";
        default:
            return "unknown";
    }
}

double BattleField::calculateDistanceToTunnel(const TerrainObject & tunnel, double x, double y) const {
    double dx     = tunnel.end_position.x - tunnel.position.x;
    double dy     = tunnel.end_position.y - tunnel.position.y;
    double length = std::hypot(dx, dy);

    if (length < 1e-6) {
        return std::hypot(x - tunnel.position.x, y - tunnel.position.y);
    }

    // Project point onto tunnel line segment
    double t =
        std::max(0.0, std::min(1.0, ((x - tunnel.position.x) * dx + (y - tunnel.position.y) * dy) / (length * length)));

    double proj_x = tunnel.position.x + t * dx;
    double proj_y = tunnel.position.y + t * dy;

    return std::hypot(x - proj_x, y - proj_y);
}

int BattleField::countUnitsInTunnel(const TerrainObject & tunnel, Agent * agent) const {
    if (!agent || !agent->simulation) {
        return 0;
    }

    int    units_in_tunnel = 0;
    double tunnel_buffer   = 50.0;

    for (const auto * other : agent->simulation->getAgents()) {
        if (!other || other->mergedOrPruned || other->profile.currentStage == "Crushing Defeat" ||
            other->profile.currentStage == "Fleeing Off the Map") {
            continue;
        }

        double dist = calculateDistanceToTunnel(tunnel, other->profile.position.x, other->profile.position.y);

        if (dist <= tunnel_buffer) {
            units_in_tunnel += other->profile.remainingNumOfTroops();
        }
    }

    return units_in_tunnel;
}

// ============================================================================
// TACTICAL EVALUATION
// ============================================================================

bool BattleField::isInTunnel(Agent * agent, double tunnel_dist, int current_round) const {
    if (!agent) {
        return false;
    }

    if (tunnel_dist < 0) {
        tunnel_dist = 0.0;
    }

    // Get tunnel buffer from config
    double tunnel_buffer = 50.0;
    if (agent->simulation && agent->simulation->config.contains("actionPropertyDefinition")) {
        nlohmann::json action_defs = agent->simulation->config["actionPropertyDefinition"];
        if (action_defs.contains("Move to Tunnel") && action_defs["Move to Tunnel"].contains("tunnel_buffer")) {
            tunnel_buffer = action_defs["Move to Tunnel"]["tunnel_buffer"].get<double>();
        }
    }

    // Use agent's round if not specified
    if (current_round < 0) {
        current_round = agent->profile.roundNb;
    }

    for (const auto & obj : terrains) {
        if (obj.type != TerrainType::Tunnel) {
            continue;
        }

        // ✅ CHECK: Tunnel construction status
        if (obj.construction_complete > 0 && current_round < obj.construction_complete) {
            if (agent->simulation) {
                agent->simulation->logger.debug(current_round)
                    << "Tunnel " << obj.name << " not yet complete (round " << current_round << " < "
                    << obj.construction_complete << ")";
            }
            continue;
        }

        // Calculate distance to tunnel
        double dist_to_line = calculateDistanceToTunnel(obj, agent->profile.position.x, agent->profile.position.y);

        if (dist_to_line <= tunnel_dist + tunnel_buffer) {
            // Check tunnel capacity
            if (obj.power > 0) {
                int units_in_tunnel = countUnitsInTunnel(obj, agent);

                if (units_in_tunnel >= obj.power) {
                    if (agent->simulation) {
                        agent->simulation->logger.warn(current_round)
                            << "Tunnel " << obj.name << " at capacity (" << obj.power << ")";
                    }
                    continue;
                }
            }

            if (agent->simulation) {
                agent->simulation->logger.debug(current_round) << "Agent " << agent->profile.name << " in tunnel "
                                                               << obj.name << " (stealth=" << obj.stealth_bonus << ")";
            }

            return true;
        }
    }

    return false;
}

bool BattleField::isEnemyWithin(Agent * agent, double distance) const {
    if (!agent || !agent->simulation) {
        return false;
    }

    if (distance < 0) {
        distance = 0.0;
    }

    double      effective_distance = distance * visibilityModifier;
    std::string faction            = agent->getFaction();

    for (auto * other_agent : agent->simulation->getAgents()) {
        if (!other_agent || other_agent->mergedOrPruned || other_agent->profile.currentStage == "Crushing Defeat" ||
            other_agent->profile.currentStage == "Fleeing Off the Map") {
            continue;
        }

        // Skip same faction
        if (!faction.empty() && other_agent->getFaction() == faction) {
            continue;
        }

        // Calculate distance
        double dist = std::hypot(agent->profile.position.x - other_agent->profile.position.x,
                                 agent->profile.position.y - other_agent->profile.position.y);

        // Apply stealth factor if enemy in tunnel
        double enemy_stealth_factor = 1.0;
        if (isInTunnel(other_agent, 0.0)) {
            TerrainObject * tunnel = getTerrainObject(other_agent->profile.position.x, other_agent->profile.position.y);
            if (tunnel && tunnel->type == TerrainType::Tunnel) {
                enemy_stealth_factor = 1.0 - tunnel->stealth_bonus;
            }
        }

        if (dist <= effective_distance * enemy_stealth_factor) {
            if (agent->simulation) {
                agent->simulation->logger.debug(agent->profile.roundNb)
                    << "Enemy " << other_agent->profile.name << " detected at distance " << static_cast<int>(dist);
            }
            return true;
        }
    }

    return false;
}

double BattleField::evaluateTacticalUse(Agent * agent) const {
    if (!agent) {
        return 1.0;
    }

    double tactical_score   = 100.0;
    double influence_radius = 150.0;

    for (const auto & terrain : terrains) {
        double dist =
            std::hypot(agent->profile.position.x - terrain.position.x, agent->profile.position.y - terrain.position.y);

        if (dist > influence_radius) {
            continue;
        }

        double proximity = (influence_radius - dist) / influence_radius;

        switch (terrain.type) {
            case TerrainType::Flat:
                tactical_score += 10 * terrain.speed_multiplier * proximity;
                tactical_score -= 30 * proximity;
                break;

            case TerrainType::Hills:
                tactical_score += terrain.artillery_range * 1.2 * proximity;
                tactical_score += terrain.defense_bonus * 0.8 * proximity;
                tactical_score += 40 * proximity;
                break;

            case TerrainType::Valley:
                tactical_score += terrain.stealth_bonus * 60 * proximity;
                tactical_score -= terrain.loss_penalty * 0.5 * proximity;
                break;

            case TerrainType::River:
                tactical_score -= 80 * proximity;
                tactical_score -= terrain.loss_penalty * 0.8 * proximity;
                break;

            case TerrainType::Forest:
                tactical_score += terrain.stealth_bonus * 100 * proximity;
                tactical_score += terrain.defense_bonus * 0.5 * proximity;
                tactical_score -= 20 * (1.0 - terrain.speed_multiplier) * proximity;
                break;

            case TerrainType::Stronghold:
                tactical_score += terrain.defense_bonus * 2.0 * proximity;
                tactical_score += terrain.artillery_range * 1.0 * proximity;
                tactical_score += 100 * proximity;
                break;

            case TerrainType::Airfield:
                tactical_score += 50 * proximity;
                tactical_score -= 40 * (1.0 - terrain.defense_bonus / 100.0) * proximity;
                break;

            case TerrainType::Tunnel:
                tactical_score += terrain.defense_bonus * 1.5 * proximity;
                tactical_score += terrain.stealth_bonus * 120 * proximity;
                tactical_score += terrain.power * 0.5;
                break;
        }
    }

    // Bonus if in tunnel
    if (isInTunnel(agent, 0.0)) {
        tactical_score += 150;
    }

    double clamped_score = std::clamp(tactical_score, 10.0, 500.0);
    return clamped_score / 100.0;
}

// ============================================================================
// POSITION VALIDATION
// ============================================================================

bool BattleField::isValidPosition(double x, double y) const {
    // Check battlefield boundaries
    if (x < -width / 2 || x > width / 2 || y < -height / 2 || y > height / 2) {
        return false;
    }

    // Check terrain obstacles
    TerrainObject * terrain = getTerrainObject(x, y);
    if (terrain) {
        // Rivers are impassable
        if (terrain->type == TerrainType::River) {
            return false;
        }

        // Tunnels with no capacity are impassable
        if (terrain->type == TerrainType::Tunnel && terrain->power <= 0) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// SITUATION GENERATION
// ============================================================================

std::string BattleField::generateBattlefieldSituation(Agent * agent) const {
    nlohmann::json situation;

    try {
        if (!agent) {
            // Generate global battlefield situation
            situation["dimensions"] = { width, height };
            situation["weather"]    = getWeather();
            situation["terrains"]   = nlohmann::json::array();

            for (const auto & obj : terrains) {
                nlohmann::json terrain_info = {
                    {"type",              terrainTypeToString(obj.type)     },
                    { "position",         { obj.position.x, obj.position.y }},
                    { "name",             obj.name                          },
                    { "speed_multiplier", obj.speed_multiplier              },
                    { "defense_bonus",    obj.defense_bonus                 },
                    { "artillery_range",  obj.artillery_range               },
                    { "power",            obj.power                         },
                    { "stealth_bonus",    obj.stealth_bonus                 }
                };

                if (obj.type == TerrainType::Tunnel) {
                    terrain_info["end_position"]          = { obj.end_position.x, obj.end_position.y };
                    terrain_info["construction_complete"] = obj.construction_complete;
                }

                situation["terrains"].push_back(terrain_info);
            }

            return situation.dump(2);
        }

        // Generate agent-specific situation
        Position    pos         = agent->profile.position;
        TerrainType terrainType = getTerrainTypeAt(pos.x, pos.y);

        situation["local_terrain"] = terrainTypeToString(terrainType);
        situation["in_tunnel"]     = isInTunnel(agent, 0.0);
        situation["weather"]       = getWeather();

        // Find nearest terrain feature
        TerrainObject * nearest = getTerrainObject(pos.x, pos.y);
        if (nearest) {
            double distance;
            if (nearest->type == TerrainType::Tunnel) {
                distance = calculateDistanceToTunnel(*nearest, pos.x, pos.y);
            } else {
                distance = std::hypot(pos.x - nearest->position.x, pos.y - nearest->position.y);
            }

            situation["nearby_feature"] = {
                {"name",           nearest->name                     },
                { "type",          terrainTypeToString(nearest->type)},
                { "distance",      static_cast<int>(distance)        },
                { "defense_bonus", nearest->defense_bonus            }
            };
        }

        // Forces information
        nlohmann::json allied  = nlohmann::json::array();
        nlohmann::json enemy   = nlohmann::json::array();
        std::string    faction = agent->getFaction();

        for (auto * other : agent->simulation->getAgents()) {
            if (!other || other->mergedOrPruned || other->profile.currentStage == "Crushing Defeat" ||
                other->profile.currentStage == "Fleeing Off the Map") {
                continue;
            }

            nlohmann::json profile = {
                {"name",      other->profile.name                                     },
                { "position", { other->profile.position.x, other->profile.position.y }},
                { "troops",   other->profile.remainingNumOfTroops()                   },
                { "moral",    other->profile.getMoralString()                         }
            };

            if (other->getFaction() == faction) {
                allied.push_back(profile);
            } else {
                enemy.push_back(profile);
            }
        }

        situation["allied_forces"] = allied;
        situation["enemy_forces"]  = enemy;

        // Tactical evaluation
        double tactical_score       = evaluateTacticalUse(agent);
        situation["tactical_score"] = tactical_score;

        return situation.dump(2);

    } catch (const std::exception & e) {
        if (agent && agent->simulation) {
            agent->simulation->logger.error(agent->profile.roundNb)
                << "Failed to generate battlefield situation: " << e.what();
        }
        return "{}";
    }
}

std::string BattleField::generateCompactSituation(Agent * agent) const {
    if (!agent) {
        return "{}";
    }

    nlohmann::json compact;
    Position       pos = agent->profile.position;

    compact["you"] = {
        { "name", agent->profile.name },
        { "position", { pos.x, pos.y } },
        { "troops", agent->profile.remainingNumOfTroops() },
        { "morale", agent->profile.getMoralString() },
        { "in_tunnel", isInTunnel(agent, 0.0) }
    };

    compact["local_terrain"] = terrainTypeToString(getTerrainTypeAt(pos.x, pos.y));
    compact["weather"]       = getWeather();

    // Find nearest enemies
    nlohmann::json enemies = nlohmann::json::array();
    std::string    faction = agent->getFaction();
    int            count   = 0;

    for (auto * other : agent->simulation->getAgents()) {
        if (count >= 2 || !other || other->mergedOrPruned || other->getFaction() == faction ||
            other->profile.currentStage == "Crushing Defeat") {
            continue;
        }

        double dist = std::hypot(pos.x - other->profile.position.x, pos.y - other->profile.position.y);

        if (dist <= 800) {
            enemies.push_back({
                {"name",      other->profile.name                  },
                { "troops",   other->profile.remainingNumOfTroops()},
                { "distance", static_cast<int>(dist)               }
            });
            count++;
        }
    }

    compact["enemies"] = enemies;

    return compact.dump();
}
