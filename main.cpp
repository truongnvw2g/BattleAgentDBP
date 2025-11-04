#include "nlohmann/json.hpp"
#include "Simulation.h"

#include <ctime>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

void print_usage(const char * prog_name) {
    std::cerr << "\nUsage: " << prog_name << " <scenario.json>\n";
    std::cerr << "Example JSON:\n";
    std::cerr << R"({
      "model_path": "path/to/model.gguf",
      "prompt": "War at Dien Bien Phu, 1954. Vietnamese forces under Vo Nguyen Giap aim to defeat French forces led by Christian de Castries using guerrilla tactics, tunnel networks, and siege warfare.",
      "num_rounds": 30,
      "battle_config": {
        "combat_speed": 100,
        "max_rounds": 30
      },
      "red_configs": {
        "name": "Vietnamese",
        "initial_position": [500, 0],
        "initial_troops": 12000,
        "moral": "High",
        "commander": "Vo Nguyen Giap",
        "equipment": {"rifles": 8000, "machine_guns": 200, "mortars": 50},
        "tactics": {"stealth": 0.8, "assault": 0.6, "defense": 0.7},
        "ammo": {"ak": 5000, "artillery": 50},
        "initialMission": "Capture French strongholds like Him Lam [200, 200] using guerrilla tactics and tunnel networks",
        "System_Setting": "Simulation of Dien Bien Phu battle with realistic terrain, tunnel networks, and weather effects",
        "historySetting": "Dien Bien Phu, 1954. Vietnamese forces used tunnels to approach French strongholds like Him Lam [200, 200] undetected, leveraging heavy rain to mask movements.",
        "AmySetting": "Vietnamese army consists of infantry divisions with limited artillery support, specializing in guerrilla and siege tactics",
        "roleSetting": "Commander of Vietnamese forces",
        "troopInformation": "{\"remaining_troops\": 12000, \"ammo\": {\"ak\": 5000, \"artillery\": 50}, \"equipment\": {\"rifles\": 8000, \"machine_guns\": 200, \"mortars\": 50}, \"moral\": \"High\"}",
        "sub_agents": [
          {"name": "Vietnamese_Div1", "initialNumOfTroops": 4000, "initial_position": [450, 50], "troopType": "infantry"}
        ]
      },
      "green_configs": {
        "name": "French",
        "initial_position": [0, -200],
        "initial_troops": 6000,
        "moral": "Medium",
        "commander": "Christian de Castries",
        "equipment": {"rifles": 5000, "machine_guns": 150, "artillery": 30},
        "tactics": {"stealth": 0.4, "assault": 0.5, "defense": 0.9},
        "ammo": {"ak": 4000, "artillery": 50},
        "initialMission": "Defend Dien Bien Phu strongholds like Doc Lap [0, -200] against Vietnamese assault",
        "System_Setting": "Simulation of Dien Bien Phu battle with realistic terrain, tunnel networks, and weather effects",
        "historySetting": "Dien Bien Phu, 1954. French forces are entrenched in fortified positions like Doc Lap [0, -200], relying on artillery and air support.",
        "AmySetting": "French army consists of fortified infantry and artillery units with strong defensive capabilities",
        "roleSetting": "Commander of French forces",
        "troopInformation": "{\"remaining_troops\": 6000, \"ammo\": {\"ak\": 4000, \"artillery\": 50}, \"equipment\": {\"rifles\": 5000, \"machine_guns\": 150, \"artillery\": 30}, \"moral\": \"Medium\"}",
        "sub_agents": [
          {"name": "French_Div1", "initialNumOfTroops": 2000, "initial_position": [-50, -250], "troopType": "infantry"}
        ]
      },
      "terrain_config": {
        "width": 2000,
        "height": 2000,
        "terrains": [
          {
            "type": "Stronghold",
            "position": [0, -200],
            "name": "Doc Lap",
            "speed_multiplier": 1.0,
            "health_bonus": 100,
            "loss_penalty": 50,
            "defense_bonus": 20,
            "artillery_range": 200,
            "power": 1000
          },
          {
            "type": "Stronghold",
            "position": [200, 200],
            "name": "Him Lam",
            "speed_multiplier": 1.0,
            "health_bonus": 100,
            "loss_penalty": 50,
            "defense_bonus": 20,
            "artillery_range": 200,
            "power": 1000
          }
        ],
        "tunnels": [
          {
            "name": "Muong Thanh Tunnel",
            "start": [400, 400],
            "end": [200, 200],
            "speed_multiplier": 0.5,
            "defense_bonus": 30,
            "capacity": 1000,
            "stealth_bonus": 0.5
          }
        ],
        "weather": [
          {
            "turn": 1,
            "type": "Clear",
            "visibilityModifier": 1.0,
            "artilleryModifier": 1.0
          },
          {
            "turn": 10,
            "type": "Rain",
            "visibilityModifier": 0.5,
            "artilleryModifier": 0.7
          }
        ]
      },
      "actionList": [
        "Wait without Action",
        "Reposition Forces",
        "Initiate Skirmish",
        "Employ Artillery",
        "Ambush Enemy",
        "Construct Defenses",
        "Rally Troops",
        "Tactical Retreat",
        "Launch Full Assault",
        "Fortify Position",
        "Close-Quarters Assault",
        "Counterattack",
        "Conduct Reconnaissance",
        "Create Decoy Units",
        "Fortify Rear Guard",
        "Conduct Feigned Retreats",
        "Implement Guerilla Warfare",
        "Engage in Siege Warfare",
        "Organize Raiding Parties",
        "Execute Flanking Maneuvers",
        "Create Diversions",
        "Implement Supply Chain Disruption",
        "Move to Tunnel"
      ],
      "actionPropertyDefinition": {
        "Wait without Action": {"description": "Hold position without taking action.", "ammo_cost": 0, "effect": "none", "moral_impact": 0},
        "Reposition Forces": {"description": "Move forces to a new tactical position for better advantage.", "ammo_cost": 50, "effect": "move", "speed": 100},
        "Move to Tunnel": {"description": "Move forces into a tunnel for increased stealth and defense, limited by tunnel capacity.", "ammo_cost": 20, "effect": "stealth", "defense_bonus": 30, "stealth_bonus": 0.5}
      },
      "actionInstructionBlock": "Select one action per turn based on battlefield conditions, prioritizing faction objectives, resource availability, and morale. Vietnamese commanders must prioritize guerilla tactics (Implement Guerilla Warfare, Ambush Enemy) and siege operations (Engage in Siege Warfare) to capture French strongholds like Him Lam [200, 200]. Vietnamese commanders should prioritize Move to Tunnel when near tunnels and visibilityModifier < 0.7 to enhance stealth and defense. French commanders must prioritize SpawnSubAgent to defend Doc Lap [0, -200] if troops > 2000 and ammo[ak] >= 500. Adjust tactics based on weather: prioritize stealth-based actions (Ambush Enemy, Implement Guerilla Warfare, Move to Tunnel) when visibilityModifier < 0.7.",
      "jsonConstraintVariable": {
        "agentNextActionType": "string",
        "targetedAgentName": "string",
        "remarks": "string",
        "SubAgentsRecall": [ "string" ],
        "agentMoral": "string",
        "speed": "integer",
        "agentNextPosition": [ "number", "number" ],
        "deploySubUnit": "boolean",
        "inTunnel": "boolean",
        "weather_modifier": {
          "visibilityModifier": "number",
          "artilleryModifier": "number"
        },
        "actions": [
          {
            "subAgent_NextActionType": "string",
            "troopType": "string",
            "speed": "integer",
            "deployedNum": "integer",
            "ownPotentialLostNum": "integer",
            "enemyPotentialLostNum": "integer",
            "position": [ "number", "number" ],
            "agentName": "string",
            "remarks": "string",
            "inTunnel": "boolean"
          }
        ]
      }
    })"
              << "\n";
}

int main(int argc, char ** argv) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::ifstream json_file(argv[1]);
    if (!json_file.is_open()) {
        std::cerr << "Error: Cannot open file: " << argv[1] << std::endl;
        return 1;
    }

    json config;
    try {
        json_file >> config;
    } catch (const json::exception & e) {
        std::cerr << "Error: Failed to parse JSON: " << e.what() << std::endl;
        return 1;
    }
    json_file.close();

     const std::vector<std::string> required_keys = { "lla_modle_path",
                                                     "prompt",
                                                     "num_rounds",
                                                     "battle_config",
                                                     "red_configs",
                                                     "green_configs",
                                                     "terrain_config",
                                                     "actionList",
                                                     "actionPropertyDefinition",
                                                     "stagePropertyDefinition",
                                                     "definitionOfJsonKeys",
                                                     "jsonConstraintVariable",
                                                     "actionInstructionBlock",
                                                     "soldier_summary_config" };
    for (const auto & key : required_keys) {
        if (!config.contains(key)) {
            std::cerr << "Error: Missing required key in JSON: " << key << std::endl;
            return 1;
        }
    }

    try {
        auto add_field = [&](std::stringstream& stream, const std::string& name, const std::string& value) {
        if (!value.empty()) {
            stream << name << ": " << value << "\n";
        }
        else {
            stream << name << ": Not available\n";
        }
    };

    auto add_json_field = [&](std::stringstream& stream, const std::string& name, const std::string& value) {
        try {
            nlohmann::json json_value = nlohmann::json::parse(value);
            stream << name << ":\n" << json_value.dump(2) << "\n";
        }
        catch (const std::exception& e) {
            stream << name << ": Not available (JSON parse error): " << e.what() << "\n";
        }
    };
        Simulation sim(config);

        /* 
        std::vector<nlohmann::json> prompts;
        std::stringstream           prompt_system, prompt_user;

        prompt_system << "### SYSTEM INSTRUCTION & GLOBAL CONTEXT ###\n";
        prompt_system << "\n--- GLOBAL SETTINGS ---\n";
        add_field(prompt_system, "System Setting", sim.config["prompt"].dump());
        add_field(prompt_system, "History Setting",
                  sim.countryA->profile.historySetting + ". " + sim.countryB->profile.historySetting);
        add_field(prompt_system, "Army Setting",
                  sim.countryA->profile.armySetting + ". " + sim.countryB->profile.armySetting);
        add_field(prompt_system, "Role Setting",
                  sim.countryA->profile.roleSetting + ". " + sim.countryB->profile.roleSetting);

        prompt_system << "\n--- ACTION INSTRUCTION BLOCK ---\n";
        prompt_system << sim.config["actionInstructionBlock"].dump() << "\n";

        prompts.push_back({
            {"role",     "system"           },
            { "content", prompt_system.str()}
        });
        std::string bf = sim.field.generateBattlefieldSituation(sim.countryA);
        prompt_user << "CURRENT TACTICAL ENVIRONMENT (JSON Detailed)\n" << bf << std::endl;    

        prompt_user << "\n[ACTION REQUEST]\n";
        prompt_user << "Synthesize a battlefield assessment for both sides, including quantitative metrics (e.g., morale scores out of 100, distances in meters, troop percentages). Clearly describe the combat posture of the two sides (one side attacks, the other defends). Provide complete assessment without truncation."<< "\n";
        prompts.push_back({
            {"role",     "user"           },
            { "content", prompt_user.str()}
        });


       // nlohmann::json llm_json = nlohmann::json::parse(sim.llm->infer(prompts));
        //std::cout << llm_json.dump(2) << std::endl;
        sim.run(config["num_rounds"].get<int>());

        prompt_system << "Bạn là AI chỉ huy Trả về JSON đúng schema, không thêm text thừa\n";
        prompts.push_back({
            {"role",     "system"           },
            { "content", prompt_system.str()}
        });
        prompt_user << "Định nghĩa biến và quy tắc :\n";
        prompt_user << sim.config["definitionOfJsonKeys"] << "\n";
        prompt_user << "Synthesize a battlefield assessment for both sides, including quantitative metrics (e.g., morale scores out of 100, distances in meters, troop percentages). Clearly describe the combat posture of the two sides (one side attacks, the other defends). Provide complete assessment without truncation."<< "\n";
        prompts.push_back({
            {"role",     "user"           },
            { "content", prompt_user.str()}
        });

        nlohmann::json llm_json = nlohmann::json::parse(sim.llm->infer(prompts, sim.config["jsonConstraintVariable"]));
        std::cout << llm_json.dump(2) << std::endl;*/

        sim.run(config["num_rounds"].get<int>());

    } catch (const std::exception & e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
