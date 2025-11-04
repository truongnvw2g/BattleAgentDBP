#include "Soldier.h"
#include "Agent.h"
#include "Simulation.h"
#include <sstream>

SoldierAgent::SoldierAgent(const nlohmann::json & json_data) {
    name                  = json_data.value("Name", "Unknown");
    age                   = json_data.value("Age", 25);
    family                = json_data.value("Family", "");
    occupation            = json_data.value("Occupation", "Binh sĩ");
    personality           = json_data.value("Personality", "Trung bình");
    socialStatus          = json_data.value("SocialStatus", "Bình dân");
    potentialIllness      = json_data.value("PotentialIllness", "Không");
    bodyCondition         = json_data.value("BodyCondition", "Bình thường");
    hobbiesAndInterests   = json_data.value("HobbiesAndInterests", "");
    styleOfTalking        = json_data.value("StyleOfTalking", "Bình thường");
    uniqueQuirks          = json_data.value("UniqueQuirks", "");
    secretsOrScandals     = json_data.value("SecretsOrScandals", "");
    morale_start          = moraleFromString(json_data.value("MoraleStart", "Medium"));
    current_fatigue_level = json_data.value("current_fatigue_level", "Medium");
    current_morale        = morale_start;
    commander             = nullptr;
}

std::string SoldierAgent::speak(const std::string & context) const {
    std::vector<std::string> templates;

    if (context == "heavy_loss") {
        if (personality.find("bi quan") != std::string::npos) {
            templates = { "Trời ơi, lại chết nữa rồi... Bao giờ mới hết?", "Tôi không chịu nổi nữa..." };
        } else if (personality.find("kiên cường") != std::string::npos) {
            templates = { "Máu đổ, nhưng ta vẫn tiến lên!", "Vì dân tộc, hy sinh là vinh quang." };
        }
    } else if (context == "tunnel") {
        templates = { "Hầm sâu, an toàn. Pháo không tới được.", "Dưới đất như ma, trên mặt đất là thần." };
    } else if (context == "near_target") {       
        templates = { "Tôi thấy đồi Him Lam rồi! Chỉ một bước nữa!", "Về nhà thôi, anh em ơi!" };
      
    } else {
        if (current_fatigue_level == "High") {
            templates = { "Mệt quá... chân muốn rụng rồi.", "Ngủ một giấc thôi, xin chỉ huy." };
        } else if (current_morale >= High) {
            templates = { "Sẵn sàng! Ra lệnh đi!", "Hôm nay là ngày chiến thắng!" };
        }
    }

    if (templates.empty()) {
        return name + ": \"...\"";
    }
    return name + " (" + std::to_string(age) + ", " + occupation + "): \"" + templates[std::rand() % templates.size()] +
           "\"";
}

void SoldierAgent::updateMorale(int loss_this_round, bool in_tunnel, bool near_target) {
    int morale_change = 0;
    if (loss_this_round > 500) {
        morale_change -= 2;
    } else if (loss_this_round > 200) {
        morale_change -= 1;
    }

    if (in_tunnel) {
        morale_change += 1;
    }
    if (near_target) {
        morale_change += 1;
    }
    if (current_fatigue_level == "High") {
        morale_change -= 1;
    }

    current_morale = static_cast<Morale>(std::clamp(static_cast<int>(current_morale) + morale_change, 0, 4));
}

SoldierCollector::~SoldierCollector() {
    // 1. Giải phóng bộ nhớ cho các SoldierAgent đã được triển khai (deployedSoldiers)
    // Cấu trúc mới: std::map<std::string, std::vector<SoldierAgent*>> deployedSoldiers;
    for (auto const& pair : deployedSoldiers) {
        // pair.second là std::vector<SoldierAgent*>
        const std::vector<SoldierAgent*>& soldierList = pair.second;

        // Lặp qua từng SoldierAgent* trong vector và xóa
        for (SoldierAgent* soldier : soldierList) {
            delete soldier;
        }
    }

    // 2. Giải phóng bộ nhớ cho các SoldierAgent còn lại (availableSoldiers)
    // Cấu trúc: std::map<std::string, SoldierAgent*> availableSoldiers;
    for (const auto & soldier : availableSoldiers) {
        // pair.second là SoldierAgent*
        delete soldier;
    }

    // Ghi chú: Nếu có các cấu trúc dữ liệu khác chứa SoldierAgent*, cũng cần được giải phóng tại đây.
}


SoldierCollector::SoldierCollector(const nlohmann::json& allProfilesJson) {
    rng.seed(static_cast<unsigned int>(std::time(0)));

    loadAllAvailableSoldiers(allProfilesJson);
}
void SoldierCollector::loadAllAvailableSoldiers(const nlohmann::json& allProfilesJson) {
    for (nlohmann::json::const_iterator it = allProfilesJson.begin(); it != allProfilesJson.end(); ++it) {
        std::string profileKey = it.key();
        try {
            // Tạo đối tượng SoldierAgent từ dữ liệu JSON
            SoldierAgent* newSoldier = new SoldierAgent(it.value());
            availableSoldiers.push_back(newSoldier);
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading SoldierAgent for " << profileKey << ": " << e.what() << std::endl;
        }
    }
    std::cout << "Loaded and initialized " << availableSoldiers.size() << " SoldierAgents as available templates.\n";
}

std::vector<SoldierAgent*> SoldierCollector::getRandomAvailableSoldiers(int count) {
    std::vector<SoldierAgent*> result;

    // 1. Kiểm tra điều kiện
    if (count <= 0) {
        return result;
    }

    if (availableSoldiers.empty()) {
        // Không còn lính nào để chọn
        return result;
    }

    // Đảm bảo không chọn quá số lượng lính hiện có
    int actual_count = std::min((int)availableSoldiers.size(), count);

    // 3. Tạo một vector chứa các chỉ số (index)
    std::vector<int> indices(availableSoldiers.size());
    std::iota(indices.begin(), indices.end(), 0); // Điền 0, 1, 2, 3, ...

    // 4. Trộn ngẫu nhiên các chỉ số
    // Sử dụng rng (std::mt19937) đã được khởi tạo trong SoldierCollector
    std::shuffle(indices.begin(), indices.end(), rng);

    // 5. Lấy các SoldierAgent* từ các chỉ số đã chọn
    for (int i = 0; i < actual_count; ++i) {
        result.push_back(availableSoldiers[indices[i]]);
    }

    return result;
}
std::vector<SoldierAgent*> SoldierCollector::getSoldiers(Agent* owner) {
    if (!owner) {
        return {};
    }
    auto it = deployedSoldiers.find(owner);
    if (it != deployedSoldiers.end()) {
        // Trả về vector lính của Agent chỉ huy này
        return it->second;
    }
    return {}; // Trả về vector rỗng nếu không tìm thấy
}

void SoldierCollector::deploySoldier(SoldierAgent * soldier, Agent * owner) {
    if (!soldier) {
        throw std::invalid_argument("Cannot deploy a nullptr soldier.");
    }
    if (!owner) {
        throw std::invalid_argument("Cannot deploy soldier to a nullptr commander agent.");
    }

    auto it = std::find(availableSoldiers.begin(), availableSoldiers.end(), soldier);
    if (it != availableSoldiers.end()) {
        // ✅ Tạo bản sao (deep copy)
        SoldierAgent * newSoldier = new SoldierAgent(*soldier);
        newSoldier->commander     = owner;

        // ✅ Thêm vào danh sách triển khai
        deployedSoldiers[owner].push_back(newSoldier);

        // availableSoldiers.erase(it);
    } else {
        throw std::runtime_error("Soldier not found in available list.");
    }
}
