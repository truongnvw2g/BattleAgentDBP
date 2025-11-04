#include "Simulation.h"

#include "Agent.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

Logger::Logger(const std::string& filename, bool log_to_console, LogLevel min_level)
    : log_to_console_(log_to_console), min_level_(min_level) {
    file_.open(filename, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "❌ Cannot open log file: " << filename << std::endl;
    }
}

Logger::~Logger() {
    if (file_.is_open()) {
        file_.close();
    }
}

Logger::LogStream::LogStream(Logger& logger, LogLevel level, int turn)
    : logger_(logger), level_(level), turn_(turn) {}

Logger::LogStream::~LogStream() {
    logger_.log(level_, stream_.str(), turn_);
}

void Logger::log(LogLevel level, const std::string& message, int turn) {
    if (level < min_level_) return;

    // Tạo timestamp
    std::time_t now = std::time(nullptr);
    char time_str[20];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    // Tạo chuỗi log
    std::ostringstream log_line;
    log_line << "[" << time_str << "][";
    switch (level) {
    case LogLevel::INFO:  log_line << "INFO"; break;
    case LogLevel::DEBUG: log_line << "DEBUG"; break;
    case LogLevel::WARN:  log_line << "WARN"; break;
    case LogLevel::ERROR: log_line << "ERROR"; break;
    }
    log_line << "]";
    if (turn >= 0) {
        log_line << "[Turn " << turn << "]";
    }
    log_line << " " << message;

    // Ghi vào file
    if (file_.is_open()) {
        file_ << log_line.str() << std::endl;
    }

    // Ghi ra console nếu bật
    if (log_to_console_) {
        if (level == LogLevel::ERROR) {
            std::cerr << log_line.str() << std::endl;
        }
        else {
            std::cout << log_line.str() << std::endl;
        }
    }
}
Simulation::Simulation(const nlohmann::json & config) :
    field(2000, 2000),
    config(config),
    unique_id_counter(0),
    llm(NULL),
    logger("simulation_log.txt", true, LogLevel::INFO) {

    model_path = config["lla_modle_path"].get<std::string>();
    // Khởi tạo địa hình
    if (config.contains("terrain_config")) {
       
        const auto & terrain_config = config["terrain_config"];
        if (config.contains("terrain_config")) {
          
            const auto & terrain_config = config["terrain_config"];
            field.width                 = terrain_config.value("width",2000);
            field.height                 = terrain_config.value("height", 2000);    
            if (terrain_config.contains("terrains")) {
                for (const auto & terrain : terrain_config["terrains"]) {
                    std::string type_str         = terrain["type"].get<std::string>();
                    auto        pos              = terrain["position"].get<std::vector<double>>();
                    double      speed_multiplier = terrain.value("speed_multiplier", 1.0);  // Giá trị mặc định
                    int         health_bonus     = terrain.value("health_bonus", 0);        // Giá trị mặc định
                    int         loss_penalty     = terrain.value("loss_penalty", 0);        // Giá trị mặc định                   
                    TerrainType type;
                    if (type_str == "Flat") {
                        type = TerrainType::Flat;
                    } else if (type_str == "Hills") {
                        type = TerrainType::Hills;
                    } else if (type_str == "Valley") {
                        type = TerrainType::Valley;
                    } else if (type_str == "River") {
                        type = TerrainType::River;
                    } else if (type_str == "Forest") {
                        type = TerrainType::Forest;
                    } else if (type_str == "Stronghold") {
                        type = TerrainType::Stronghold;
                    } else if (type_str == "Airfield") {
                        type = TerrainType::Airfield;
                    } else {
                        logger.warn() << "Unknown terrain type: " << type_str << "\n";
                        continue;
                    }

                    field.setTerrain(pos[0], pos[1], type, speed_multiplier, health_bonus, loss_penalty);

                    if (terrain.contains("name")) {
                        std::string name            = terrain["name"].get<std::string>();
                        int         defense_bonus   = terrain.value("defense_bonus", 0);
                        int         artillery_range = terrain.value("artillery_range", 0);
                        int         power           = terrain.value("power", 100);
                        field.setTerrainName(pos[0], pos[1], name, defense_bonus, artillery_range, power);
                    }
                }
            }
        }

        if (terrain_config.contains("tunnels")) {
          
            for (const auto & tunnel : terrain_config["tunnels"]) {
                auto        start            = tunnel["start"].get<std::vector<double>>();
                auto        end              = tunnel["end"].get<std::vector<double>>();
                std::string name             = tunnel.value("name", "Unnamed Tunnel");
                double      speed_multiplier = tunnel.value("speed_multiplier", 0.5);
                int         defense_bonus    = tunnel.value("defense_bonus", 20);
                int         capacity         = tunnel.value("capacity", 1000);
                double      stealth_bonus    = tunnel.value("stealth_bonus", 0.0);

                int construction_start    = tunnel.value("construction_start", 0);
                int construction_complete = tunnel.value("construction_complete", 0);

                field.addTunnel({ start[0], start[1] }, { end[0], end[1] }, name, speed_multiplier, defense_bonus,
                                capacity, stealth_bonus, construction_start, construction_complete);
                logger.info() << "Added tunnel: " << name << " from [" << start[0] << ", " << start[1] << "] to [" << end[0]
                       << ", " << end[1] << "] with stealth_bonus=" << stealth_bonus << "\n";
            }
        }

        if (terrain_config.contains("weather")) {
            for (const auto & weather : terrain_config["weather"]) {
              
                if (weather["turn"].get<int>() == 1) {
                    field.setWeather(weather["type"].get<std::string>(), weather["visibilityModifier"].get<double>(),
                                     weather.value("artilleryModifier", 1.0));
                    logger.info() << "Applied initial weather: " << weather["type"].get<std::string>() << "\n";
                }
            }
        }
    }

    // Khởi tạo countryA (Vietnamese)
    nlohmann::json viet_config              = config["red_configs"];   
    viet_config["actionList"]               = config["actionList"].dump();
    viet_config["actionPropertyDefinition"] = config["actionPropertyDefinition"].dump();
    viet_config["stagePropertyDefinition"]  = config["stagePropertyDefinition"].dump();
    viet_config["actionInstructionBlock"]   = config["actionInstructionBlock"];
    viet_config["jsonConstraintVariable"]   = config["jsonConstraintVariable"].dump();

    Profile viet(viet_config);
    viet.updateTroopInformation();
    countryA = new Agent(viet, this);
    agents.push_back(countryA);

    if (config["red_configs"].contains("individual_profiles")) {
        soldierCollectorA = new SoldierCollector(config["red_configs"]["individual_profiles"]);
        logger.info() << "Initialized SoldierCollector for Vietnamese (A) with "
            << soldierCollectorA->getNumAvailableSoldiers() << " available soldiers.\n";
    }
    else {
        // Xử lý trường hợp không tìm thấy profile (ví dụ: khởi tạo rỗng)
        soldierCollectorA = new SoldierCollector(nlohmann::json::object());
        logger.warn() << "Warning: 'individual_profiles' not found for Vietnamese (A).\n";
    }
    if (soldierCollectorA && soldierCollectorA->getNumAvailableSoldiers() > 0) {
        std::vector<SoldierAgent*> chosen_soldiers = soldierCollectorA->getRandomAvailableSoldiers(10);

        if (!chosen_soldiers.empty()) {
            for (SoldierAgent* soldier : chosen_soldiers) {

                soldierCollectorA->deploySoldier(soldier, countryA);
            }
            logger.info() << "Agent " << countryA->profile.name << " assigned " << chosen_soldiers.size() << " soldier profiles.\n";
        }
    }
    // Khởi tạo countryB (French)
    nlohmann::json french_config              = config["green_configs"];   
    french_config["actionList"]               = config["actionList"].dump();
    french_config["actionPropertyDefinition"] = config["actionPropertyDefinition"].dump();
    french_config["stagePropertyDefinition"]  = config["stagePropertyDefinition"].dump();
    french_config["actionInstructionBlock"]   = config["actionInstructionBlock"];
    french_config["jsonConstraintVariable"] = config["jsonConstraintVariable"].dump();  
    Profile french(french_config);
    french.updateTroopInformation();
    countryB = new Agent(french, this);
    agents.push_back(countryB);   

    // Khởi tạo SoldierCollector cho Phe B (French)
    if (config["green_configs"].contains("individual_profiles")) {
        soldierCollectorB = new SoldierCollector(config["green_configs"]["individual_profiles"]);
        logger.info() << "Initialized SoldierCollector for French (B) with "
            << soldierCollectorB->getNumAvailableSoldiers() << " available soldiers.\n";
    }
    else {
        // Xử lý trường hợp không tìm thấy profile (ví dụ: khởi tạo rỗng)
        soldierCollectorB = new SoldierCollector(nlohmann::json::object());
        logger.info() << "Warning: 'individual_profiles' not found for French (B).\n";
    }
    if (soldierCollectorB && soldierCollectorB->getNumAvailableSoldiers() > 0) {
        std::vector<SoldierAgent*> chosen_soldiers = soldierCollectorB->getRandomAvailableSoldiers(10);
        if (!chosen_soldiers.empty()) {
            for (SoldierAgent* soldier : chosen_soldiers) {

                soldierCollectorB->deploySoldier(soldier, countryB);
            }
            logger.info() << "Agent " << countryB->profile.name << " assigned " << chosen_soldiers.size() << " soldier profiles.\n";
        }
    }

    // Khởi tạo sub-agents
    for (const auto & side : { "red_configs", "green_configs" }) {
        if (config[side].contains("sub_agents")) {
            for (auto sub_agent : config[side]["sub_agents"]) {
                nlohmann::json sub_config = sub_agent;              
                sub_config["commander"] =
                    sub_agent.value("commander", side == "red_configs" ? "Vietnamese_Sub" : "French_Sub");               
                
                sub_config["currentBattlefieldSituation"] = config[side].value("currentBattlefieldSituation", "{}");
                sub_config["System_Setting"]              = config[side].value("System_Setting", "");
                sub_config["historySetting"]              = config[side].value("historySetting", "");
                sub_config["AmySetting"]                  = config[side].value("AmySetting", "");
                sub_config["roleSetting"]                 = config[side].value("roleSetting", "");
                sub_config["troopInformation"]            = config[side].value("troopInformation", "");
                sub_config["actionList"]                  = config["actionList"].dump();
                sub_config["actionPropertyDefinition"]    = config["actionPropertyDefinition"].dump();
                sub_config["stagePropertyDefinition"]     = config["stagePropertyDefinition"].dump();
                sub_config["actionInstructionBlock"]      = config["actionInstructionBlock"];
                sub_config["jsonConstraintVariable"]      = config["jsonConstraintVariable"].dump();
                Agent * sub                               = new Agent(Profile(sub_config), this);
                sub->setParent(side == "red_configs" ? countryA : countryB);
                sub->setTarget(side == "red_configs" ? countryB : countryA);
                sub->profile.updateTroopInformation();
                agents.push_back(sub);
                logger.info() << "Add sub-Agent " << sub->profile.name  << ".\n";
            }
        }
    }

    countryA->setTarget(countryB);
    for (auto agent : countryA->getChildren()) {
        agent->setTarget(countryA->getTarget());
    }
    countryB->setTarget(countryA);
    for (auto agent : countryB->getChildren()) {
        agent->setTarget(countryB->getTarget());
    }
   
    // Cập nhật currentBattlefieldSituation cho tất cả agents
    for (auto * agent : agents) {
        if (!agent->profile.targetedAgentName.empty()) {
            for (auto * orther : agents) {
                if (agent->profile.targetedAgentName == orther->profile.name) {
                    agent->setTarget(orther);
                }

            }
        }
        
    }
    if (!llm) {
        llm = std::make_shared<LLMInference>(model_path, 99, 8192);
    }
    
    std::time_t now = std::time(nullptr);
    char time_str[20];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    logger.info() << "[Simulation] Starting at " << time_str;

    //initializeLLMContext();
}

Simulation::~Simulation() {
   
    delete countryA;

    delete countryB;
  
}

bool Simulation::addAgent(Agent * child) {
    return insertAgent(agents.size(), child);
}

bool Simulation::insertAgent(unsigned int index, Agent * child) {
    if (!child) {
        return false;
    }
    if (index >= agents.size()) {
        index = agents.size();
        agents.push_back(child);
    } else {
        agents.insert(agents.begin() + index, child);
    }
    return true;
}

unsigned int Simulation::getNumAgents() const {
    return static_cast<unsigned int>(agents.size());
}

bool Simulation::removeAgent(Agent * child) {
    unsigned int pos = getAgentIndex(child);
    if (pos < agents.size()) {
        return removeAgent(pos, 1);
    }
    return false;
}

bool Simulation::removeAgents(unsigned int pos, unsigned int numChildrenToRemove) {
    if (pos >= agents.size() || numChildrenToRemove == 0) return false;

    unsigned int endOfRemoveRange = std::min(pos + numChildrenToRemove, static_cast<unsigned int>(agents.size()));

    // ✅ Ghi log trước khi xóa
    for (unsigned int i = pos; i < endOfRemoveRange; ++i) {
        Agent* agent = agents[i];
        if (agent) {
            logger.info() << "[Simulation] Removing agent: " << agent->profile.name
                << " (faction=" << agent->getFaction()
                << ", troops=" << agent->profile.initialNumOfTroops << ")\n";
            delete agent;  // ✅ Giải phóng bộ nhớ thật
            agents[i] = nullptr; // tránh dùng nhầm
        }
    }

    // ✅ Erase con trỏ null khỏi vector
    agents.erase(agents.begin() + pos, agents.begin() + endOfRemoveRange);
    return true;
}

bool Simulation::replaceAgent(Agent * origAgent, Agent * newAgent) {
    if (newAgent == nullptr || origAgent == newAgent) {
        return false;
    }
    unsigned int pos = getAgentIndex(origAgent);
    if (pos < agents.size()) {
        return setAgent(pos, newAgent);
    }
    return false;
}

bool Simulation::setAgent(unsigned int i, Agent * newAgent) {
    if (i < agents.size() && newAgent) {
        agents[i]     = newAgent;
        return true;
    }
    return false;
}

Agent* Simulation::findAgent(const std::string& name)
{
    for (Agent * agent : agents) {
        if (agent->profile.name == name) {
            return agent;
        }
    }
    return NULL;
}

int Simulation::generateUniqueId() {
    return unique_id_counter++;
}

bool Simulation::isTerrainObjectCaptured(const TerrainObject & obj) {
    for (auto * agent : agents) {
        if (!agent->mergedOrPruned && agent->profile.currentStage != "Crushing Defeat") {
            if (std::abs(agent->profile.position.x - obj.position.x) < 50 &&
                std::abs(agent->profile.position.y - obj.position.y) < 50) {

                return true;
            }
        }
    }
    return false;
}

bool Simulation::isTerrainObjectEncircled(const TerrainObject& obj) {
    int encircling_units = 0;

    // Giả định đối tượng địa hình thuộc countryB (French) để kiểm tra bao vây bởi countryA (Vietnamese)
    for (auto* agent : agents) {
        // Chỉ xem xét agent bộ binh, không bị hợp nhất, và không ở trạng thái thất bại
        if (agent->profile.troopType == "infantry" && !agent->mergedOrPruned &&
            agent->profile.currentStage != "Crushing Defeat" &&
            agent->profile.currentStage != "fleeing Off the Map") {
            
            // Xác định phe của agent
            bool is_country_a = (countryA && (agent == countryA || agent->getParent() == countryA || 
                               agent->profile.name.find("Vietnamese") != std::string::npos));
            
            // Chỉ tính agent từ countryA (phe đối lập với countryB)
            if (is_country_a) {
                // Tính khoảng cách đến đối tượng địa hình
                double dist = std::sqrt(std::pow(agent->profile.position.x - obj.position.x, 2) +
                                        std::pow(agent->profile.position.y - obj.position.y, 2));
                
                // Kiểm tra xem agent có nhắm vào một agent của countryB gần đối tượng địa hình
                if (dist < 150 && !agent->profile.targetedAgentName.empty()) {
                    Agent* target_agent = nullptr;
                    for (auto* other : agents) {
                        if (other->profile.name == agent->profile.targetedAgentName &&
                            !other->mergedOrPruned &&
                            other->profile.currentStage != "Crushing Defeat" &&
                            other->profile.currentStage != "fleeing Off the Map") {
                            target_agent = other;
                            break;
                        }
                    }
                    
                    // Kiểm tra xem target_agent thuộc countryB và gần đối tượng địa hình
                    if (target_agent) {
                        bool target_is_country_b =
                            (countryB && (target_agent == countryB || target_agent->getParent() == countryB || 
                                                  target_agent->profile.name.find("French") != std::string::npos));
                        if (target_is_country_b) {
                            double target_dist = std::sqrt(std::pow(target_agent->profile.position.x - obj.position.x, 2) +
                                                           std::pow(target_agent->profile.position.y - obj.position.y, 2));
                            if (target_dist < 50) {
                                encircling_units++;
                            }
                        }
                    }
                }
            }
        }
    }

    logger.debug() << "Terrain object " << obj.name << " has " << encircling_units << " encircling units\n";
    return encircling_units >= 2;
}

std::string Simulation::checkSupplyLine(Agent * agent) {
    // Xác định phe của agent
    bool is_country_a = agent->isCountryA();

    // Điểm tiếp tế là vị trí của root agent (countryA hoặc countryB)
    Position supply_point = { 0, 0 };  // Mặc định nếu không có root agent
    if (is_country_a && countryA) {
        supply_point = countryA->profile.position;
    } else if (!is_country_a && countryB) {
        supply_point = countryB->profile.position;
    }

    // Tính khoảng cách từ agent đến điểm tiếp tế
    double dist = std::sqrt(std::pow(agent->profile.position.x - supply_point.x, 2) +
                            std::pow(agent->profile.position.y - supply_point.y, 2));

    // Kiểm tra xem agent mục tiêu có cắt tuyến tiếp tế không
    if (!agent->profile.targetedAgentName.empty()) {
        Agent * target_agent = nullptr;
        for (auto * other : agents) {
            if (other->profile.name == agent->profile.targetedAgentName && !other->mergedOrPruned &&
                other->profile.currentStage != "Crushing Defeat" &&
                other->profile.currentStage != "fleeing Off the Map") {
                target_agent = other;
                break;
            }
        }

        if (target_agent) {
            // Xác định phe của target_agent
            bool target_is_country_a = target_agent->isCountryA();                

            // Đảm bảo target_agent thuộc phe đối phương
            if (is_country_a != target_is_country_a) {
                // Tính khoảng cách từ target_agent đến điểm tiếp tế
                double enemy_dist = std::sqrt(std::pow(target_agent->profile.position.x - supply_point.x, 2) +
                                              std::pow(target_agent->profile.position.y - supply_point.y, 2));

                // Kiểm tra xem target_agent có ở gần tuyến tiếp tế không
                if (enemy_dist < 200) {
                    logger.warn() << "Agent " << agent->profile.name << " supply line severed by "
                           << target_agent->profile.name << " at distance " << static_cast<int>(enemy_dist) << "\n";
                    return "Severed";
                }
            }
        }
    }

    // Nếu không có kẻ thù cắt tuyến, kiểm tra khoảng cách đến điểm tiếp tế
    if (dist < 1000) {
        logger.info() << "Agent " << agent->profile.name
               << " supply line active, distance to supply point: " << static_cast<int>(dist) << "\n";
        return "Active";
    } else {
        logger.warn() << "Agent " << agent->profile.name << " supply line severed due to distance: " << static_cast<int>(dist) << "\n";
        return "Severed";
    }
}

void Simulation::applyMoraleEffect(Agent * agent) {
    if (agent->profile.moral == Moral::Low) {
        agent->profile.takeDamage(50);
        logger.warn() << "Agent " << agent->profile.name << " morale Low, lost 50 troops\n";
        if (!agent->profile.targetedAgentName.empty()) {
            bool near_terrain = false;
            for (const auto & obj : field.getTerrainObjects()) {
                double dist = std::sqrt(std::pow(agent->profile.position.x - obj.position.x, 2) +
                                        std::pow(agent->profile.position.y - obj.position.y, 2));
                if (dist < 100) {
                    near_terrain = true;
                    break;
                }
            }
            if (!near_terrain) {
                agent->profile.takeDamage(25);
                logger.warn() << "Agent " << agent->profile.name
                       << " lost additional 25 troops due to no nearby terrain objects\n";
            }
        }
    } else if (agent->profile.moral == Moral::High) {
        agent->profile.recoverTroops(50);
        logger.info() << "Agent " << agent->profile.name << " morale High, recovered 50 troops\n";
    }
}


void Simulation::applyActionEffects(Agent* agent, double& anti_aircraft_modifier) {
    nlohmann::json last_action;
    last_action["targetedAgentName"] = agent->profile.targetedAgentName;
    last_action["ownPotentialLostNum"] = 0;
    last_action["enemyPotentialLostNum"] = 0;

    auto sync_results = agent->execute();
    for (const auto& result : sync_results) {
        if (result.contains("error")) {
            logger.error() << "Error for Agent " << agent->profile.name << ": " << result["error"].get<std::string>();
        }
        else if (result.contains("action") && result.contains("result")) {
            logger.info() << "Agent " << agent->profile.name << " performed " << result["action"].get<std::string>()
                << " with result: " << result["result"].get<std::string>()
                << (result.contains("sub_agent") ? " for sub-agent " + result["sub_agent"].get<std::string>() : "");
        }
    }

    for (const auto& action : sync_results) {
        if (action.contains("action") && action["action"] == "sub_agent_created") {
            continue;
        }
        if (action.contains("ownPotentialLostNum") && action["ownPotentialLostNum"].is_number_integer()) {
            int own_loss = action["ownPotentialLostNum"].get<int>();
            agent->profile.takeDamage(own_loss);
            logger.warn() << "Agent " << agent->profile.name << " took " << own_loss << " damage from action.";
        }
        if (action.contains("enemyPotentialLostNum") && action["enemyPotentialLostNum"].is_number_integer() &&
            action.contains("agentName") && action["agentName"].is_string()) {
            std::string target_id = action["agentName"].get<std::string>();
            Agent* target_agent = nullptr;
            for (auto* a : agents) {
                if (a->profile.name == target_id && !a->mergedOrPruned &&
                    a->profile.currentStage != "Crushing Defeat" &&
                    a->profile.currentStage != "fleeing Off the Map") {
                    target_agent = a;
                    break;
                }
            }
            if (target_agent) {
                int enemy_loss = action["enemyPotentialLostNum"].get<int>();
                target_agent->profile.takeDamage(enemy_loss);
                logger.info() << "Agent " << agent->profile.name << " dealt " << enemy_loss << " damage to target "
                    << target_agent->profile.name << ".";
            }
        }
    }
}

void Simulation::logState(int turn, const std::string& /*agent_name*/, Agent* agent) {
    logger.info(turn) << "Agent " << agent->profile.name << " (" << agent->profile.troopType
        << ") state: " << agent->profile.currentStage << ", Troops: " << agent->profile.remainingNumOfTroops()
        << ", Position: [" << agent->profile.position.x << "," << agent->profile.position.y << "]";
}

void Simulation::visualizeDeployment(int turn, bool output_to_console) {
    const double grid_size = 100.0;
    int grid_width = static_cast<int>(std::ceil(field.width / grid_size));
    int grid_height = static_cast<int>(std::ceil(field.height / grid_size));
    std::vector<std::vector<std::string>> map(grid_height, std::vector<std::string>(grid_width, "."));
    for (int i = 0; i < grid_height; ++i) {
        for (int j = 0; j < grid_width; ++j) {
            double x = j * grid_size - field.width / 2.0;
            double y = i * grid_size - field.height / 2.0;
            for (const auto& obj : field.getTerrainObjects()) {
                if (std::abs(x - obj.position.x) < grid_size / 2.0 && std::abs(y - obj.position.y) < grid_size / 2.0) {
                    map[i][j] = obj.name.substr(0, 1) + (isTerrainObjectEncircled(obj) ? "*" : "");
                    continue;
                }
            }
            TerrainObject* t = field.getTerrainObject(x, y);
            if (t) {
                if (t->type == TerrainType::Hills) map[i][j] = "H";
                else if (t->type == TerrainType::Valley) map[i][j] = "V";
                else if (t->type == TerrainType::River) map[i][j] = "R";
                else if (t->type == TerrainType::Forest) map[i][j] = "F";
                else if (t->type == TerrainType::Tunnel) map[i][j] = "T";
                else if (t->type == TerrainType::Airfield) map[i][j] = "A";
            }
        }
    }
    std::map<std::pair<int, int>, std::vector<Agent*>> grid_agents;
    logger.info(turn) << "Active agents in turn: " << agents.size();
    for (auto* agent : agents) {
        if (!agent || agent->mergedOrPruned ||
            agent->profile.currentStage == "Crushing Defeat" ||
            agent->profile.currentStage == "fleeing Off the Map") {
            continue;
        }
        logger.debug(turn) << "Agent " << agent->profile.name << ": stage=" << agent->profile.currentStage
            << ", mergedOrPruned=" << agent->mergedOrPruned
            << ", inTunnel=" << field.isInTunnel(agent) << ", position=["
            << agent->profile.position.x << "," << agent->profile.position.y << "]";
        if (agent->profile.currentStage != "Retreating" && !field.isInTunnel(agent)) {
            int x = static_cast<int>((agent->profile.position.x + field.width / 2.0) / grid_size);
            int y = static_cast<int>((agent->profile.position.y + field.height / 2.0) / grid_size);
            if (x >= 0 && x < grid_width && y >= 0 && y < grid_height) {
                grid_agents[{x, y}].push_back(agent);
            }
            else {
                logger.warn(turn) << "Agent " << agent->profile.name << " out of grid bounds: [" << x << "," << y << "]";
            }
        }
    }
    for (const auto& [pos, agent_list] : grid_agents) {
        int x = pos.first;
        int y = pos.second;
        if (agent_list.empty()) continue;
        Agent* selected_agent = agent_list[0];
        int max_troops = selected_agent->profile.remainingNumOfTroops();
        for (auto* agent : agent_list) {
            if (agent->profile.remainingNumOfTroops() > max_troops) {
                selected_agent = agent;
                max_troops = agent->profile.remainingNumOfTroops();
            }
        }
        std::string sym = selected_agent->isCountryA() ? "V" : "F";
        if (selected_agent->profile.troopType == "scout") {
            sym = selected_agent->isCountryA() ? "v" : "f";
        }
        else if (selected_agent->profile.troopType == "artillery") {
            sym = "Ar";
        }
        else if (selected_agent->profile.troopType == "anti_aircraft") {
            sym = "a";
        }
        std::string troops_str = std::to_string(std::min(max_troops, 999));
        map[y][x] = sym + troops_str;
        logger.debug(turn) << "Mapped agent " << selected_agent->profile.name << " to grid [" << x << "," << y
            << "] with symbol " << map[y][x] << (agent_list.size() > 1 ? " (multiple agents)" : "");
    }
    logger.info(turn) << "Battlefield Deployment:";
    logger.info(turn) << "Weather: " << field.getWeather() << " (Visibility: " << field.getVisibilityModifier()
        << ", Artillery: " << field.getArtilleryModifier() << ")";
    logger.info(turn) << "Grid size: " << grid_width << "x" << grid_height << " (Width: " << field.width
        << ", Height: " << field.height << ")";
    for (const auto& row : map) {
        std::ostringstream row_stream;
        for (const auto& cell : row) {
            row_stream << std::setw(8) << std::left << cell;
        }
        logger.info(turn) << row_stream.str();
    }
    logger.info(turn) << "F/f/Ar: French (Commander/Scout/Artillery), V/v/a: Viet Minh, H: Hills, V: Valley, R: River, F: Forest, "
        << "T: Tunnel, A: Airfield, H*: Him Lam (encircled), D*: Doc Lap (encircled), .: Flat";
}


void Simulation::updateTargetList() {
    for (auto* agent : agents) {
        if (!agent->mergedOrPruned && agent->profile.currentStage != "Crushing Defeat" &&
            agent->profile.currentStage != "fleeing Off the Map") {
            Agent* current_target = agent->getTarget();
            if (current_target && !current_target->mergedOrPruned &&
                current_target->profile.currentStage != "Crushing Defeat" &&
                current_target->profile.currentStage != "fleeing Off the Map") {
                logger.info() << "Agent " << agent->profile.name << " retains target: " << current_target->profile.name;
                continue;
            }
            agent->profile.targetedAgentName = "";
            Agent* selected_target = nullptr;
            double  min_distance = std::numeric_limits<double>::max();
            bool    is_country_a = agent->isCountryA();
            if (is_country_a) {
                for (const auto& terrain : field.getTerrainObjects()) {
                    if (terrain.type == TerrainType::Stronghold) {
                        for (auto* other_agent : agents) {
                            if (other_agent != agent && !other_agent->mergedOrPruned &&
                                other_agent->profile.currentStage != "Crushing Defeat" &&
                                other_agent->profile.currentStage != "fleeing Off the Map" &&
                                !other_agent->isCountryA()) {
                                double distance =
                                    std::sqrt(std::pow(other_agent->profile.position.x - terrain.position.x, 2) +
                                        std::pow(other_agent->profile.position.y - terrain.position.y, 2));
                                if (distance < 100 && distance < min_distance) {
                                    min_distance = distance;
                                    selected_target = other_agent;
                                }
                            }
                        }
                    }
                }
                if (field.isInTunnel(agent) && !selected_target) {
                    for (const auto& terrain : field.getTerrainObjects()) {
                        if (terrain.type == TerrainType::Tunnel) {
                            for (auto* other_agent : agents) {
                                if (other_agent != agent && !other_agent->mergedOrPruned &&
                                    other_agent->profile.currentStage != "Crushing Defeat" &&
                                    other_agent->profile.currentStage != "fleeing Off the Map" &&
                                    !other_agent->isCountryA()) {
                                    double distance = std::sqrt(
                                        std::pow(other_agent->profile.position.x - terrain.end_position.x, 2) +
                                        std::pow(other_agent->profile.position.y - terrain.end_position.y, 2));
                                    if (distance < min_distance) {
                                        min_distance = distance;
                                        selected_target = other_agent;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else {
                for (const auto& terrain : field.getTerrainObjects()) {
                    if (terrain.type == TerrainType::Stronghold && std::abs(terrain.position.x - 0) < 1e-6 &&
                        std::abs(terrain.position.y + 200) < 1e-6) {
                        for (auto* other_agent : agents) {
                            if (other_agent != agent && !other_agent->mergedOrPruned &&
                                other_agent->profile.currentStage != "Crushing Defeat" &&
                                other_agent->profile.currentStage != "fleeing Off the Map" &&
                                other_agent->isCountryA()) {
                                double distance =
                                    std::sqrt(std::pow(other_agent->profile.position.x - terrain.position.x, 2) +
                                        std::pow(other_agent->profile.position.y - terrain.position.y, 2));
                                if (distance < min_distance) {
                                    min_distance = distance;
                                    selected_target = other_agent;
                                }
                            }
                        }
                    }
                }
            }
            if (!selected_target) {
                for (auto* other_agent : agents) {
                    if (other_agent != agent && !other_agent->mergedOrPruned &&
                        other_agent->profile.currentStage != "Crushing Defeat" &&
                        other_agent->profile.currentStage != "fleeing Off the Map" &&
                        is_country_a != other_agent->isCountryA()) {
                        double distance =
                            std::sqrt(std::pow(agent->profile.position.x - other_agent->profile.position.x, 2) +
                                std::pow(agent->profile.position.y - other_agent->profile.position.y, 2));
                        if (distance < min_distance) {
                            min_distance = distance;
                            selected_target = other_agent;
                        }
                    }
                }
            }
            if (selected_target) {
                agent->profile.targetedAgentName = selected_target->profile.name;
                agent->setTarget(selected_target);
                logger.info() << "Agent " << agent->profile.name << " selected target: " << selected_target->profile.name
                    << " at distance " << static_cast<int>(min_distance);
            }
            else {
                logger.warn() << "Agent " << agent->profile.name << " found no valid target";
            }
        }
    }
}

void Simulation::run(int num_rounds) {
    chart_data = {
    {"data",
     { { "labels", nlohmann::json::array() },
       { "datasets",
         { { { "label", "Vietnamese Troops" }, { "data", nlohmann::json::array() } },
           { { "label", "French Troops" }, { "data", nlohmann::json::array() } },
           { { "label", "Vietnamese Agents" }, { "data", nlohmann::json::array() } },
           { { "label", "French Agents" }, { "data", nlohmann::json::array() } } } } }}
    };
    field.setWeather("Clear", 1.0, 1.0);
    std::string last_weather_type = "Clear";
    double      last_visibility_modifier = 1.0;
    double      last_artillery_modifier = 1.0;
    for (int turn = 0; turn < num_rounds; ++turn) {
        if (config.contains("historical_events")) {
            for (const auto & event : config["historical_events"]) {
                if (event.contains("round") && event["round"].get<int>() == turn + 1) {
                    logger.info(turn + 1) << "HISTORICAL EVENT: " << event["event"].get<std::string>();
                }
            }
        }

        double anti_aircraft_modifier = last_artillery_modifier;
        if (config.contains("terrain_config") && config["terrain_config"].contains("weather")) {
            for (const auto& weather : config["terrain_config"]["weather"]) {
                if (weather.contains("turn") && weather["turn"].get<int>() == turn + 1) {
                    try {
                        std::string weather_type = weather["type"].get<std::string>();
                        double      visibilityModifier = weather["visibilityModifier"].get<double>();
                        double      artilleryModifier = weather.value("artilleryModifier", 1.0);
                        field.setWeather(weather_type, visibilityModifier, artilleryModifier);
                        anti_aircraft_modifier = artilleryModifier;
                        last_weather_type = weather_type;
                        last_visibility_modifier = visibilityModifier;
                        last_artillery_modifier = artilleryModifier;
                        logger.info(turn + 1) << "Applied weather: " << weather_type
                            << ", visibility=" << visibilityModifier
                            << ", artilleryModifier=" << artilleryModifier;
                    }
                    catch (const std::exception& e) {
                        logger.error(turn + 1) << "Invalid weather config: " << e.what();
                    }
                }
            }
        }
        else {
            logger.info(turn + 1) << "No weather config, using " << last_weather_type;
        }
        std::map<std::string, int> tunnel_troops;
        for (const auto& terrain : field.getTerrainObjects()) {
            if (terrain.type == TerrainType::Tunnel) {
                tunnel_troops[terrain.name] = 0;
            }
        }
        for (auto* agent : agents) {
            if (!agent->mergedOrPruned && field.isInTunnel(agent)) {
                TerrainObject* tunnel = field.getTerrainObject(agent->profile.position.x, agent->profile.position.y);
                if (tunnel && tunnel->type == TerrainType::Tunnel) {
                    tunnel_troops[tunnel->name] += agent->profile.remainingNumOfTroops();
                    if (tunnel_troops[tunnel->name] > tunnel->power) {
                        logger.warn(turn + 1) << "Agent " << agent->profile.name << " exceeds tunnel capacity (" << tunnel->power
                            << ") in " << tunnel->name;
                        agent->profile.currentAction = "Wait without Action";
                    }
                    else {
                        agent->profile.tactics["stealth"] =
                            std::max(agent->profile.tactics["stealth"], tunnel->stealth_bonus);
                        logger.info(turn + 1) << "Agent " << agent->profile.name << " in tunnel " << tunnel->name
                            << ", stealth=" << agent->profile.tactics["stealth"];
                    }
                }
            }
        }
        for (auto* agent : agents) {
            if (!agent->mergedOrPruned && agent->profile.currentStage != "Crushing Defeat" &&
                agent->profile.currentStage != "fleeing Off the Map") {
                try {
                    agent->profile.updateTroopInformation();
                    agent->profile.currentBattlefieldSituation = field.generateBattlefieldSituation(agent);
                    agent->profile.roundNb = turn + 1;
                    nlohmann::json weather_info = field.getWeather();
                    if (weather_info.contains("visibilityModifier") &&
                        weather_info["visibilityModifier"].get<double>() < 0.7) {
                        agent->profile.tactics["stealth"] = std::max(agent->profile.tactics["stealth"], 0.8);
                        logger.info(turn + 1) << "Agent " << agent->profile.name << " increased stealth to "
                            << agent->profile.tactics["stealth"] << " due to low visibility";
                    }
                    agent->execute();
                    applyMoraleEffect(agent);
                    applyActionEffects(agent, anti_aircraft_modifier);
                    checkSupplyLine(agent);
                }
                catch (const std::exception& e) {
                    logger.error(turn + 1) << "Exception in agent " << agent->profile.name << " execution: " << e.what();
                }
            }
        }
        updateTargetList();
        visualizeDeployment(turn + 1, true);
        int viet_total = 0, french_total = 0, viet_agents = 0, french_agents = 0;
        for (auto* agent : agents) {
            if (!agent->mergedOrPruned && agent->profile.currentStage != "Crushing Defeat" &&
                agent->profile.currentStage != "fleeing Off the Map") {
                if (agent->isCountryA()) {
                    viet_total += agent->profile.remainingNumOfTroops();
                    viet_agents++;
                }
                else {
                    french_total += agent->profile.remainingNumOfTroops();
                    french_agents++;
                }
            }
        }
        chart_data["data"]["labels"].push_back(turn + 1);
        chart_data["data"]["datasets"][0]["data"].push_back(viet_total);
        chart_data["data"]["datasets"][1]["data"].push_back(french_total);
        chart_data["data"]["datasets"][2]["data"].push_back(viet_agents);
        chart_data["data"]["datasets"][3]["data"].push_back(french_agents);

        logger.info(turn + 1) << countryA->profile.name<< " troops = " << viet_total <<", "<< countryB->profile.name << " troops = " << french_total
            << ", "<< countryA->profile.name <<" agents = " << viet_agents << ", "<< countryB->profile.name <<" agents = " << french_agents;
        if (french_total < 600) {
            logger.info(turn + 1) << countryA->profile.name << " wins!";
            break;
        }
        if (viet_total < 1200) {
            logger.info(turn + 1) << countryB->profile.name << " wins!";          
            break;
        }
    }
    try {
        std::ofstream chart_out("troop_loss_chart.json");
        chart_out << chart_data.dump(2);
        chart_out.close();
    }
    catch (const std::exception& e) {
        logger.error() << "Failed to save troop_loss_chart.json: " << e.what();
    }

    logger.info() << "Simulation ended.";
    for (auto* agent : agents) {
        if (!agent->mergedOrPruned && agent->profile.currentStage != "Crushing Defeat") {
            logger.info() << "Agent " << agent->profile.name << " final state: " << agent->profile.currentStage;
        }
    }
}
