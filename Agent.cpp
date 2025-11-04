#include "Agent.h"
#include "Soldier.h"
#include "Simulation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <sstream>
#include <set>

Agent::Agent(const Profile& profile, Simulation* sim) :
    parent(nullptr),
    profile(profile),
    simulation(sim),
    mergedOrPruned(false),
    target(nullptr) {}

Agent::~Agent() {}

std::vector<Agent*> Agent::getChildren() {
    std::vector<Agent*> children;
    if (!simulation) {
        return children;
    }
    for (Agent* agent : simulation->agents) {
        if (Agent* p = agent->getParent()) {
            if (p->profile.name == profile.name) {
                children.push_back(agent);
            }
        }
    }
    return children;
}

Agent* Agent::findChildByName(const std::string& name) {
    if (!simulation) {
        return nullptr;
    }
    for (Agent* agent : simulation->agents) {
        if (Agent* p = agent->getParent()) {
            if (p->profile.name == profile.name && agent->profile.name == name) {
                return agent;
            }
        }
    }
    return nullptr;
}

std::string Agent::getFaction() {
    Agent* root = getRootParent();
    if (root == simulation->countryA) {
        return simulation->countryA->profile.name;
    }
    else if (root == simulation->countryB) {
        return simulation->countryB->profile.name;
    }
    return "Unknown";
}

bool Agent::isCountryA() {
    Agent* root = getRootParent();
    return root == simulation->countryA;
}

std::vector<nlohmann::json> Agent::constructPrompt() {
    std::vector<nlohmann::json> prompts;
    std::stringstream           system_ss, user_ss;

    // === SYSTEM PROMPT ===
    system_ss << "### SYSTEM INSTRUCTION & GLOBAL CONTEXT ###\n\n";
    system_ss << "--- GLOBAL SETTINGS ---\n";
    system_ss << "SystemPrompt: " << simulation->config["prompt"].get<std::string>() << "\n";
    system_ss << "HistorySetting: " << profile.historySetting << "\n";
    system_ss << "ArmySetting: " << profile.armySetting << "\n";
    system_ss << "RoleSetting: " << profile.roleSetting << "\n";

    system_ss << "\n--- ACTION & STAGE DEFINITIONS ---\n";
    system_ss << "actionList:\n" << simulation->config["actionList"].dump(2) << "\n";
    system_ss << "actionPropertyDefinition:\n" << simulation->config["actionPropertyDefinition"].dump(2) << "\n";
    system_ss << "stagePropertyDefinition:\n" << simulation->config["stagePropertyDefinition"].dump(2) << "\n";

    system_ss << "\n!!! CRITICAL: FOLLOW ALL RULES IN definitionOfJsonKeys !!!\n";
    system_ss << "definitionOfJsonKeys:\n" << simulation->config["definitionOfJsonKeys"].dump(2) << "\n";

    system_ss << "\n--- ACTION INSTRUCTION BLOCK ---\n";
    system_ss << "actionInstructionBlock:\n" << simulation->config["actionInstructionBlock"].dump(2) << "\n";

    prompts.push_back({
        {"role",     "system"       },
        { "content", system_ss.str()}
    });

    // === USER PROMPT ===
    user_ss << "You are **" << profile.name << "**, commander of **" << profile.troopType << "** unit.\n";
    user_ss << "Side: **" << (isCountryA() ? "Vietnamese" : "French") << "**\n";
    user_ss << "Round: **" << profile.roundNb << " / " << simulation->config["num_rounds"].get<int>() << "**\n";
    user_ss << "**RETURN VALID JSON ONLY. NO TEXT OUTSIDE JSON.**\n\n";

    // === YOUR UNIT PROFILE ===
    user_ss << "=== YOUR UNIT PROFILE ===\n";
    user_ss << "- Name: " << profile.name << "\n";
    user_ss << "- Type: " << profile.troopType << "\n";
    user_ss << "- Troops: " << profile.remainingNumOfTroops() << " / " << profile.initialNumOfTroops << "\n";
    user_ss << "- Morale: " << profile.getMoralString() << "\n";
    user_ss << "- Position: [" << (int) profile.position.x << ", " << (int) profile.position.y << "]\n";
    user_ss << "- Speed: [" << (int) profile.speed << "]\n";

    // Tactics
    user_ss << "- Tactics: ";
    for (auto it = profile.tactics.begin(); it != profile.tactics.end(); ++it) {
        user_ss << it->first << "=" << std::fixed << std::setprecision(1) << it->second;
        if (std::next(it) != profile.tactics.end()) {
            user_ss << ", ";
        }
    }
    user_ss << "\n";

    // Equipment & Ammo
    user_ss << "- Equipment: ";
    for (const auto & [k, v] : profile.equipment) {
        if (v > 0) {
            user_ss << k << "=" << v << " ";
        }
    }
    user_ss << "\n";

    user_ss << "- Ammo: ";
    for (const auto & [k, v] : profile.ammo) {
        if (v > 0) {
            user_ss << k << "=" << v << " ";
        }
    }
    user_ss << "\n\n";

    // === COMMAND HIERARCHY ===
    user_ss << "=== COMMAND HIERARCHY ===\n";
    if (Agent * parent = getParent()) {
        if (!parent->mergedOrPruned && parent->profile.currentStage != "Crushing Defeat") {
            user_ss << "Parent: " << parent->profile.name << " | Troops: " << parent->profile.remainingNumOfTroops()
                    << " | Pos: [" << (int) parent->profile.position.x << "," << (int) parent->profile.position.y << "]\n"
                    << " | Speed: " << parent->profile.speed << "\n";
        } else {
            user_ss << "Parent: Unavailable (merged/defeated)\n";
        }
    } else {
        user_ss << "Parent: None (Root Agent)\n";
    }

    // === SUB-AGENTS ===
    user_ss << "\n=== YOUR SUB-AGENTS (YOU CAN CONTROL) ===\n";
    const auto & children         = getChildren();
    bool         has_self_created = false;

    for (const auto * sub : children) {
        if (sub->mergedOrPruned || sub->profile.currentStage == "Crushing Defeat") {
            continue;
        }
        if (sub->profile.name.find(profile.name + "_") != 0) {
            continue;
        }

        has_self_created = true;
        int    orig      = sub->profile.initialNumOfTroops;
        int    curr      = sub->profile.remainingNumOfTroops();
        double pct       = orig > 0 ? (double) curr / orig * 100 : 0;

        user_ss << "• " << sub->profile.name << " | Troops: " << curr << "/" << orig << " (" << (int) pct << "%)"
                << " | Mission: " << (sub->profile.currentAction.empty() ? "Unknown" : sub->profile.currentAction)
                << " | Pos: [" << (int) sub->profile.position.x << "," << (int) sub->profile.position.y << "]\n"
                << " | Speed: " << sub->profile.speed << "\n";
    }

    if (!has_self_created) {
        user_ss << "• None\n";
    }

    // === ORIGINAL SUB-AGENTS ===
    user_ss << "\n=== ORIGINAL SUB-AGENTS (DO NOT CONTROL) ===\n";
    bool has_original = false;

    for (const auto * sub : children) {
        if (sub->mergedOrPruned || sub->profile.currentStage == "Crushing Defeat") {
            continue;
        }
        if (sub->profile.name.find(profile.name + "_") == 0) {
            continue;
        }

        has_original = true;
        user_ss << "• [DO NOT TOUCH] " << sub->profile.name << " | Troops: " << sub->profile.remainingNumOfTroops()
                << " | Pos: [" << (int) sub->profile.position.x << "," << (int) sub->profile.position.y << "]\n";
    }

    if (!has_original) {
        user_ss << "• None\n";
    }

    // === HISTORICAL BATTLEFIELD SUMMARY ===
    if (!profile.currentBattlefieldSituation.empty()) {
        user_ss << "\n=== HISTORICAL BATTLEFIELD SUMMARY (FROM PREVIOUS ROUNDS) ===\n";
        user_ss << profile.currentBattlefieldSituation << "\n";
    }

    // === CURRENT BATTLEFIELD SITUATION ===
    try {
        user_ss << "\n=== CURRENT BATTLEFIELD SITUATION ===\n";
        user_ss << simulation->field.generateBattlefieldSituation(this) << "\n";
    } catch (const std::exception & e) {
        user_ss << "\n=== CURRENT BATTLEFIELD SITUATION ===\n[PARSE ERROR]: " << e.what() << "\n";
    }

    // ✅ THÊM: SOLDIER MORALE REPORT
    if (simulation->config.contains("soldier_summary_config")) {
        std::string soldier_report = generateSoldierSummary();
        if (!soldier_report.empty() && soldier_report.find("No soldiers") == std::string::npos) {
            user_ss << "\n" << soldier_report;
        }
    }

    // === HISTORICAL EVENTS ===
    if (simulation->config.contains("historical_events")) {
        user_ss << "\n=== RELEVANT HISTORICAL EVENTS ===\n";

        for (const auto & ev : simulation->config["historical_events"]) {
            if (ev.contains("round") && ev["round"].is_number_integer()) {
                int event_round = ev["round"].get<int>();
                if (event_round <= profile.roundNb) {
                    user_ss << "• Round " << event_round << ": " << ev["event"].get<std::string>() << "\n";
                }
            }
        }
    }

    // === ACTION REQUEST ===
    user_ss << "\n[ACTION REQUEST]\n";
    user_ss << "Your MISSION: **ATTACK " << profile.targetedAgentName << " AT ALL COSTS**\n";
    user_ss << "• `targetedAgentName` = **" << profile.targetedAgentName << "** → **NEVER CHANGE**\n";
    user_ss << "• `deploySubAgent = true` → Create NEW to clear blockers\n";
    user_ss << "• `deploySubAgent = false` → Update EXISTING sub-agent\n";
    user_ss << "• Recall ONLY when mission complete OR <30% troops\n";
    user_ss << "• Sub-agents DO NOT have targetedAgentName\n";
    user_ss << "• Output **VALID JSON ONLY**\n";

    prompts.push_back({
        {"role",     "user"       },
        { "content", user_ss.str()}
    });

    return prompts;
}

std::string Agent::summarizeHistory(int max_len) const {
    if (history.empty()) {
        return "";
    }

    nlohmann::json history_array = nlohmann::json::array();

    int history_limit = std::min(max_len, static_cast<int>(history.size()));
    size_t start_index = std::max(0, static_cast<int>(history.size()) - history_limit);

    for (size_t i = start_index; i < history.size(); ++i) {
        const auto& turn = history[i];

        int own_loss_total = 0, enemy_loss_total = 0;

        nlohmann::json sub_actions_summary = nlohmann::json::array();
        if (turn.contains("actions") && turn["actions"].is_array()) {
            for (const auto& action : turn["actions"]) {
                own_loss_total += action.value("ownLoss", 0);
                enemy_loss_total += action.value("enemyLoss", 0);

                sub_actions_summary.push_back({
                    {"agentName", action.value("agentName", "N/A")},
                    {"action", action.value("actionType", "None")},
                    {"deployedNum", action.value("deployedNum", 0)},
                    {"ownLoss", action.value("ownLoss", 0)},
                    {"enemyLoss", action.value("enemyLoss", 0)}
                    });
            }
        }
        nlohmann::json recall_summary = nlohmann::json::array();
        if (turn.contains("SubAgentsRecall") && turn["SubAgentsRecall"].is_array()) {
            recall_summary = turn["SubAgentsRecall"];
        }

        nlohmann::json turn_summary = {
            { "turn", static_cast<int>(i + 1) },
            { "agentNextActionType", turn.value("agentNextActionType", "Wait without Action") },
            { "agentStage", turn.value("agentStage", "In Battle") },
            { "targetedAgentName", turn.value("targetedAgentName", "None") },
            { "agentMoral", turn.value("agentMoral", "Unknown") },
            { "Recent history Battlefield Situation", turn.value("remarks", "No tactical notes provided") },          
            { "final_position", turn.value("agentNextPosition", nlohmann::json::array({0.0, 0.0})) },
            { "total_casualties", {
                { "own_lost", own_loss_total },
                { "enemy_lost", enemy_loss_total }
            },
            { "sub_units_actions", sub_actions_summary.empty() ? nlohmann::json::array() : sub_actions_summary },
            { "sub_units_recalled", recall_summary.empty() ? nlohmann::json::array() : recall_summary }
        }
        };

        history_array.push_back(turn_summary);
    }

    std::stringstream summary;
    summary << "### HISTORY SUMMARY (Last " << history_limit << " Rounds - Structured JSON) ###\n";
    summary << "This summary details past actions, outcomes, and the LLM's decision logic (Remarks), including the most recent round's logic.\n";
    summary << "The JSON structure directly reflects the output requirements for optimal context.\n";
    summary << history_array.dump(2);
    return summary.str();
}


nlohmann::json Agent::execute() {
    // Khởi tạo JSON kết quả
    nlohmann::json result = {
        { "agentName", profile.name },
        { "agentNextActionType", "Wait without Action" },
        { "agentStage", "In Battle" },
        { "currentBattlefieldSituation", "" },
        { "targetedAgentName", profile.targetedAgentName.empty() ? "None" : profile.targetedAgentName },
        { "agentMoral", profile.getMoralString() },
        { "speed", simulation->config["battle_config"].value("combat_speed", 80) },
        { "inTunnel", simulation->field.isInTunnel(this, 0.0) },
        { "weather_modifier", simulation->field.getWeather() },
        { "deploySubAgent", false },
        { "SubAgentsRecall", nlohmann::json::array() },
        { "mainOwnLoss", 0 },
        { "mainEnemyLoss", 0 },
        { "actions", nlohmann::json::array() },
        { "remarks", "" },
        { "agentNextPosition", { profile.position.x, profile.position.y } }
    };

    // Track stage duration
    static std::unordered_map<std::string, int> stage_duration;
    if (stage_duration.find(profile.name) == stage_duration.end()) {
        stage_duration[profile.name] = 0;
    }

    profile.updateTroopInformation();

    try {
        // === 1. GỌI LLM ===
        std::vector<nlohmann::json> prompts = constructPrompt();
        std::string    llm_response = simulation->llm->infer(prompts, simulation->config["jsonConstraintVariable"]);
        nlohmann::json llm_json     = nlohmann::json::parse(llm_response);
        simulation->logger.debug(profile.roundNb) << "Agent " << profile.name << " LLM: " << llm_json.dump(2);

        // Load configs
        nlohmann::json actionDefs   = simulation->config["actionPropertyDefinition"];
        nlohmann::json stageDefs    = simulation->config["stagePropertyDefinition"];
        nlohmann::json jsonKeys     = simulation->config["definitionOfJsonKeys"];
        nlohmann::json instructions = simulation->config["actionInstructionBlock"];

        // === 2. VALIDATE agentNextActionType ===
        std::string new_action = llm_json.value("agentNextActionType", "Wait without Action");
        if (!actionDefs.contains(new_action)) {
            simulation->logger.warn(profile.roundNb)
                << "Agent " << profile.name << ": Invalid action '" << new_action << "' → default";
            new_action = "Wait without Action";
        }

        // === 3. VALIDATE agentStage ===
        std::string new_stage = llm_json.value("agentStage", "In Battle");
        if (!stageDefs.contains(new_stage)) {
            simulation->logger.warn(profile.roundNb)
                << "Agent " << profile.name << ": Invalid stage '" << new_stage << "' → default";
            new_stage = "In Battle";
        }

        // === 4. VALIDATE agentNextPosition ===
        nlohmann::json pos_json =
            llm_json.value("agentNextPosition", nlohmann::json::array({ profile.position.x, profile.position.y }));
        double next_x = profile.position.x, next_y = profile.position.y;
        if (pos_json.is_array() && pos_json.size() == 2 && pos_json[0].is_number() && pos_json[1].is_number()) {
            double x = pos_json[0].get<double>(), y = pos_json[1].get<double>();
            if (simulation->field.isValidPosition(x, y)) {
                next_x = x;
                next_y = y;
                simulation->logger.info(profile.roundNb)
                    << "Agent " << profile.name << ": Moving to [" << x << "," << y << "]";
            }
        }
        profile.currentBattlefieldSituation = llm_json.value("currentBattlefieldSituation", "");
        // === 5. VALIDATE targetedAgentName ===
        std::string targeted_agent = llm_json.value("targetedAgentName", "None");
        Agent *     target_agent   = simulation->findAgent(targeted_agent);
        if (!target_agent || target_agent->mergedOrPruned || target_agent->profile.currentStage == "Crushing Defeat" ||
            target_agent->profile.currentStage == "Fleeing Off the Map") {
            targeted_agent = "None";
            setTarget(nullptr);
        } else {
            setTarget(target_agent);
            profile.targetedAgentName = targeted_agent;
        }

        // === 6. VALIDATE agentMoral ===
        std::string moral_str = llm_json.value("agentMoral", "Medium");
        profile.moral         = (moral_str == "High") ? Moral::High : (moral_str == "Low") ? Moral::Low : Moral::Medium;

        // === 7. VALIDATE speed, inTunnel, weather_modifier ===
        
        int speed = llm_json.value("speed", simulation->config["battle_config"].value("combat_speed", 80));
        if (speed < 0 || speed > 200) {
            speed = simulation->config["battle_config"].value("combat_speed", 80);
        }

        bool in_tunnel = llm_json.value("inTunnel", simulation->field.isInTunnel(this, 0.0));
        if (in_tunnel && !simulation->field.isInTunnel(this, 0.0)) {
            in_tunnel = simulation->field.isInTunnel(this, 0.0);
        }

        nlohmann::json weather_mod = simulation->field.getWeather();
        if (simulation->config.contains("terrain_config") && simulation->config["terrain_config"].contains("weather")) {
            for (const auto & w : simulation->config["terrain_config"]["weather"]) {
                if (w.contains("turn") && w["turn"].get<int>() == profile.roundNb) {
                    weather_mod = {
                        { "type", w["type"] },
                        { "visibilityModifier", w["visibilityModifier"] },
                        { "artilleryModifier", w.value("artilleryModifier", 1.0) },
                        { "speed_modifier", w.value("speed_modifier", 1.0) }
                    };

                    simulation->logger.info(profile.roundNb)
                        << "Agent " << profile.name << ": Using weather from config round " << profile.roundNb << " - "
                        << w["type"].get<std::string>();
                    break;
                }
            }
        }

        // === 8. ESTIMATE mainOwnLoss / mainEnemyLoss ===
        int main_own_loss   = llm_json.value("mainOwnLoss", 0);
        int main_enemy_loss = llm_json.value("mainEnemyLoss", 0);

        if ((main_own_loss == 0 || main_enemy_loss == 0) && getTarget()) {
            double vis      = weather_mod.value("visibilityModifier", 1.0);
            double art      = weather_mod.value("artilleryModifier", 1.0);
            auto [o, e]     = estimateCasualties(profile.remainingNumOfTroops(), vis, art);
            main_own_loss   = main_own_loss > 0 ? main_own_loss : o;
            main_enemy_loss = main_enemy_loss > 0 ? main_enemy_loss : e;
        }

        if (main_own_loss > 0) {
            profile.takeDamage(main_own_loss);
            simulation->logger.info(profile.roundNb) << "Agent " << profile.name << ": MAIN lost " << main_own_loss;
        }
        if (main_enemy_loss > 0 && getTarget()) {
            getTarget()->profile.takeDamage(main_enemy_loss);
            simulation->logger.info(profile.roundNb)
                << "Target " << getTarget()->profile.name << ": Lost " << main_enemy_loss << " by MAIN";
        }

        // === 9. VALIDATE SubAgentsRecall + deploySubAgent CONFLICT ===       
        nlohmann::json        recall_list    = llm_json.value("SubAgentsRecall", nlohmann::json::array());
        std::set<std::string> recall_names;
        for (const auto & r : recall_list) {
            if (r.is_string()) {
                recall_names.insert(r.get<std::string>());
            }
        }       

        // === 10. HANDLE actions + deploySubAgent ===
        nlohmann::json sub_actions   = llm_json.value("actions", nlohmann::json::array());
        nlohmann::json valid_actions = nlohmann::json::array();

        if (sub_actions.is_array()) {
            for (auto & act : sub_actions) {
                bool deploy_subunit = act.value("deploySubAgent", false);
                if (deploy_subunit) {
                    if (!act.contains("agentName") || !act.contains("deployedNum") || !act.contains("position")) {
                        continue;
                    }
                    int deploy_num = act["deployedNum"].get<int>();
                    if (deploy_num <= 0 || deploy_num > profile.remainingNumOfTroops() * 0.4) {
                        continue;
                    }

                    std::string name = act["agentName"].get<std::string>();
                    if (findChildByName(name) || recall_names.count(name)) {
                        name             = profile.name + "_Sub" + std::to_string(simulation->generateUniqueId());
                        act["agentName"] = name;
                    }

                    Agent * sub = spawnSubAgent(act);
                    if (!sub) {
                        continue;
                    }

                    int own_loss   = act.value("ownLoss", 0);
                    int enemy_loss = act.value("enemyLoss", 0);
                    if ((own_loss == 0 || enemy_loss == 0) && getTarget()) {
                        double vis  = weather_mod.value("visibilityModifier", 1.0);
                        double art  = weather_mod.value("artilleryModifier", 1.0);
                        auto [o, e] = sub->estimateCasualties(deploy_num, vis, art);
                        own_loss    = own_loss > 0 ? own_loss : o;
                        enemy_loss  = enemy_loss > 0 ? enemy_loss : e;
                    }

                    if (own_loss > 0) {
                        sub->profile.takeDamage(own_loss);
                    }
                    if (enemy_loss > 0 && getTarget()) {
                        getTarget()->profile.takeDamage(enemy_loss);
                    }

                    valid_actions.push_back({
                        { "actionType", act.value("actionType", "Unknown") },
                        { "troopType", sub->profile.troopType },
                        { "deploySubAgent", deploy_subunit },
                        { "agentName", name },
                        { "speed", act.value("speed",0) },
                        { "deployedNum", deploy_num },
                        { "position", act["position"] },
                        { "ownLoss", own_loss },
                        { "enemyLoss", enemy_loss },
                        { "inTunnel", act.value("inTunnel", false) },
                        { "remarks", act.value("remarks", "") }
                    });
                } else {
                 
                    if (!act.contains("agentName")) {
                        continue;
                    }
                    std::string name = act["agentName"].get<std::string>();
                    if (recall_names.count(name)) {
                        continue;
                    }

                    Agent * sub = findChildByName(name);
                    if (!sub) {
                        continue;
                    }

                    if (act.contains("position") && act.is_array() && act["position"].size() == 2) {
                        double x = act["position"][0].get<double>(), y = act["position"][1].get<double>();
                        if (simulation->field.isValidPosition(x, y)) {
                            sub->profile.position.x = x;
                            sub->profile.position.y = y;
                        }
                    }
                    int current_sub_troops = sub->profile.remainingNumOfTroops();
                    int new_sub_troops     = act.value("deployedNum", 0);
                    sub->profile.addTroops(new_sub_troops-current_sub_troops);
                    profile.addTroops(current_sub_troops - new_sub_troops);

                    profile.speed = act.value("speed", 0);

                    int own_loss   = act.value("ownLoss", 0);
                    int enemy_loss = act.value("enemyLoss", 0);
                    if ((own_loss == 0 || enemy_loss == 0) && getTarget()) {
                        double vis  = weather_mod.value("visibilityModifier", 1.0);
                        double art  = weather_mod.value("artilleryModifier", 1.0);
                        auto [o, e] = sub->estimateCasualties(sub->profile.remainingNumOfTroops(), vis, art);
                        own_loss    = own_loss > 0 ? own_loss : o;
                        enemy_loss  = enemy_loss > 0 ? enemy_loss : e;
                    }

                    if (own_loss > 0) {
                        sub->profile.takeDamage(own_loss);
                    }
                    if (enemy_loss > 0 && getTarget()) {
                        getTarget()->profile.takeDamage(enemy_loss);
                    }
                    valid_actions.push_back({
                        { "actionType", act.value("actionType", "Unknown") },
                        { "troopType", sub->profile.troopType },
                        { "deploySubAgent", deploy_subunit },
                        { "speed", act.value("speed", 0) },
                        { "agentName", name },                     
                        { "position", act["position"] },
                        { "ownLoss", own_loss },
                        { "enemyLoss", enemy_loss },
                        { "inTunnel", act.value("inTunnel", false) },
                        { "remarks", act.value("remarks", "") }
                    });                    
                }
            }
        }       

        // === 11. HANDLE SubAgentsRecall ===
        nlohmann::json valid_recall = nlohmann::json::array();
        for (const auto & r : recall_list) {
            if (r.is_string()) {
                std::string name = r.get<std::string>();
                if (findChildByName(name)) {
                    BranchStreamlining(name);
                    valid_recall.push_back(name);
                    simulation->logger.info(profile.roundNb) << "Agent " << profile.name << ": RECALLED " << name;
                }
            }
        }

        // === 12. UPDATE result ===
        result["agentNextActionType"] = new_action;
        result["agentStage"]          = new_stage;
        result["currentBattlefieldSituation"] = profile.currentBattlefieldSituation;
        result["agentNextPosition"]   = { next_x, next_y };
        result["targetedAgentName"]   = targeted_agent;
        result["agentMoral"]          = moral_str;
        result["speed"]               = speed;
        result["inTunnel"]            = in_tunnel;
        result["weather_modifier"]    = weather_mod;       
        result["SubAgentsRecall"]     = valid_recall;
        result["mainOwnLoss"]         = main_own_loss;
        result["mainEnemyLoss"]       = main_enemy_loss;
        result["actions"]             = valid_actions;
        result["remarks"]             = llm_json.value("remarks", "Action executed.");

        // === 13. UPDATE profile ===
        profile.position.x    = next_x;
        profile.position.y    = next_y;
        profile.currentAction = new_action;
        profile.speed         = speed;
        profile.currentStage  = new_stage + " " + llm_json.value("remarks", "Action executed.");
               // === 14. SAVE history ===
        history.push_back(result);
        if (history.size() > 10) {
            history.erase(history.begin());
        }

        simulation->logger.info(profile.roundNb)
            << "Agent " << profile.name << ": '" << new_action << "' | Stage: " << new_stage << " | Pos: [" << next_x
            << "," << next_y << "]"
            << " | MainLoss: " << main_own_loss << " | Subs: " << valid_actions.size();

    } catch (const std::exception & e) {
        simulation->logger.error(profile.roundNb) << "Agent " << profile.name << ": " << e.what();
        result["remarks"] = "Error: " + std::string(e.what());
        history.push_back(result);
    }

    return result;
}

Agent* Agent::spawnSubAgent(const nlohmann::json& action) {
    const std::vector<std::string> required_fields = { "agentName", "troopType", "deployedNum", "position" };
    for (const auto& field : required_fields) {
        if (!action.contains(field)) {
            simulation->logger.error(profile.roundNb) << "Missing field '" << field << "' in sub-agent action for agent " << profile.name;
            return nullptr;
        }
    }

    std::string agentName = action["agentName"].get<std::string>();
    std::string initAction = action["actionType"].get<std::string>();
    std::string troop_type = action["troopType"].get<std::string>();
    std::string remarks    = action["remarks"].get<std::string>();
  
    int deployed_num = action["deployedNum"].get<int>();
    std::vector<double> position = action["position"].get<std::vector<double>>();

    if (deployed_num > profile.remainingNumOfTroops()) {
        simulation->logger.warn(profile.roundNb) << "Insufficient troops to deploy sub-agent for " << profile.name;
        return nullptr;
    }

    if (!simulation->field.isValidPosition(position[0], position[1])) {
        simulation->logger.warn(profile.roundNb) << "Invalid position [" << position[0] << ", " << position[1] << "] for sub-agent of " << profile.name;
        return nullptr;
    }

    Profile sub_profile(nlohmann::json{
        {"name", agentName},
        {"troopType", troop_type},
        {"initial_troops", deployed_num},
        {"initial_position", action["position"]},
        {"speed",            action["speed"] },
        {"moral", std::to_string(int(profile.moral))},
        {"currentAction", initAction + " " + remarks},
        {"initialMission",initAction},
        {"ammo", profile.ammo},
        {"equipment", profile.equipment},
        {"tactics", profile.tactics},
        {"historySetting", profile.historySetting},
        {"armySetting", profile.armySetting},
        {"roleSetting", profile.roleSetting},
        {"troopInformation", profile.troopInformation},
        {"actionList", profile.actionList},
        {"actionPropertyDefinition", profile.actionPropertyDefinition},
        {"stagePropertyDefinition", profile.stagePropertyDefinition},
        {"actionInstructionBlock", profile.actionInstructionBlock},
        {"jsonConstraintVariable", profile.jsonConstraintVariable},
        {"currentBattlefieldSituation", profile.currentBattlefieldSituation}
        });

    Agent* sub_agent = new Agent(sub_profile, simulation);
    sub_agent->setParent(this);
    profile.deployedNumOfTroops += deployed_num;
    simulation->addAgent(sub_agent);
    simulation->logger.info(profile.roundNb) << "Agent " << profile.name << " created sub-agent " << sub_profile.name
        << " with troops: " << deployed_num << ", position: [" << position[0] << ", " << position[1] << "]";

    return sub_agent;
}

nlohmann::json Agent::BranchStreamlining(const std::string& sub_agent_name) {
    nlohmann::json result = {
        {"status", "Failed"},
        {"message", "Agent not found or operation failed."}
    };

    Agent* child = findChildByName(sub_agent_name);

    if (child == nullptr || child->mergedOrPruned) {
        simulation->logger.warn(profile.roundNb) << "BranchStreamlining failed: Sub-agent " << sub_agent_name << " not found or already processed";
        result["message"] = "Sub-agent " + sub_agent_name + " not found or already processed.";
        return result;
    }

    std::string streamlining;
    if (child->profile.moral == Moral::Low) {
        streamlining = "Prune";
    }
    else {
        streamlining = "Merge";
    }

    int children_relocated = 0;
    for (Agent* sub_agent : child->getChildren()) {
        sub_agent->setParent(this);
        children_relocated++;
    }
    if (streamlining == "Merge") {
        this->profile.deployedNumOfTroops -= child->profile.initialNumOfTroops;
        this->profile.lostNumOfTroops += child->profile.lostNumOfTroops;
    }
    else if (streamlining == "Prune") {
        this->profile.deployedNumOfTroops -= child->profile.initialNumOfTroops;
        this->profile.lostNumOfTroops += child->profile.initialNumOfTroops;
        child->profile.currentStage = "Crushing Defeat";
    }

    child->mergedOrPruned = true;
    child->parent = nullptr;

    simulation->removeAgent(child);

    result["status"] = "Success";
    result["message"] = "Sub-agent " + sub_agent_name + " was " + streamlining + "d. " + std::to_string(children_relocated) + " children relocated.";
    result["action"] = streamlining;
    result["subAgentName"] = sub_agent_name;
    simulation->logger.info(profile.roundNb) << "Sub-agent " << sub_agent_name << " was " << streamlining << "d with " << children_relocated << " children relocated";
    return result;
}

nlohmann::json Agent::generateTargetSummary() {
    nlohmann::json summary;
    summary["agentName"] = profile.name;
    summary["target"] = nlohmann::json::object();

    if (!profile.targetedAgentName.empty()) {
        if (simulation->field.isValidPosition(profile.targetPosition.x, profile.targetPosition.y)) {
            summary["target"]["agentName"] = profile.targetedAgentName;
            summary["target"]["position"] = { profile.targetPosition.x, profile.targetPosition.y };
        }
        else {
            summary["target"] = "Invalid target position";
            simulation->logger.warn(profile.roundNb) << "Invalid target position [" << profile.targetPosition.x << ", " << profile.targetPosition.y << "] for agent " << profile.name;
        }
    }
    else {
        summary["target"] = "No target assigned";
    }

    simulation->logger.debug(profile.roundNb) << "Agent " << profile.name << " TargetSummary: " << summary.dump(2);
    return summary;
}

std::vector<SoldierAgent*> Agent::getSoldiers() {
    SoldierCollector* collector = nullptr;

    if (isCountryA()) {
        collector = simulation->soldierCollectorA;
    }
    else {
        collector = simulation->soldierCollectorB;
    }

    if (!collector) return {};

    return collector->getSoldiers(this);
}

std::string Agent::generateSoldierSummary() {
    std::vector<SoldierAgent *> my_soldiers = getSoldiers();

    if (my_soldiers.empty()) {
        return "";  // ✅ Trả về rỗng thay vì "No soldiers"
    }

    nlohmann::json soldier_data = nlohmann::json::array();
    for (SoldierAgent * soldier : my_soldiers) {
        soldier_data.push_back(soldier->toJson());
    }

    std::vector<nlohmann::json> prompts;
    std::stringstream           prompt_system;

    if (!simulation->config.contains("soldier_summary_config")) {
        return "";  // ✅ Không có config thì skip
    }

    nlohmann::json soldier_summary_config = simulation->config["soldier_summary_config"];

    prompt_system << "### SYSTEM INSTRUCTION: INTELLIGENCE OFFICER ROLE ###\n";
    prompt_system << "You are a highly experienced Intelligence Officer. ";
    prompt_system << "Analyze the 'RAW SOLDIER DATA' and 'SITUATION' to produce ONE consolidated tactical report.\n";
    prompt_system << "\n--- JSON OUTPUT CONSTRAINT ---\n";

    if (soldier_summary_config.contains("soldier_summary_schema")) {
        prompt_system << "Output Schema: " << soldier_summary_config["soldier_summary_schema"].dump() << "\n";
    }

    if (soldier_summary_config.contains("analysis_guidance")) {
        prompt_system << "Analysis Guidance: " << soldier_summary_config["analysis_guidance"].dump() << "\n";
    }

    prompt_system << "Output pure JSON only. No explanation.\n";

    prompts.push_back({
        {"role",     "system"           },
        { "content", prompt_system.str()}
    });

    std::stringstream user_prompt_content;
    user_prompt_content << "[RAW SOLDIER DATA]\n";
    user_prompt_content << soldier_data.dump(2) << "\n";
    user_prompt_content << "\n--- CURRENT SITUATION ---\n";
    user_prompt_content << "Action: " << profile.currentAction << "\n";
    user_prompt_content << "Battlefield: " << simulation->field.generateBattlefieldSituation(this) << "\n";
    user_prompt_content << "\n[JSON REPORT START]\n";

    prompts.push_back({
        {"role",     "user"                   },
        { "content", user_prompt_content.str()}
    });

    try {
        std::string    soldier_report_json = simulation->llm->infer(prompts);
        nlohmann::json report              = nlohmann::json::parse(soldier_report_json);

        std::stringstream formatted_summary;
        formatted_summary << "=== UNIT MORALE & INTELLIGENCE SUMMARY ===\n";
        formatted_summary << "MORALE: " << report.value("agent_morale_assessment", "N/A") << "\n";
        formatted_summary << "OBSERVATIONS: " << report.value("key_observations_summary", "None") << "\n";
        formatted_summary << "SENTIMENT: " << report.value("overall_sentiment_summary", "Stable") << "\n";

        if (report.contains("physical_condition_trend")) {
            formatted_summary << "PHYSICAL: " << report["physical_condition_trend"].get<std::string>() << "\n";
        }

        if (report.contains("combat_effectiveness_estimate")) {
            double effectiveness = report["combat_effectiveness_estimate"].get<double>();
            formatted_summary << "EFFECTIVENESS: " << static_cast<int>(effectiveness * 100) << "%\n";
        }

        formatted_summary << "==========================================\n";

        return formatted_summary.str();

    } catch (const std::exception & e) {
        simulation->logger.error(profile.roundNb)
            << "Failed to generate soldier summary for " << profile.name << ": " << e.what();
        return "";  // ✅ Fail silent, không crash
    }
}

std::pair<int, int> Agent::estimateCasualties(int deployedNum, double visibilityModifier, double artilleryModifier) {
    // ========================================
    // 1. VALIDATION - Kiểm tra đầu vào
    // ========================================
    int attacker_avail = profile.remainingNumOfTroops();
    deployedNum        = std::min(deployedNum, attacker_avail);

    if (deployedNum <= 0 || !target) {
        simulation->logger.warn(profile.roundNb) << "CasualtyCalc: " << profile.name << ": no valid target or troops";
        return { 0, 0 };
    }

    Agent * defender = getTarget();
    if (!defender) {
        simulation->logger.warn(profile.roundNb) << "CasualtyCalc: " << profile.name << ": target is null";
        return { 0, 0 };
    }

    int defender_avail = defender->profile.remainingNumOfTroops();
    if (defender_avail <= 0) {
        simulation->logger.warn(profile.roundNb) << "CasualtyCalc: " << profile.name << ": target already destroyed";
        return { 0, 0 };
    }

    // ========================================
    // 2. LẤY HỆ SỐ TỪ CONFIG
    // ========================================
    double base_coeff = simulation->config["battle_config"].value("casualty_coeff", 0.08);

    // ========================================
    // 3. TÍNH ATTACK EFFECTIVENESS
    // ========================================
    double att_eff = 1.0;

    // Tactics bonus
    if (profile.tactics.count("assault")) {
        att_eff += profile.tactics.at("assault") * 0.5;
    }
    if (profile.tactics.count("stealth")) {
        att_eff += profile.tactics.at("stealth") * 0.3;
    }
    if (profile.tactics.count("attack")) {
        att_eff += profile.tactics.at("attack");
    }

    // Troop type bonus
    double troop_bonus = 1.0;
    if (profile.troopType == "artillery") {
        troop_bonus = 1.3;
    } else if (profile.troopType == "scout") {
        troop_bonus = 0.8;
    }
    att_eff *= troop_bonus;

    // ========================================
    // 4. TÍNH DEFENSE EFFECTIVENESS
    // ========================================
    double def_eff = 1.0;

    // Defender tactics
    if (defender->profile.tactics.count("defense")) {
        def_eff += defender->profile.tactics.at("defense") * 0.6;
    }

    // Terrain defense bonus
    TerrainObject * def_terrain =
        simulation->field.getTerrainObject(defender->profile.position.x, defender->profile.position.y);
    if (def_terrain && def_terrain->defense_bonus > 0) {
        def_eff += def_terrain->defense_bonus / 20.0;
    }

    // ========================================
    // 5. MORALE MULTIPLIERS
    // ========================================
    double att_morale = 1.0;
    switch (profile.moral) {
        case Moral::High:
            att_morale = 1.2;
            break;
        case Moral::Medium:
            att_morale = 1.0;
            break;
        case Moral::Low:
            att_morale = 0.8;
            break;
    }

    double def_morale = 1.0;
    switch (defender->profile.moral) {
        case Moral::High:
            def_morale = 1.2;
            break;
        case Moral::Medium:
            def_morale = 1.0;
            break;
        case Moral::Low:
            def_morale = 0.8;
            break;
    }

    // ========================================
    // 6. TACTICAL EVALUATION (Địa hình)
    // ========================================
    double att_tactical = simulation->field.evaluateTacticalUse(this);
    double def_tactical = simulation->field.evaluateTacticalUse(defender);

    // ========================================
    // 7. TÍNH ATTACK POWER & DEFENSE POWER
    // ========================================
    double att_power = deployedNum * att_eff * att_morale * att_tactical;
    double def_power = defender_avail * def_eff * def_morale * def_tactical;

    if (att_power <= 0 || def_power <= 0) {
        simulation->logger.warn(profile.roundNb) << "CasualtyCalc: " << profile.name << ": invalid power calculation";
        return { 0, 0 };
    }

    // ========================================
    // 8. TÍNH COMBAT RATIO
    // ========================================
    double ratio = att_power / (att_power + def_power);
    ratio        = std::clamp(ratio, 0.05, 0.95);

    // ========================================
    // 9. TERRAIN MODIFIERS
    // ========================================
    double terrain_mod_attacker = 1.0;
    double terrain_mod_defender = 1.0;

    // Attacker attacking into stronghold = harder
    if (def_terrain && def_terrain->type == TerrainType::Stronghold) {
        terrain_mod_attacker = 1.4;   // Attacker takes more casualties
        terrain_mod_defender = 0.65;  // Defender takes less casualties
    } else if (def_terrain) {
        // Other terrain types
        switch (def_terrain->type) {
            case TerrainType::Hills:
                terrain_mod_attacker = 1.2;
                terrain_mod_defender = 0.8;
                break;
            case TerrainType::Forest:
                terrain_mod_attacker = 1.15;
                terrain_mod_defender = 0.85;
                break;
            case TerrainType::Flat:
                terrain_mod_attacker = 1.3;
                terrain_mod_defender = 1.0;
                break;
            default:
                break;
        }
    }

    // ========================================
    // 10. TUNNEL PROTECTION (50% reduction)
    // ========================================
    double tunnel_mod_attacker = 1.0;
    double tunnel_mod_defender = 1.0;

    if (simulation->field.isInTunnel(this, 0.0)) {
        tunnel_mod_attacker = 0.50;  // 50% casualty reduction
        simulation->logger.info(profile.roundNb) << "Agent " << profile.name << " in tunnel: 50% casualty reduction";
    }

    if (simulation->field.isInTunnel(defender, 0.0)) {
        tunnel_mod_defender = 0.50;  // 50% casualty reduction
        simulation->logger.info(profile.roundNb)
            << "Agent " << defender->profile.name << " in tunnel: 50% casualty reduction";
    }

    // ========================================
    // 11. ACTION MODIFIERS
    // ========================================
    double action_mod = 1.0;
    if (profile.currentAction == "Launch Full Assault") {
        action_mod = 1.50;
    } else if (profile.currentAction == "Launch Night Assault") {
        action_mod = 1.35;
    } else if (profile.currentAction == "Human Wave Assault") {
        action_mod = 1.80;
    } else if (profile.currentAction == "Hold Position") {
        action_mod = 0.70;
    } else if (profile.currentAction == "Fortify Position") {
        action_mod = 0.65;
    } else if (profile.currentAction == "Dig Assault Tunnel") {
        action_mod = 0.80;
    } else if (profile.currentAction == "Move to Tunnel") {
        action_mod = 0.75;
    }

    // ========================================
    // 12. ARTILLERY DOMINANCE (Vietnamese)
    // ========================================
    double artillery_advantage = artilleryModifier;

    if (isCountryA() && simulation->config["battle_config"].contains("artillery_dominance_vn")) {
        artillery_advantage *= simulation->config["battle_config"]["artillery_dominance_vn"].get<double>();
        simulation->logger.debug(profile.roundNb) << "Vietnamese artillery advantage: " << artillery_advantage;
    }

    // French artillery degrades over time
    if (!isCountryA() && profile.roundNb >= 20) {
        artillery_advantage *= 0.3;  // Reduced to 30% effectiveness after Round 20
        simulation->logger.debug(profile.roundNb) << "French artillery degraded: " << artillery_advantage;
    }

    // ========================================
    // 13. FINAL CASUALTY CALCULATION
    // ========================================
    // Own Loss Formula: troops × coeff × (1-ratio) × terrain × visibility × tunnel × morale × action
    int est_own_loss =
        static_cast<int>(deployedNum * base_coeff * (1.0 - ratio) * terrain_mod_attacker * visibilityModifier *
                         tunnel_mod_attacker * (1.0 / att_morale) *  // Lower morale = more casualties
                         action_mod);

    // Enemy Loss Formula: enemy_troops × coeff × ratio × terrain × artillery × morale × action
    int est_enemy_loss = static_cast<int>(defender_avail * base_coeff * ratio * terrain_mod_defender *
                                          artillery_advantage * (1.0 / def_morale) *  // Lower morale = more casualties
                                          action_mod);

    // ========================================
    // 14. CLAMP VALUES
    // ========================================
    est_own_loss   = std::clamp(est_own_loss, 0, attacker_avail);
    est_enemy_loss = std::clamp(est_enemy_loss, 0, defender_avail);

    // ========================================
    // 15. HISTORICAL ACCURACY CHECK
    // ========================================
    // VN average ~410/round, French ~41/round
    // Major battles: 5-10× higher
    if (est_own_loss > deployedNum * 0.5) {
        simulation->logger.warn(profile.roundNb)
            << "WARNING: " << profile.name << " casualties very high: " << est_own_loss << " ("
            << (est_own_loss * 100 / deployedNum) << "%)";
    }

    if (est_enemy_loss > defender_avail * 0.5) {
        simulation->logger.warn(profile.roundNb)
            << "WARNING: " << defender->profile.name << " casualties very high: " << est_enemy_loss << " ("
            << (est_enemy_loss * 100 / defender_avail) << "%)";
    }

    // ========================================
    // 16. DETAILED LOGGING
    // ========================================
    simulation->logger.debug(profile.roundNb)
        << "\n=== CASUALTY CALCULATION: " << profile.name << " vs " << defender->profile.name << " ==="
        << "\nDeployed: " << deployedNum << " vs Defender: " << defender_avail << "\nAttack Eff: " << att_eff
        << " | Defense Eff: " << def_eff << "\nAttack Power: " << att_power << " | Defense Power: " << def_power
        << "\nCombat Ratio: " << ratio << "\nModifiers:"
        << "\n  - Visibility: " << visibilityModifier << "\n  - Artillery: " << artillery_advantage
        << "\n  - Tunnel (Att): " << tunnel_mod_attacker << "\n  - Tunnel (Def): " << tunnel_mod_defender
        << "\n  - Terrain (Att): " << terrain_mod_attacker << "\n  - Terrain (Def): " << terrain_mod_defender
        << "\n  - Action: " << action_mod << "\nResult: Own Loss = " << est_own_loss
        << " | Enemy Loss = " << est_enemy_loss
        << "\n====================================================================";

    // ========================================
    // 17. APPLY CASUALTIES (REMOVED - Let caller handle)
    // ========================================
    // NOTE: Không apply damage ở đây nữa, để caller (execute()) xử lý
    // Tránh double-counting casualties

    return { est_own_loss, est_enemy_loss };
}

nlohmann::json Agent::getProfilePrompt() const {
    nlohmann::json p;
    p["Name"]     = profile.name;
    p["Position"] = { profile.position.x, profile.position.y };
    p["speed"]    = profile.speed;
    p["Troops"]   = profile.remainingNumOfTroops();
    p["Morale"]   = profile.getMoralString();

    // ✅ FIX: Kiểm tra thực tế có trong tunnel không
    p["In Tunnel"] = simulation->field.isInTunnel(const_cast<Agent *>(this), 0.0, profile.roundNb);

    p["Encircled"] = profile.encircled;
    p["Target"]    = profile.targetedAgentName;

    double distanceToTarget = -1;
    if (target) {
        distanceToTarget = profile.getDistanceTo(target->profile.position);
    }

    p["Distance to Target"] = distanceToTarget;
    p["Last Action"]        = profile.currentAction + " (Round " + std::to_string(profile.roundNb - 1) + ")";
    p["Tactics"]            = profile.tactics;

    return p;
}
