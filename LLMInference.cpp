#include "LLMInference.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>
#include <iomanip>
#include <regex>
#include <future>
#include <thread>
#include <atomic>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <random>
#define NOMINMAX
#include <curl/curl.h>

class ThreadPool {
  public:
    ThreadPool(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) {
                            return;
                        }
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread & worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(std::move(task));
        }
        condition.notify_one();
    }

  private:
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;
    std::mutex                        queue_mutex;
    std::condition_variable           condition;
    bool                              stop;
};


bool LLMInference::validateAndFormatPrompts(const std::string &           prompts,
                                            std::vector<nlohmann::json> & formatted_prompts) {
    formatted_prompts.clear();

    nlohmann::json prompts_json = nlohmann::json::parse(prompts);
    if (!prompts_json.is_array()) {
        log << "Prompts must be a JSON array: " << prompts << "\n";
        return false;
    }

    for (const auto & prompt : prompts_json) {
        try {
            if (prompt.is_string()) {
                formatted_prompts.push_back({
                    {"role",     "user"                   },
                    { "content", prompt.get<std::string>()}
                });
                continue;
            }
            if (!prompt.is_object()) {
                log << "Prompt is not an object or string: " << prompt.dump() << "\n";
                return false;
            }
            if (!prompt.contains("role") || !prompt.contains("content") || !prompt["role"].is_string() ||
                !prompt["content"].is_string()) {
                log << "Invalid prompt format, missing role or content: " << prompt.dump() << "\n";
                return false;
            }
            std::string role = prompt["role"].get<std::string>();
            std::transform(role.begin(), role.end(), role.begin(), [](unsigned char c) { return std::tolower(c); });
            if (role != "system" && role != "user" && role != "assistant") {
                log << "Invalid role in prompt, must be 'system', 'user', or 'assistant' (case-insensitive): "
                    << prompt.dump() << "\n";
                return false;
            }
            formatted_prompts.push_back({
                {"role",     role                                },
                { "content", prompt["content"].get<std::string>()}
            });
        } catch (const std::exception & e) {
            log << "Error processing prompt: " << e.what() << "\n";
            return false;
        }
    }

    return true;
}

bool LLMInference::validateAndFormatPrompts(const std::vector<nlohmann::json> & prompts,
                                            std::vector<nlohmann::json> &       formatted_prompts) {
    formatted_prompts.clear();
    for (const auto & prompt : prompts) {
        try {
            // Kiểm tra nếu prompt là chuỗi
            if (prompt.is_string()) {
                formatted_prompts.push_back({
                    {"role",     "user"                   },
                    { "content", prompt.get<std::string>()}
                });
                continue;
            }
            // Kiểm tra nếu prompt là đối tượng
            if (!prompt.is_object()) {
                log << "Prompt is not an object or string: " << prompt.dump() << "\n";
                return false;
            }
            // Kiểm tra các khóa cần thiết
            if (!prompt.contains("role") || !prompt.contains("content") || !prompt["role"].is_string() ||
                !prompt["content"].is_string()) {
                log << "Invalid prompt format, missing role or content: " << prompt.dump() << "\n";
                return false;
            }
            std::string role = prompt["role"].get<std::string>();
            std::transform(role.begin(), role.end(), role.begin(), [](unsigned char c) { return std::tolower(c); });
            if (role != "system" && role != "user" && role != "assistant") {
                log << "Invalid role in prompt, must be 'system', 'user' or 'assistant': " << prompt.dump() << "\n";
                return false;
            }
            // Prompt hợp lệ, thêm vào danh sách
            formatted_prompts.push_back({
                {"role",     role                                },
                { "content", prompt["content"].get<std::string>()}
            });
        } catch (const std::exception & e) {
            log << "Error processing prompt: " << e.what() << "\n";
            return false;
        }
    }
   
    return true;
}

// ========== custom deleters ==========
void LLMInference::ModelDeleter::operator()(llama_model * p) const noexcept {
    if (p) {
        llama_model_free(p);
    }
}

void LLMInference::ContextDeleter::operator()(llama_context * p) const noexcept {
    if (p) {
        llama_free(p);
    }
}

void LLMInference::SamplerDeleter::operator()(llama_sampler * p) const noexcept {
    if (p) {
        llama_sampler_free(p);
    }
}

bool LLMInference::is_url(const std::string & str) const {
    static const std::regex pattern(
        R"(^(https?:\/\/)?(([a-zA-Z0-9.-]+)|(\d{1,3}\.){3}\d{1,3}|localhost)(:\d{1,5})?(\/.*)?$)");
    return std::regex_match(str, pattern);
}

LLMInference::LLMInference(const std::string & model, int ngl_, int n_ctx_) : n_ctx(n_ctx_), ngl(ngl_) {
    log.open("llminference.log", std::ios::app);
    if (!log.is_open()) {
        std::cerr << "❌ Không thể mở file log: "
                  << "llminference.log" << std::endl;
    }
    if (model.find_first_of(", ;") != std::string::npos || is_url(model)) {
        is_server_mode = true;
        std::regex re(R"([,;\s]+)");
        for (std::sregex_token_iterator it(model.begin(), model.end(), re, -1); it != std::sregex_token_iterator();
             ++it) {
            std::string token = it->str();
            token.erase(0, token.find_first_not_of(" \t\n\r"));
            token.erase(token.find_last_not_of(" \t\n\r") + 1);
            if (!token.empty() && is_url(token)) {
                server_ips.push_back(token);
            }
        }
        if (!server_ips.empty()) {
            curl_global_init(CURL_GLOBAL_ALL);            
        }
        log << "Server mode: " + std::to_string(server_ips.size()) + " endpoint(s)\n";
    } else {
        is_server_mode = false;
        initialize(model, ngl, n_ctx);
        log << "Local model mode: " + model << "\n";
    }
}

LLMInference::~LLMInference() {
    smpl.reset();
    ctx.reset();
    model.reset();
    if (is_server_mode) {
        curl_global_cleanup();
    }
    if (log.is_open()) {
        log.close();
    }
}

void LLMInference::reset() {
    if (ctx && !is_server_mode) {
        llama_free(ctx.get());
        llama_context_params ctxp = llama_context_default_params();
        ctxp.n_threads            = std::min(8, std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1));
        ctxp.n_ctx                = n_ctx;
        ctxp.n_batch              = n_ctx;
        ctx.reset(llama_init_from_model(model.get(), ctxp));
        log << "Context reset successfully\n";
    }
}

void LLMInference::setSamplerParams(float temperature, float min_p) {
    temperature = temperature;
    min_p       = min_p;
}

bool LLMInference::isInitialized() const noexcept {
    if (is_server_mode) {
        return !server_ips.empty();
    }
    return model.get() && ctx.get() && smpl.get();
}

// ========== initialize model ==========
void LLMInference::initialize(const std::string & model_path, int ngl, int n_ctx) {
    ggml_backend_load_all();

    llama_model_params mparams = llama_model_default_params();
    if (ngl >= 0) {
        mparams.n_gpu_layers = ngl;
    }

    llama_model * raw_model = llama_model_load_from_file(model_path.c_str(), mparams);
    if (!raw_model) {
        log << "Error: Failed to load model from " + model_path << "\n";
        return;
    }
    model.reset(raw_model);

    unsigned int hw        = std::thread::hardware_concurrency();
    int          n_threads = 1;
    if (hw > 1) {
        n_threads = std::min(8, std::max(1, static_cast<int>(hw) - 1));
    }
    llama_context_params ctxp = llama_context_default_params();
    ctxp.n_threads            = n_threads;
    ctxp.n_threads_batch      = n_threads;
    if (n_ctx > 0) {
        ctxp.n_ctx   = n_ctx;
        ctxp.n_batch = n_ctx;
        n_ctx        = n_ctx;
    }

    llama_context * raw_ctx = llama_init_from_model(model.get(), ctxp);
    if (!raw_ctx) {
        log << "Error: Failed to create context from model\n";
        model.reset();
        return;
    }
    ctx.reset(raw_ctx);

    llama_sampler * raw_smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!raw_smpl) {
        log << "Error: Failed to init sampler chain\n";
        ctx.reset();
        model.reset();
        return;
    }
    smpl.reset(raw_smpl);

    llama_sampler_chain_add(smpl.get(), llama_sampler_init_min_p(min_p, 1));
    llama_sampler_chain_add(smpl.get(), llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(smpl.get(), llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    std::ostringstream oss;
    oss << "Model loaded: " << model_path << " (threads=" << n_threads << ", ctx=" << n_ctx << ")";
    log << oss.str() << "\n";
}

const std::string system_prompt = R"(
You are a tactical decision-making AI for a battlefield simulation.
Your task is to select one valid action for the given agent,
based on the environment, objectives, and faction rules.

You MUST return the result strictly as a JSON object matching this schema:

{
  "agentNextActionType": "string",
  "targetedAgentName": "string",
  "agentStage": "string",
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

Do not include any explanations or text outside this JSON.
)";

std::string LLMInference::response(const std::string & prompt) {
    if (!isInitialized()) {
        log << "Model/context/sampler not initialized\n";
        return R"({"error":"Model not initialized"})";
    }

    // Tạo full prompt theo chuẩn System/User/Assistant
    std::string full_prompt =
        "### System:\n" + system_prompt + "\n\n" + "### User:\n" + prompt + "\n\n" + "### Assistant:\n";

    const bool is_first = llama_memory_seq_pos_max(llama_get_memory(ctx.get()), 0) == -1;

    // Tokenize full prompt
    std::vector<llama_token> prompt_tokens(full_prompt.size());
    int n_tokens = llama_tokenize(nullptr, full_prompt.c_str(), full_prompt.size(), prompt_tokens.data(),
                                  prompt_tokens.size(), is_first, true);
    if (n_tokens <= 0) {
        return R"({"error":"Tokenization failed"})";
    }
    prompt_tokens.resize(n_tokens);

    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());

    std::string response;
    response.reserve(full_prompt.size() * 2);

    std::mutex resp_mutex;
    bool       finished = false;

    auto worker = [&]() {
        while (!finished) {
            int dec = llama_decode(ctx.get(), batch);
            if (dec != 0) {
                log << "Decode failed: " << std::to_string(dec) << "\n";
                finished = true;
                break;
            }

            llama_token new_token = llama_sampler_sample(smpl.get(), ctx.get(), -1);
            if (llama_vocab_is_eog(nullptr, new_token)) {
                finished = true;
                break;
            }

            char buf[512];
            int  n = llama_token_to_piece(nullptr, new_token, buf, sizeof(buf), 0, true);
            if (n > 0) {
                std::lock_guard<std::mutex> lock(resp_mutex);
                response.append(buf, n);
            }

            batch = llama_batch_get_one(&new_token, 1);
        }
    };

    int                      n_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (int i = 0; i < n_threads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto & t : threads) {
        t.join();
    }

    if (response.empty()) {
        return R"({"error":"Empty or no stream data"})";
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(response);
        return parsed.dump();
    } catch (const nlohmann::json::parse_error & e) {
        log << "JSON parse error: " << e.what() << "\n";
        return nlohmann::json{
            {"content", response}
        }.dump();
    } catch (...) {
        log << "Unknown exception in infer\n";
        return nlohmann::json{
            {"content", response}
        }.dump();
    }
}

std::string LLMInference::response(const std::vector<nlohmann::json> & prompts) {
    if (!isInitialized()) {
        log << "generate_json_response: Model/context/sampler not initialized\n";
        return R"({"error":"Model or context not initialized"})";
    }

    std::mutex                            ctx_mutex;
    std::vector<std::future<std::string>> futures(prompts.size());

    for (size_t i = 0; i < prompts.size(); ++i) {
        futures[i] = std::async(std::launch::async, [this, &prompts, i, &ctx_mutex]() -> std::string {
            try {
                // --- Xây dựng full prompt ---
                std::ostringstream oss;
                for (const auto & msg : prompts[i]) {
                    if (!msg.contains("role") || !msg.contains("content")) {
                        continue;
                    }
                    std::string role    = msg["role"].get<std::string>();
                    std::string content = msg["content"].get<std::string>();
                    if (role == "system") {
                        oss << "### System:\n" << content << "\n\n";
                    } else if (role == "user") {
                        oss << "### User:\n" << content << "\n\n";
                    } else if (role == "assistant") {
                        oss << "### Assistant:\n" << content << "\n\n";
                    }
                }
                std::string full_prompt = oss.str();

                // --- Tokenize trước, không lock ---
                const bool is_first = llama_memory_seq_pos_max(llama_get_memory(ctx.get()), 0) == -1;
                int        n_tokens = -llama_tokenize(llama_model_get_vocab(model.get()), full_prompt.c_str(),
                                                      full_prompt.size(), nullptr, 0, is_first, true);
                if (n_tokens <= 0) {
                    return R"({"error":"Tokenization failed"})";
                }

                std::vector<llama_token> tokens(n_tokens);
                int ret = llama_tokenize(llama_model_get_vocab(model.get()), full_prompt.c_str(), full_prompt.size(),
                                         tokens.data(), tokens.size(), is_first, true);
                if (ret <= 0) {
                    return R"({"error":"Tokenization failed"})";
                }
                tokens.resize(ret);

                std::string response;

                // --- Lock chỉ khi decode / sample ---
                {
                    std::lock_guard<std::mutex> lock(ctx_mutex);
                    llama_batch                 batch = llama_batch_get_one(tokens.data(), tokens.size());

                    while (true) {
                        if (llama_decode(ctx.get(), batch) != 0) {
                            return R"({"error":"Decode failed"})";
                        }

                        llama_token tok = llama_sampler_sample(smpl.get(), ctx.get(), -1);
                        if (llama_vocab_is_eog(llama_model_get_vocab(model.get()), tok)) {
                            break;
                        }

                        char buf[512];
                        int  n =
                            llama_token_to_piece(llama_model_get_vocab(model.get()), tok, buf, sizeof(buf), 0, true);
                        if (n < 0) {
                            return R"({"error":"Token conversion failed"})";
                        }
                        response.append(buf, n);

                        batch = llama_batch_get_one(&tok, 1);
                    }
                }

                if (response.empty()) {
                    return R"({"error":"Empty or no stream data"})";
                }

                // --- Parse JSON hoặc wrap text ---
                try {
                    nlohmann::json js = nlohmann::json::parse(response);
                    return js.dump();
                } catch (...) {
                    nlohmann::json js = {
                        {"content", response}
                    };
                    return js.dump();
                }
            } catch (...) {
                log << "Unknown exception in infer\n";
                return R"({"error":"Unknown exception"})";
            }
        });
    }

    // --- Gom kết quả tất cả prompt vào một JSON ---
    nlohmann::json results_json = nlohmann::json::array();
    for (auto & f : futures) {
        results_json.push_back(nlohmann::json::parse(f.get()));
    }

    return results_json.dump();
}

// ========== public infer ==========
std::string LLMInference::infer(const std::string & prompt) {
    try {
        if (is_server_mode) {
            // Gọi inference qua server nếu ở chế độ server
            return response(server_ips, prompt);
        }
        // Gọi inference cục bộ
        return response(prompt);
    } catch (const std::exception & e) {
        log << "Exception in infer: " << e.what() << "\n";
        std::ostringstream o;
        o << R"({"error":"Exception: )" << e.what() << "\"}";
        return o.str();
    } catch (...) {
        log << "Unknown exception in infer \n";
        return R"({"error":"Unknown exception"})";
    }
}

std::string LLMInference::infer(const std::vector<nlohmann::json> & prompts,
                                const nlohmann::json & response_format) {
    try {
        if (is_server_mode) {
            // Gọi inference qua server nếu ở chế độ server
            return response(server_ips, prompts, response_format);
        }
        // Gọi inference cục bộ
        return response(prompts);
    } catch (const std::exception & e) {
        log << "Exception in infer: " << e.what() << "\n";
        std::ostringstream o;
        o << R"({"error":"Exception: )" << e.what() << "\"}";
        return o.str();
    } catch (...) {
        log << "Unknown exception in infer\n";
        return R"({"error":"Unknown exception"})";
    }
}


std::string LLMInference::response(const std::vector<std::string> & ips, const std::string & prompts, int timeout_ms) {
    std::vector<nlohmann::json> formatted_prompts;
    if (!validateAndFormatPrompts(prompts, formatted_prompts)) {      
        return R"({"error":"Invalid prompt format"})";
    }
    return response(ips, formatted_prompts, timeout_ms);
}


std::string LLMInference::response(const std::vector<std::string> &    ips,
                                   const std::vector<nlohmann::json> & prompts,
                                   const nlohmann::json & response_format,
                                   int                                 timeout_ms) {
    log << "🧠 [response] Starting sequential LLM requests...\n";

    if (ips.empty()) {
        log << "❌ No IPs provided.\n";
        return R"({"error":"No IPs provided"})";
    }

    // Dùng atomic<bool> để đảm bảo chỉ một thread set kết quả đầu tiên
    std::atomic<bool> done(false);
    std::mutex        result_mutex;
    std::string       final_result = R"({"error":"No LLM server responded"})";

    // Tạo vector future
    std::vector<std::future<void>> futures;
    futures.reserve(ips.size());

    for (const auto & ip : ips) {
        futures.push_back(std::async(std::launch::async, [&, ip]() {
            try {
                if (done.load(std::memory_order_acquire)) {
                    return;
                }

                log << "➡️ [Thread] Connecting to LLM server: " << ip << "\n";
                std::string result = response(ip, prompts, response_format, timeout_ms);

                // Kiểm tra phản hồi
                nlohmann::json j;
                try {
                    j = nlohmann::json::parse(result);
                } catch (...) {
                    log << "⚠️ [Thread " << ip << "] Invalid JSON, skipping.\n";
                    return;
                }

                // Nếu server trả về JSON hợp lệ, nhận kết quả đầu tiên
                if (!done.exchange(true, std::memory_order_release)) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    final_result = result;
                    log << "✅ [Thread " << ip << "] Accepted as first valid response.\n";
                } else {
                    log << "ℹ️ [Thread " << ip << "] Late response ignored.\n";
                }
            } catch (const std::exception & e) {
                log << "❌ [Thread " << ip << "] Exception: " << e.what() << "\n";
            } catch (...) {
                log << "❌ [Thread " << ip << "] Unknown error.\n";
            }
        }));
    }

    // Chờ tất cả thread kết thúc hoặc có kết quả
    const auto start_time = std::chrono::steady_clock::now();

    while (!done.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time)
                .count();
        if (elapsed > timeout_ms) {
            log << "⏰ Timeout after " << timeout_ms << "ms, no valid response.\n";
            break;
        }
    }

    // Đợi toàn bộ thread hoàn tất để cleanup
    for (auto & f : futures) {
        try {
            f.wait();
        } catch (...) {
        }
    }

    log << "🏁 [response] Finished. Result length = " << final_result.size() << "\n";

    return final_result;
}

struct StreamContext {
    std::string *                                        result;                // Kết quả cuối cùng
    std::string                                          temp_buffer;           // Buffer tạm cho chunk
    std::vector<std::string>                             chunk_buffer;          // Lưu chunk chưa parse
    std::atomic<bool> *                                  first_token_received;  // Cờ TTFT
    std::atomic<std::chrono::steady_clock::time_point> * last_progress_time;    // Thời điểm tiến trình
    bool *                                               done_received;         // Cờ hoàn tất stream
    std::mutex *                                         log_mutex;             // Mutex cho log
    std::ostream *                                       log;                   // Stream log
};

static size_t WriteCallback(void * contents, size_t size, size_t nmemb, void * userp) {
    size_t      realsize = size * nmemb;
    auto *      ctx      = static_cast<StreamContext *>(userp);
    std::string chunk(static_cast<char *>(contents), realsize);

    // Giới hạn buffer
    if (ctx->result->size() + ctx->temp_buffer.size() + chunk.size() > 10 * 1024 * 1024) {
        return 0;  // Ngừng nếu buffer đầy
    }

    // Xử lý stream theo thời gian thực
    size_t pos = 0;
    while (pos < chunk.size()) {
        size_t next_newline = chunk.find('\n', pos);
        if (next_newline == std::string::npos) {
            ctx->temp_buffer += chunk.substr(pos);
            break;
        }
        std::string line = ctx->temp_buffer + chunk.substr(pos, next_newline - pos);
        ctx->temp_buffer.clear();
        pos = next_newline + 1;

        if (line == "data: [DONE]") {
            *(ctx->done_received) = true;
            break;
        }
        if (line.rfind("data: ", 0) == 0) {
            std::string json_data = line.substr(6);
            try {
                nlohmann::json chunk_json = nlohmann::json::parse(json_data);
                if (chunk_json.contains("choices") && chunk_json["choices"].is_array()) {
                    const auto & ch = chunk_json["choices"][0];
                    if (ch.contains("delta") && ch["delta"].contains("content") && !ch["delta"]["content"].is_null()) {
                        ctx->result->append(ch["delta"]["content"].get<std::string>());
                        ctx->first_token_received->store(true);
                    } else if (ch.contains("message") && ch["message"].contains("content") &&
                               !ch["message"]["content"].is_null()) {
                        ctx->result->append(ch["message"]["content"].get<std::string>());
                        ctx->first_token_received->store(true);
                    } else {
                        ctx->chunk_buffer.push_back(json_data);
                    }
                } else {
                    ctx->chunk_buffer.push_back(json_data);
                }
            } catch (...) {
                ctx->chunk_buffer.push_back(json_data);
            }
        } else if (line.find('{') != std::string::npos) {
            try {
                nlohmann::json j = nlohmann::json::parse(line);
                if (j.contains("choices") && j["choices"].is_array()) {
                    const auto & ch = j["choices"][0];
                    if (ch.contains("message") && ch["message"].contains("content") &&
                        !ch["message"]["content"].is_null()) {
                        ctx->result->append(ch["message"]["content"].get<std::string>());
                        ctx->first_token_received->store(true);
                    }
                } else {
                    ctx->chunk_buffer.push_back(line);
                }
            } catch (...) {
                ctx->chunk_buffer.push_back(line);
            }
        }
    }

    ctx->last_progress_time->store(std::chrono::steady_clock::now());
    return realsize;
}

std::string LLMInference::response(const std::string &                 ip,
                                   const std::vector<nlohmann::json> & prompts,
                                   const nlohmann::json &              response_format,
                                   int                                 timeout_ms) {
    try {
        // Khởi tạo biến
        std::string                                        result;
        std::atomic<bool>                                  first_token_received{ false };
        std::atomic<std::chrono::steady_clock::time_point> last_progress_time{ std::chrono::steady_clock::now() };
        bool                                               done_received = false;
        const int base_timeout_ms = timeout_ms*10;
        const int low_speed_time  = base_timeout_ms / 3;
        CURLcode  res             = CURLE_FAILED_INIT;
        long      http_code       = 0;

        // Chuẩn bị prompt
        std::vector<nlohmann::json> formatted_prompts;
        formatted_prompts.reserve(prompts.size());
        if (!validateAndFormatPrompts(prompts, formatted_prompts)) {
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "Invalid prompt format detected\n";
            return R"({"error":"Invalid prompt format"})";
        }
        nlohmann::json              body              = {
            {"model",        "default"        },
            { "messages",    formatted_prompts},
            { "temperature", temperature      },
            { "max_tokens",  n_ctx            },
            { "stream",      true             }
        };
        if (!response_format.empty()) {
            body["response_format"] = response_format;
        }
        std::string body_str = body.dump();

        // Khởi tạo URL
        std::string url = ip + "/v1/chat/completions";
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "🌐 Connecting to LLM server: " << url << " (timeout: " << base_timeout_ms / 1000
                << "s, low_speed_time: " << low_speed_time / 1000 << "s)\n";
            log << "ℹ️ Preparing to execute CURL request with body: " << body_str.substr(0, 256) << "...\n";
        }

        // Thiết lập CURL
        CURL * curl = curl_easy_init();
        if (!curl) {
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "❌ libcurl init failed\n";
            return R"JSON({"error":"libcurl initialization failed"})JSON";
        }

        struct curl_slist * headers = nullptr;
        headers                     = curl_slist_append(headers, "Content-Type: application/json");
        headers                     = curl_slist_append(headers, "Connection: keep-alive");

        StreamContext ctx{
            &result, "", {}, &first_token_received, &last_progress_time, &done_received, &log_mutex, &log
        };
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body_str.size());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, base_timeout_ms / 1000);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 50L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, low_speed_time / 1000);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

        // Thực hiện CURL request
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "ℹ️ CURL perform completed, res=" << curl_easy_strerror(res) << ", HTTP=" << http_code << "\n";
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        // Kiểm tra lỗi kết nối
        if (res != CURLE_OK || http_code != 200) {
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "❌ Request failed: CURL=" << curl_easy_strerror(res) << ", HTTP=" << http_code << "\n";
            if (!result.empty()) {
                nlohmann::json wrapped = {
                    {"content", result                                      },
                    { "error",  "Partial response due to connection failure"}
                };
                return wrapped.dump();
            }
            return R"JSON({"error":"Connection failed or invalid HTTP response"})JSON";
        }

        // Xử lý chunk chưa parse
        if (!ctx.chunk_buffer.empty()) {
            std::string combined_chunks;
            for (const auto & chunk : ctx.chunk_buffer) {
                combined_chunks += chunk + "\n";
            }
            try {
                nlohmann::json combined_json = nlohmann::json::parse(combined_chunks);
                if (combined_json.contains("choices") && combined_json["choices"].is_array()) {
                    const auto & ch = combined_json["choices"][0];
                    if (ch.contains("delta") && ch["delta"].contains("content") && !ch["delta"]["content"].is_null()) {
                        result += ch["delta"]["content"].get<std::string>();
                        first_token_received = true;
                    } else if (ch.contains("message") && ch["message"].contains("content") &&
                               !ch["message"]["content"].is_null()) {
                        result += ch["message"]["content"].get<std::string>();
                        first_token_received = true;
                    }
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(log_mutex);
                log << "⚠️ Failed to parse combined chunks: " << combined_chunks.substr(0, 256) << "...\n";
            }
        }

        // Xử lý temp_buffer còn lại
        if (!ctx.temp_buffer.empty()) {
            try {
                nlohmann::json temp_json = nlohmann::json::parse(ctx.temp_buffer);
                if (temp_json.contains("choices") && temp_json["choices"].is_array()) {
                    const auto & ch = temp_json["choices"][0];
                    if (ch.contains("delta") && ch["delta"].contains("content") && !ch["delta"]["content"].is_null()) {
                        result += ch["delta"]["content"].get<std::string>();
                        first_token_received = true;
                    } else if (ch.contains("message") && ch["message"].contains("content") &&
                               !ch["message"]["content"].is_null()) {
                        result += ch["message"]["content"].get<std::string>();
                        first_token_received = true;
                    }
                }
            } catch (...) {
                ctx.chunk_buffer.push_back(ctx.temp_buffer);
                std::lock_guard<std::mutex> lock(log_mutex);
                log << "⚠️ Failed to parse temp buffer: " << ctx.temp_buffer.substr(0, 256) << "...\n";
            }
        }

        // Kiểm tra kết quả
        if (result.empty()) {
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "⚠️ Empty result from " << ip << "\n";
            return R"JSON({"error":"Empty result or malformed data"})JSON";
        }

        {
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "ℹ️ Plain text response from " << ip << ": " << result.substr(0, 512) << "...\n";
            if (!done_received) {
                log << "⚠️ Response incomplete, missing [DONE], returning partial result\n";
            }
        }

        // Thử parse JSON
        try {
            nlohmann::json              jres = nlohmann::json::parse(result);
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "✅ Received valid JSON from " << ip << "\n";
            log << "🏁 [response] Finished. Result length = " << result.size() << "\n";
            return jres.dump();
        } catch (...) {
            nlohmann::json wrapped = {
                {"content", result}
            };
            if (!done_received) {
                wrapped["warning"] = "Partial response, server may not have completed";
            }
            std::lock_guard<std::mutex> lock(log_mutex);
            log << "✅ [response] Accepted partial response as JSON\n";
            log << "🏁 [response] Finished. Result length = " << result.size() << "\n";
            return wrapped.dump();
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(log_mutex);
        log << "❗ Unknown exception in response (" << ip << ")\n";
        return R"JSON({"error":"Unknown exception during inference"})JSON";
    }
}
