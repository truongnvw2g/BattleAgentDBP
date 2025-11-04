#pragma once
#include "nlohmann/json.hpp"
#include "Profile.h"

#include <string>
#include <vector>

enum class TerrainType { Flat, Hills, Valley, River, Forest, Stronghold, Airfield, Tunnel };

class Agent;

struct TerrainObject {
    TerrainType type;
    Position    position;
    Position    end_position;  // For tunnels
    std::string name;
    double      speed_multiplier;
    int         health_bonus;
    int         loss_penalty;
    int         defense_bonus;
    int         artillery_range;
    int         power;                  // Capacity for tunnels
    double      stealth_bonus;
    int         construction_start;     // Round bắt đầu xây
    int         construction_complete;  // Round hoàn thành

    // Constructor với giá trị mặc định
    TerrainObject() :
        type(TerrainType::Flat),
        position({ 0, 0 }),
        end_position({ 0, 0 }),
        name(""),
        speed_multiplier(1.0),
        health_bonus(0),
        loss_penalty(0),
        defense_bonus(0),
        artillery_range(0),
        power(100),
        stealth_bonus(0.0),
        construction_start(0),
        construction_complete(0) {}
};

class BattleField {
  public:
    BattleField(double width, double height);
    ~BattleField();

    // Terrain management
    void setTerrain(double x, double y, TerrainType type, double speed_multiplier, int health_bonus, int loss_penalty);

    void setTerrainName(double              x,
                        double              y,
                        const std::string & name,
                        int                 defense_bonus,
                        int                 artillery_range,
                        int                 power);

    void addTunnel(const Position &    start,
                   const Position &    end,
                   const std::string & name,
                   double              speed_multiplier,
                   int                 defense_bonus,
                   int                 capacity,
                   double              stealth_bonus         = 0.2,
                   int                 construction_start    = 0,
                   int                 construction_complete = 0);

    // Weather control
    void setWeather(const std::string & type,
                    double              visibilityModifier,
                    double              artilleryModifier,
                    double              speedModifier = 1.0);

    nlohmann::json getWeather() const;

    double getVisibilityModifier() const { return visibilityModifier; }

    double getArtilleryModifier() const { return artilleryModifier; }

    double getSpeedModifier() const { return speedModifier; }

    double getAirdropSuccess() const;

    // Terrain queries
    std::vector<TerrainObject> getTerrainObjects() const;
    TerrainType                getTerrainTypeAt(double x, double y) const;
    TerrainObject *            getTerrainObject(double x, double y) const;
    double                     getSpeedMultiplier(TerrainType type) const;
    int                        getHealthBonus(TerrainType type) const;
    int                        getLossPenalty(TerrainType type) const;

    // Position validation
    bool isValidPosition(double x, double y) const;

    // Tactical evaluation
    bool   isInTunnel(Agent * agent, double tunnel_dist = 0.0, int current_round = -1) const;
    bool   isEnemyWithin(Agent * agent, double distance) const;
    double evaluateTacticalUse(Agent * agent) const;

    // Situation generation
    std::string generateBattlefieldSituation(Agent * agent = nullptr) const;
    std::string generateCompactSituation(Agent * agent) const;

    // Public members
    double                     width;
    double                     height;
    std::vector<TerrainObject> terrains;
    std::string                currentWeather;

  private:
    double visibilityModifier;
    double artilleryModifier;
    double speedModifier;

    // Helper functions
    std::string terrainTypeToString(TerrainType type) const;
    double      calculateDistanceToTunnel(const TerrainObject & tunnel, double x, double y) const;
    int         countUnitsInTunnel(const TerrainObject & tunnel, Agent * simulation) const;
};
