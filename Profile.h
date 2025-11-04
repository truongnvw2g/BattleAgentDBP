#pragma once
#include "nlohmann/json.hpp"
#include <string>
#include <iostream>
#include <map>


enum class Moral { High, Medium, Low };

struct Position {
    double x, y;
};

class Profile {
public:
    std::string name;
    Position position;
    int initialNumOfTroops;
    int lostNumOfTroops;
    int deployedNumOfTroops;
    int speed;

    Moral moral;
    std::string commander;
    std::string troopType;

    std::string historySetting;
    std::string armySetting;
    std::string roleSetting;
    std::string troopInformation;
    std::string currentBattlefieldSituation;
    std::string actionList;
    std::string actionPropertyDefinition;
    std::string stagePropertyDefinition;
    std::string actionInstructionBlock;
    std::string jsonConstraintVariable;
    std::string initialMission;

    std::string currentAction;
    std::string currentStage;
    Position targetPosition;
    std::string targetedAgentName;
    int roundNb;

    std::map<std::string, int> equipment;
    std::map<std::string, double> tactics;
    std::map<std::string, int> ammo;
    bool encircled;

    std::vector<std::pair<int, Position>> positionHistDict;

    Profile(const nlohmann::json & config) {
        try {
            name                = config.value("name", "Unknown");
            commander           = config.value("commander", "Unknown");
            initialNumOfTroops  = config.value("initial_troops", 0);
            deployedNumOfTroops = 0;
            lostNumOfTroops     = 0;
            troopType           = config.value("troopType", "infantry");
            speed               = config.value("speed", 0);

            // Kiểm tra initial_position
            if (!config.contains("initial_position") || !config["initial_position"].is_array() ||
                config["initial_position"].size() != 2 || !config["initial_position"][0].is_number() ||
                !config["initial_position"][1].is_number()) {
                throw std::runtime_error("Invalid initial_position in Profile");
            }
            position = { config["initial_position"][0].get<double>(), config["initial_position"][1].get<double>() };
            targetPosition = position;

            currentAction     = config.value("currentAction", "Defend");
            currentStage      = config.value("currentStage", "In Battle");
            
            encircled         = false;
            roundNb           = 0;
            targetedAgentName = config.value("targetedAgentName", "");

            moral = (config.value("moral", "Medium") == "High") ? Moral::High :
                    (config.value("moral", "Medium") == "Low")  ? Moral::Low :
                                                                  Moral::Medium;

            // Kiểm tra ammo
            if (!config.contains("ammo") || !config["ammo"].is_object()) {
                ammo = nlohmann::json::object();
            } else {
                for (const auto & [weapon, qty] : config["ammo"].items()) {
                    if (!qty.is_number_integer()) {
                        ammo[weapon] = 0;
                    } else {
                        ammo[weapon] = qty.get<int>();
                    }
                }
            }

            // Kiểm tra equipment
            if (!config.contains("equipment") || !config["equipment"].is_object()) {
                equipment = nlohmann::json::object();
            } else {
                for (const auto & [equip, qty] : config["equipment"].items()) {
                    if (!qty.is_number_integer()) {                       
                        equipment[equip] = 0;
                    } else {
                        equipment[equip] = qty.get<int>();
                    }
                }
            }

            // Kiểm tra tactics
            if (!config.contains("tactics") || !config["tactics"].is_object()) {               
                tactics = {
                    {"stealth",  0.5},
                    { "assault", 0.5},
                    { "defense", 0.5}
                };
            } else {
                for (const auto & [tactic, value] : config["tactics"].items()) {
                    if (!value.is_number()) {                       
                        tactics[tactic] = 0.5;
                    } else {
                        tactics[tactic] = value.get<double>();
                    }
                }
            }

            historySetting           = config.value("historySetting", "");
            armySetting              = config.value("armySetting", "");
            roleSetting              = config.value("roleSetting", "");
            troopInformation         = config.value("troopInformation", "");
            actionList               = config.value("actionList", "[]");
            actionPropertyDefinition = config.value("actionPropertyDefinition", "{}");
            stagePropertyDefinition  = config.value("stagePropertyDefinition", "{}");
            // Sửa actionInstructionBlock
            actionInstructionBlock =
                config.contains("actionInstructionBlock") && config["actionInstructionBlock"].is_object() ?
                    config["actionInstructionBlock"].dump() :
                    config.value("actionInstructionBlock", "");
            jsonConstraintVariable      = config.value("jsonConstraintVariable", "{}");
            initialMission              = config.value("initialMission", "");
            currentBattlefieldSituation = config.value("currentBattlefieldSituation", "");
        } catch (const std::exception & e) {
            std::cout << "Failed to initialize Profile for agent " << name << ": " << e.what() << "\n";
            throw std::runtime_error("Failed to initialize Profile: " + std::string(e.what()));
        }
    }

    bool operator==(const Profile & other) const noexcept {
        return name == other.name && troopType == other.troopType && initialNumOfTroops == other.initialNumOfTroops;
    }

    bool operator!=(const Profile & other) const noexcept { return !(*this == other); }

    int remainingNumOfTroops() const {
        return std::max(0, initialNumOfTroops - deployedNumOfTroops - lostNumOfTroops);
    }

    void takeDamage(int damage) {
        lostNumOfTroops += std::min(damage, remainingNumOfTroops());
        if (remainingNumOfTroops() <= 0) currentStage = "Crushing Defeat";
    }

    void recoverTroops(int amount) {
        lostNumOfTroops -= std::min(amount, lostNumOfTroops);
        if (lostNumOfTroops < 0) lostNumOfTroops = 0;
    }

    void addTroops(int amount) { initialNumOfTroops += amount; }

    void positionUpdatedHist(int round, Position pos) { positionHistDict.push_back({ round, pos }); }

    void updateTroopInformation() {
        nlohmann::json troop_info;
        troop_info["remaining_troops"] = remainingNumOfTroops();
        troop_info["ammo"] = ammo;
        troop_info["equipment"] = equipment;
        troop_info["moral"] = (moral == Moral::High ? "High" : moral == Moral::Medium ? "Medium" : "Low");
        troopInformation = troop_info.dump();
    }

    nlohmann::json toJson() const {
        return {
            {"name", name},
            {"initial_position", {position.x, position.y}},
            {"initial_troops", initialNumOfTroops},
            {"speed",   speed },
            {"moral", moral == Moral::High ? "High" : moral == Moral::Medium ? "Medium" : "Low"},
            {"commander", commander},
            {"equipment", equipment},
            {"tactics", tactics},
            {"ammo", ammo},
            {"initialMission", initialMission},
            {"historySetting", historySetting},
            {"armySetting", armySetting},
            {"roleSetting", roleSetting},
            {"troopInformation", troopInformation}
        };
    }

    double getDistanceTo(const Position & other) const {
        return std::hypot(position.x - other.x, position.y - other.y);
    }

    double getDistanceToTarget() const {
        if (targetPosition.x == 0 && targetPosition.y == 0) {
            return -1;
        }
        return getDistanceTo(targetPosition);
    }
    std::string getMoralString() const {
        return moral == Moral::High ? "High" : moral == Moral::Medium ? "Medium" : "Low";
    }

    
    nlohmann::json Profile::prompt() const {
        nlohmann::json profile_json;
        try {
            // Basic Profile Information
            profile_json["Name"]              = name;
            profile_json["Commander"]         = commander;
            profile_json["Troop Type"]        = troopType;
            profile_json["Initial Mission"]   = initialMission;
            profile_json["Current Action"]    = currentAction;
            profile_json["Current Stage"]     = currentStage;
            profile_json["speed"]             = speed;
            profile_json["Encircled"]         = encircled ? "Yes" : "No";
            profile_json["Target Agent Name"] = targetedAgentName;
            profile_json["Position"]          = { position.x, position.y };           
            profile_json["Morale"]          = moral == Moral::High ? "High" : moral == Moral::Medium ? "Medium" : "Low";
            profile_json["Initial Troops"]  = initialNumOfTroops;
            profile_json["Deployed Troops"] = deployedNumOfTroops;
            profile_json["Lost Troops"]     = lostNumOfTroops;
            profile_json["Remaining Troops"] = remainingNumOfTroops();
            profile_json["Round"]            = roundNb;

           

            // Ammo, Equipment, and Tactics
            try {
                profile_json["Ammo"]      = ammo;
                profile_json["Equipment"] = equipment;
                profile_json["Tactics"]   = tactics;
            } catch (const std::exception & e) {              
                profile_json["Ammo"]      = "Not available";
                profile_json["Equipment"] = "Not available";
                profile_json["Tactics"]   = "Not available";
            }

            // Historical Context
            profile_json["History Setting"] = historySetting;
            profile_json["Army Setting"]    = armySetting;
            profile_json["Role Setting"]    = roleSetting;
            // Position History
            nlohmann::json position_history = nlohmann::json::array();
            for (const auto & [round, pos] : positionHistDict) {
                nlohmann::json entry;
                entry["Round"]    = round;
                entry["Position"] = { pos.x, pos.y };
                position_history.push_back(entry);
            }
            profile_json["Position History"] = position_history;

        } catch (const std::exception & e) {           
            profile_json["Error"] = "Failed to construct prompt: " + std::string(e.what());
        }

        return profile_json;
    }
};
