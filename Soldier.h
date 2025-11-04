#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <random>
#include <ctime>
#include "nlohmann/json.hpp" 

class Agent;

class SoldierAgent {
public:
    enum Morale { VeryLow, Low, Medium, High, VeryHigh };

    SoldierAgent(const nlohmann::json & json_data);

    SoldierAgent * clone() const {
        SoldierAgent * copy = new SoldierAgent(*this);
        copy->commander     = nullptr;
        return copy;
    }

    bool operator==(const SoldierAgent & other) const noexcept {
        return name == other.name && age == other.age && family == other.family && occupation == other.occupation &&
               personality == other.personality && socialStatus == other.socialStatus;
    }

    std::string speak(const std::string & context) const;
  
    void updateMorale(int loss_this_round, bool in_tunnel, bool near_target);

    bool operator!=(const SoldierAgent & other) const noexcept { return !(*this == other); }

    nlohmann::json toJson() const {
        return {
            {"Name",                   name                        },
            { "Age",                   age                         },
            { "Family",                family                      },
            { "Occupation",            occupation                  },
            { "Personality",           personality                 },
            { "SocialStatus",          socialStatus                },
            { "PotentialIllness",      potentialIllness            },
            { "BodyCondition",         bodyCondition               },
            { "HobbiesAndInterests",   hobbiesAndInterests         },
            { "StyleOfTalking",        styleOfTalking              },
            { "UniqueQuirks",          uniqueQuirks                },
            { "SecretsOrScandals",     secretsOrScandals           },
            { "MoraleStart",           moraleToString(morale_start)},
            { "current_fatigue_level", current_fatigue_level       }
        };
    }

    std::string name;
    int         age;
    std::string family;
    std::string occupation;
    std::string personality;
    std::string socialStatus;
    std::string potentialIllness;
    std::string bodyCondition;
    std::string hobbiesAndInterests;
    std::string styleOfTalking;
    std::string uniqueQuirks;
    std::string secretsOrScandals;
    Agent*      commander;   

    Morale      current_morale;
    std::string current_fatigue_level;
    Morale      morale_start;
  private:
    static Morale moraleFromString(const std::string & s) {
        if (s == "Very High") {
            return VeryHigh;
        }
        if (s == "High") {
            return High;
        }
        if (s == "Low") {
            return Low;
        }
        if (s == "Very Low") {
            return VeryLow;
        }
        return Medium;
    }

    static std::string moraleToString(Morale m) {
        switch (m) {
            case VeryHigh:
                return "Very High";
            case High:
                return "High";
            case Low:
                return "Low";
            case VeryLow:
                return "Very Low";
            default:
                return "Medium";
        }
    }
};

class SoldierCollector {

public:   
    SoldierCollector(const nlohmann::json& allProfilesJson);

    ~SoldierCollector();

    std::vector<SoldierAgent *> getSoldiers(Agent * owner);   

    void deploySoldier(SoldierAgent * soldier, Agent * owner);

    unsigned int getNumAvailableSoldiers() {
        return  availableSoldiers.size();
    }
    std::vector<SoldierAgent*> getRandomAvailableSoldiers(int count = 10);

private:
    void loadAllAvailableSoldiers(const nlohmann::json& allProfilesJson);   


private:
    std::vector<SoldierAgent*> availableSoldiers;

    std::map<Agent*, std::vector<SoldierAgent*>> deployedSoldiers;

    std::mt19937 rng;

};
