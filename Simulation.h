#pragma once
#include "Agent.h"
#include "Soldier.h"
#include "BattleField.h"
#include "LLMInference.h"
#include "nlohmann/json.hpp"
#include "Profile.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

enum class LogLevel { INFO, DEBUG, WARN, ERROR };

class Logger {
public:
    Logger(const std::string& filename = "simulation_log.txt", bool log_to_console = true, LogLevel min_level = LogLevel::INFO);
    ~Logger();

    // Lớp tạm để hỗ trợ cú pháp << cho mỗi mức log
    class LogStream {
    public:
        LogStream(Logger& logger, LogLevel level, int turn = -1);
        ~LogStream();

        template<typename T>
        LogStream& operator<<(const T& value) {
            stream_ << value;
            return *this;
        }

        // Hỗ trợ cho std::endl và các manipulator khác
        LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
            stream_ << manip;
            return *this;
        }

    private:
        Logger& logger_;
        LogLevel level_;
        std::ostringstream stream_;
        int turn_;
    };

    LogStream info(int turn = -1) { return LogStream(*this, LogLevel::INFO, turn); }
    LogStream debug(int turn = -1) { return LogStream(*this, LogLevel::DEBUG, turn); }
    LogStream warn(int turn = -1) { return LogStream(*this, LogLevel::WARN, turn); }
    LogStream error(int turn = -1) { return LogStream(*this, LogLevel::ERROR, turn); }

    void setLogToConsole(bool enable) { log_to_console_ = enable; }
    void setMinLogLevel(LogLevel level) { min_level_ = level; }

private:
    void log(LogLevel level, const std::string& message, int turn);

    std::ofstream file_;
    bool log_to_console_;
    LogLevel min_level_;
};
class Simulation {
  public:
    BattleField                   field;
    Agent *                       countryA;
    Agent *                       countryB;
    SoldierCollector*             soldierCollectorA;

    SoldierCollector*             soldierCollectorB;
    std::vector<Agent *>          agents;
    nlohmann::json                config, chart_data;
  
    std::map<std::string, int>    encirclement_turns;
    std::shared_ptr<LLMInference> llm; 
    std::string                   model_path;
    int                           unique_id_counter;
    Logger                        logger;

    Simulation(const nlohmann::json & config);
    ~Simulation();

    bool addAgent(Agent * child);

    bool insertAgent(unsigned int index, Agent * child);

    virtual bool removeAgent(Agent * child);

    inline bool removeAgent(unsigned int pos, unsigned int numChildrenToRemove = 1) {
        if (pos < agents.size()) {
            return removeAgents(pos, numChildrenToRemove);
        } else {
            return false;
        }
    }

    bool removeAgents(unsigned int pos, unsigned int numChildrenToRemove);

    bool replaceAgent(Agent * origChild, Agent * newChild);

    unsigned int getNumAgents() const;

    bool setAgent(unsigned int i, Agent * agent);

    inline std::vector<Agent *> getAgents() { return agents; }

    inline Agent * getAgent(unsigned int i) { return agents[i]; }

    inline const Agent * getAgent(unsigned int i) const { return agents[i]; }

    inline bool containsAgent(const Agent * agent) const {
        for (std::vector<Agent *>::const_iterator itr = agents.begin(); itr != agents.end(); ++itr) {
            if ((*itr) == agent) {
                return true;
            }
        }
        return false;
    }

    inline unsigned int getAgentIndex(const Agent * agent) const {
        for (unsigned int childNum = 0; childNum < agents.size(); ++childNum) {
            if (agents[childNum] == agent) {
                return childNum;
            }
        }
        return static_cast<unsigned int>(agents.size());  // agent not found.
    }

    Agent *     findAgent(const std::string & name);

    int         generateUniqueId();   
    bool        isTerrainObjectCaptured(const TerrainObject & obj);
    bool        isTerrainObjectEncircled(const TerrainObject & obj);
    void        applyMoraleEffect(Agent * agent);
    std::string checkSupplyLine(Agent * agent);
    void        applyActionEffects(Agent * agent, double & anti_aircraft_modifier);
 
    void        logState(int turn, const std::string & team, Agent * commander);
    void        visualizeDeployment(int turn, bool output_to_console);
    void        updateTargetList();
    void        run(int num_rounds);
};
