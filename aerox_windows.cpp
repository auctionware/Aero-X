#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// Official RandomX C-API Header
#include <randomx.h>

struct StratumJob {
    std::string job_id;
    std::string blob;
    std::string seed_hash;
    uint64_t target_diff{0};
    bool active{false};
};

std::atomic<bool> g_running(true);
std::atomic<uint64_t> g_hash_count(0);
std::atomic<uint64_t> g_share_count(0);

std::mutex g_job_mutex;
StratumJob g_current_job;

SOCKET g_socket_fd = INVALID_SOCKET;
std::mutex g_socket_mutex;

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(strtol(hex.substr(i, 2).c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

std::string extract_json_field(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\":";
    size_t start = json.find(key);
    if (start == std::string::npos) return "";
    start += key.length();
    while (start < json.length() && (json[start] == ' ' || json[start] == '\"')) {
        if (json[start] == '\"') { start++; break; }
        start++;
    }
    size_t end = start;
    while (end < json.length() && json[end] != '\"' && json[end] != ',' && json[end] != '}' && json[end] != ']') {
        end++;
    }
    return json.substr(start, end - start);
}

void stratum_net_thread(const std::string& host, int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Network] WSAStartup failed.\n";
        return;
    }

    g_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_socket_fd == INVALID_SOCKET) {
        std::cerr << "[Network] Failed to create socket: " << WSAGetLastError() << "\n";
        WSACleanup();
        return;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr);

    std::cout << "[Network] Connecting to P2Pool stratum at " << host << ":" << port << "...\n";
    if (connect(g_socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "[Network] Connection failed. Make sure P2Pool is running on port " << port << ".\n";
        closesocket(g_socket_fd);
        WSACleanup();
        g_running = false;
        return;
    }

    std::cout << "[Network] Connected! Sending Stratum login payload...\n";
    std::string login_req = "{\"id\":1, \"method\":\"login\", \"params\":{\"login\":\"x+10000\", \"pass\":\"aerox\", \"agent\":\"AeroX-Windows/2.0\"}}\n";
    
    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        send(g_socket_fd, login_req.c_str(), (int)login_req.length(), 0);
    }

    std::string read_buffer;
    char buffer[4096];

    while (g_running) {
        int valread = recv(g_socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (valread <= 0) {
            std::cout << "[Network] Connection closed by pool.\n";
            break;
        }
        buffer[valread] = '\0';
        read_buffer.append(buffer);

        size_t pos;
        while ((pos = read_buffer.find('\n')) != std::string::npos) {
            std::string line = read_buffer.substr(0, pos);
            read_buffer.erase(0, pos + 1);

            if (line.find("\"job\"") != std::string::npos || line.find("\"method\":\"job\"") != std::string::npos) {
                std::string j_id = extract_json_field(line, "job_id");
                std::string blob = extract_json_field(line, "blob");
                std::string seed = extract_json_field(line, "seed_hash");
                std::string target_hex = extract_json_field(line, "target");

                if (!blob.empty() && !j_id.empty()) {
                    std::lock_guard<std::mutex> lock(g_job_mutex);
                    g_current_job.job_id = j_id;
                    g_current_job.blob = blob;
                    g_current_job.seed_hash = seed;
                    
                    if (!target_hex.empty()) {
                        uint32_t t32 = strtoul(target_hex.c_str(), nullptr, 16);
                        g_current_job.target_diff = (t32 == 0) ? 0xFFFFFFFFFFFFFFFFULL : (0xFFFFFFFFULL / t32);
                    } else {
                        g_current_job.target_diff = 10000;
                    }

                    g_current_job.active = true;
                    std::cout << "\n[Network] Job received -> ID: " << j_id << " | Target Diff: " << g_current_job.target_diff << "\n";
                }
            }
        }
    }

    closesocket(g_socket_fd);
    WSACleanup();
}

void submit_share(const std::string& job_id, uint32_t nonce, const std::string& result_hex) {
    std::stringstream ss;
    ss << "{\"id\":2, \"method\":\"submit\", \"params\":{"
       << "\"job_id\":\"" << job_id << "\", "
       << "\"nonce\":\"" << std::hex << std::setfill('0') << std::setw(8) << nonce << "\", "
       << "\"result\":\"" << result_hex << "\"}}\n";

    std::string payload = ss.str();
    
    std::lock_guard<std::mutex> lock(g_socket_mutex);
    if (g_socket_fd != INVALID_SOCKET) {
        send(g_socket_fd, payload.c_str(), (int)payload.length(), 0);
        g_share_count++;
        std::cout << "\n[AeroX] Share submitted! Nonce: 0x" << std::hex << nonce << "\n";
    }
}

void worker_thread_routine(int thread_id, int core_id, randomx_dataset* dataset, randomx_flags flags) {
    // Pin thread to specific core using Windows API
    DWORD_PTR mask = (1ULL << (core_id % 64));
    SetThreadAffinityMask(GetCurrentThread(), mask);

    randomx_vm* vm = randomx_create_vm(flags, nullptr, dataset);
    if (!vm) return;

    uint32_t nonce = thread_id * 0x10000000;
    uint8_t hash_output[RANDOMX_HASH_SIZE];
    StratumJob local_job;

    while (g_running) {
        {
            std::lock_guard<std::mutex> lock(g_job_mutex);
            if (!g_current_job.active) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            local_job = g_current_job;
        }

        std::vector<uint8_t> block_blob = hex_to_bytes(local_job.blob);
        if (block_blob.size() < 43) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::memcpy(&block_blob[39], &nonce, sizeof(nonce));

        randomx_calculate_hash(vm, block_blob.data(), block_blob.size(), hash_output);
        g_hash_count++;
        nonce++;

        uint64_t hash_val;
        std::memcpy(&hash_val, &hash_output[24], sizeof(hash_val));

        if (hash_val < local_job.target_diff) {
            std::string result_hex = bytes_to_hex(hash_output, RANDOMX_HASH_SIZE);
            submit_share(local_job.job_id, nonce - 1, result_hex);
        }
    }

    randomx_destroy_vm(vm);
}

int main() {
    std::cout << "===========================================\n";
    std::cout << "               AeroX Core v1.0             \n";
    std::cout << "           github.com/auctionware          \n";
    std::cout << "===========================================\n";

    randomx_flags flags = randomx_get_flags();
    flags |= RANDOMX_FLAG_JIT;
    flags |= RANDOMX_FLAG_HARD_AES;
    flags |= RANDOMX_FLAG_LARGE_PAGES;
    flags |= RANDOMX_FLAG_FULL_MEM;

    std::cout << "[Init] Allocating ~2.08 GiB RandomX v2 dataset cache...\n";
    randomx_dataset* dataset = randomx_alloc_dataset(flags);
    if (!dataset) {
        std::cout << "[Warning] Large Pages allocation failed. Falling back to default pages...\n";
        flags = static_cast<randomx_flags>(static_cast<int>(flags) & ~RANDOMX_FLAG_JIT);
        dataset = randomx_alloc_dataset(flags);
    }

    std::string seed = "P2Pool_Seed_Key_Init";
    randomx_cache* cache = randomx_alloc_cache(flags);
    randomx_init_cache(cache, seed.data(), seed.length());
    
    uint32_t dataset_item_count = randomx_dataset_item_count();
    randomx_init_dataset(dataset, cache, 0, dataset_item_count);
    randomx_release_cache(cache);

    std::cout << "[Init] Dataset initialized successfully.\n";

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads > 2) num_threads -= 1;

    std::thread net_thread(stratum_net_thread, "127.0.0.1", 3333);

    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    auto start_time = std::chrono::steady_clock::now();
    for (unsigned int i = 0; i < num_threads; ++i) {
        workers.emplace_back(worker_thread_routine, i, i, dataset, flags);
    }

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - start_time;
        
        uint64_t hashes = g_hash_count.load();
        double hashrate = hashes / elapsed.count();

        std::cout << "[Telemetry] Hashrate: " << std::fixed << std::setprecision(2) << hashrate 
                  << " H/s | Total Hashes: " << hashes 
                  << " | Valid Shares: " << g_share_count.load() << "\r" << std::flush;
    }

    if (net_thread.joinable()) net_thread.join();
    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    randomx_release_dataset(dataset);
    return 0;
}
