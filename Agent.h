#pragma once
#include "BattleField.h"
#include "LLMInference.h"
#include "nlohmann/json.hpp"
#include "Profile.h"

#include <map>
#include <string>
#include <vector>

class Simulation;  // Forward declaration
class SoldierAgent;

class Agent {
  public:
    Agent(const Profile & profile, Simulation * sim);
    ~Agent();

    bool operator==(const Agent & other) const noexcept { return this == &other || profile.name == other.profile.name; }

    bool operator!=(const Agent & other) const noexcept { return !(*this == other); }

    // Hierarchy management
    std::vector<Agent *> getChildren();
    Agent *              findChildByName(const std::string & name);

    Agent * getParent() { return parent; }

    Agent * Agent::getRootParent() {
        Agent * current = this;
        while (current->getParent() != nullptr) {
            current = current->getParent();
        }
        return current;
    }

    bool setParent(Agent * p) {
        if (p) {
            parent = p;
            return true;
        }
        return false;
    }

    // Target management
    Agent * getTarget() { return target; }

    bool setTarget(Agent * t) {
        if (t) {
            target                    = t;
            profile.targetedAgentName = t->profile.name;
            profile.targetPosition    = t->profile.position;
            return true;
        }
        return false;
    }

    // Faction identification
    bool        isCountryA();
    std::string getFaction();

    // Soldier management
    std::vector<SoldierAgent *> getSoldiers();
    std::string                 generateSoldierSummary();

    // Execution & combat
    nlohmann::json      execute();
    std::pair<int, int> estimateCasualties(int deployedNum, double visibilityModifier, double artilleryModifier);

    // Prompt generation
    std::vector<nlohmann::json> constructPrompt();
    std::string                 summarizeHistory(int max_len = 5) const;

    // Sub-agent management
    Agent *        spawnSubAgent(const nlohmann::json & action);
    nlohmann::json BranchStreamlining(const std::string & sub_agent_name);

    // Reporting
    nlohmann::json generateTargetSummary();
    nlohmann::json getProfilePrompt() const;

    // Public members
    Profile                     profile;
    Simulation *                simulation;
    Agent *                     target;
    bool                        mergedOrPruned;
    Agent *                     parent;
    std::vector<nlohmann::json> history;
};
