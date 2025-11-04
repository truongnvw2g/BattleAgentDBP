#pragma once
#include "llama.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class LLMInference {
  public:
    LLMInference(const std::string & model, int ngl = 99, int n_ctx = 2048);
    ~LLMInference();

    std::string infer(const std::string & prompt);
    std::string infer(const std::vector<nlohmann::json> & prompts,const nlohmann::json & response_format = "");

    void setSamplerParams(float temperature, float min_p);
    void setLogPath(const std::string & path);

    bool isInitialized() const noexcept;

    void reset();

  private:
    LLMInference(const LLMInference &)             = delete;
    LLMInference & operator=(const LLMInference &) = delete;

    struct ModelDeleter {
        void operator()(llama_model * p) const noexcept;
    };

    struct ContextDeleter {
        void operator()(llama_context * p) const noexcept;
    };

    struct SamplerDeleter {
        void operator()(llama_sampler * p) const noexcept;
    };

    std::unique_ptr<llama_model, ModelDeleter>          model;
    std::unique_ptr<llama_context, ContextDeleter>      ctx;
    std::unique_ptr<llama_sampler, SamplerDeleter>      smpl;

    int                                                 n_ctx = 16384;
    int                                                 ngl   = 0;

    float                                               temperature = 0.8f;
    float                                               min_p       = 0.05f;

    std::ofstream                                       log;
    std::vector<std::string>                            server_ips;
    bool                                                is_server_mode = false;
    std::mutex                                          log_mutex;


    void initialize(const std::string & model_path, int ngl, int n_ctx);

    std::string response(const std::vector<std::string> & ips, const std::string & prompts, int timeout_ms = 1600000);

    std::string response(const std::string &                 ip,
                         const std::vector<nlohmann::json> & prompts,
                         const nlohmann::json & response_format = "",
                         int timeout_ms = 16000000);
    std::string response(const std::vector<std::string> &    ips,
                         const std::vector<nlohmann::json> & prompts,
                         const nlohmann::json &              response_format = "",
                         int                                 timeout_ms =1600000);

    std::string response(const std::string & prompt);
    std::string response(const std::vector<nlohmann::json> & prompts);

    bool is_url(const std::string & str) const;  // Hàm kiểm tra URL

    bool validateAndFormatPrompts(const std::vector<nlohmann::json> & prompts,
                                  std::vector<nlohmann::json> &       formatted_prompts);
    bool validateAndFormatPrompts(const std::string & prompts, std::vector<nlohmann::json> & formatted_prompts);
};
