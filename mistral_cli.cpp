/*
Copyright (C) 2026 Leith Ketchell

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://gnu.org>.
*/

#include <iostream>
#include <stdexcept>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <set>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <locale>
#include <codecvt>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <array>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
inline int setenv(const char* name, const char* value, int) {
    return SetEnvironmentVariable(name, value) ? 0 : -1;
}
#else
#include <unistd.h>
#include <sys/utsname.h>
#endif

#if defined(__linux__)
#define MISTRAL_CLI_OS "Linux"
#elif defined(__APPLE__)
#define MISTRAL_CLI_OS "macOS"
#elif defined(_WIN32)
#define MISTRAL_CLI_OS "Windows"
#else
#define MISTRAL_CLI_OS "Unknown"
#endif

#include "mistral_cli.h"

bool debugMode = false;
const int MAX_RETRIES = 3;
const int INITIAL_RETRY_DELAY_MS = 1000;
std::chrono::steady_clock::time_point lastApiCallTime;
const int MIN_API_DELAY_MS = 1000;

std::string currentModel = "mistral-large-latest";
double currentTemperature = 0.7;
int currentMaxTokens = 0;
int currentRandomSeed = 0;
std::set<std::string> g_allowlist;
bool g_allowlistLoaded = false;
std::set<std::string> g_processedFiles;

// Spinner utility for indeterminate progress
void showSpinner(const std::string& message) {
    static const char spinner[] = {'|', '/', '-', '\\'};
    static int spinnerPos = 0;
    std::cout << "\r" << message << " " << spinner[spinnerPos % 4] << std::flush;
    spinnerPos++;
}
// Helper function to extract JSON from hybrid responses (mixture of simple plaintext, markdown-wrapped JSON and plaintext JSON)
json extractJsonFromHybridResponse(const std::string& contentStr) {
    // Trim whitespace from the content
    std::string trimmedContent = contentStr;
    trimmedContent.erase(0, trimmedContent.find_first_not_of(" \n\r\t"));
    trimmedContent.erase(trimmedContent.find_last_not_of(" \n\r\t") + 1);

    // --- 1. Handle Markdown-Wrapped JSON (e.g., ```json ... ```) ---
    size_t markdownJsonStart = trimmedContent.find("```json");
    size_t markdownJsonEnd = trimmedContent.rfind("```");
    if (markdownJsonStart != std::string::npos && markdownJsonEnd != std::string::npos && markdownJsonEnd > markdownJsonStart) {
        std::string jsonPart = trimmedContent.substr(markdownJsonStart + 7, markdownJsonEnd - (markdownJsonStart + 7));
        jsonPart.erase(0, jsonPart.find_first_not_of(" \n\r\t"));
        jsonPart.erase(jsonPart.find_last_not_of(" \n\r\t") + 1);

        try {
            json content = json::parse(jsonPart);
            if (content.is_object()) {
                // Add plaintext before the JSON as a "message" field
                if (markdownJsonStart > 0) {
                    std::string plaintext = trimmedContent.substr(0, markdownJsonStart);
                    plaintext.erase(0, plaintext.find_first_not_of(" \n\r\t"));
                    plaintext.erase(plaintext.find_last_not_of(" \n\r\t") + 1);
                    if (!plaintext.empty()) {
                        content["message"] = plaintext;
                    }
                }
                return content;
            }
        } catch (const json::parse_error& e) {
         // Parsing failures are too important to hide behind the -debug switch
         //   if (debugMode) {
                std::cerr << "[extractJsonFromHybridResponse] JSON parse failed for markdown-wrapped JSON: " << e.what() << "\n";
                std::cerr << "[extractJsonFromHybridResponse] JSON part: " << jsonPart << "\n";
         //   }
        }
    }

    // --- 2. Handle Hybrid Responses (Plaintext + plaintext JSON separated by \n---\n) ---
    size_t separatorPos = trimmedContent.find("\n---\n");
    if (separatorPos != std::string::npos) {
        std::string plaintext = trimmedContent.substr(0, separatorPos);
        std::string jsonPart = trimmedContent.substr(separatorPos + 5);

        plaintext.erase(0, plaintext.find_first_not_of(" \n\r\t"));
        plaintext.erase(plaintext.find_last_not_of(" \n\r\t") + 1);
        jsonPart.erase(0, jsonPart.find_first_not_of(" \n\r\t"));
        jsonPart.erase(jsonPart.find_last_not_of(" \n\r\t") + 1);

        try {
            json content = json::parse(jsonPart);
            if (content.is_object()) {
                content["message"] = plaintext;
                return content;
            }
        } catch (const json::parse_error& e) {
            if (debugMode) {
                std::cerr << "[DEBUG] JSON parse failed for hybrid response: " << e.what() << "\n";
                std::cerr << "[DEBUG] JSON part: " << jsonPart << "\n";
            }
        }
    }

    // --- 3. Try to parse the entire content as JSON ---
    try {
        json content = json::parse(trimmedContent);
        if (content.is_object()) {
            return content;
        }
    } catch (const json::parse_error& e) {
        } catch (const json::parse_error& e) {
         // Parsing failures are too important to hide behind the -debug switch
         //  if (debugMode) {
            std::cerr << "[DEBUG] JSON parse failed for entire content: " << e.what() << "\n";
            std::cerr << "[DEBUG] Content: " << trimmedContent << "\n";
        // }
    }

    // --- 4. Fallback: Return as plaintext in a JSON object ---
    return json::object({{"answer", trimmedContent}});
}

// --- Git Support Functions ---
std::string executeShellCommand(const std::string& command) {
    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to execute command: " + command);
    }
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

bool isDestructiveGitCommand(const std::string& command) {
    const std::vector<std::string> destructiveCommands = {
        "commit", "push", "pull", "reset", "checkout", "merge", "rebase", "stash pop"
    };
    for (const auto& cmd : destructiveCommands) {
        if (command.find(cmd) == 0) {
            return true;
        }
    }
    return false;
}

bool isDestructiveShellCommand(const std::string& command) {
    const std::vector<std::string> destructiveCommands = {
        "rm", "dd", "mv", "cp", "chmod", "chown", "mkfs", "fdisk", "format"
    };
    for (const auto& cmd : destructiveCommands) {
        if (command.find(cmd) == 0 || command.find(" " + cmd) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string getGitContext() {
    std::string context;
    try {
        std::string branch = executeShellCommand("git rev-parse --abbrev-ref HEAD 2>/dev/null");
        if (branch.empty()) {
            return "[Git: Not a repository]";
        }
        std::string dirtyFiles = executeShellCommand("git status --porcelain | wc -l");
        std::string stagedFiles = executeShellCommand("git diff --cached --name-only | wc -l");
        std::string lastCommit = executeShellCommand("git log -1 --pretty=format:'%s' 2>/dev/null");
        dirtyFiles.erase(dirtyFiles.find_last_not_of(" \n\r\t") + 1);
        stagedFiles.erase(stagedFiles.find_last_not_of(" \n\r\t") + 1);
        lastCommit.erase(lastCommit.find_last_not_of(" \n\r\t") + 1);
        context = "[Git: branch=" + branch + " | dirty=" + dirtyFiles + " files | staged=" + stagedFiles + " files | last_commit=\\\"" + lastCommit + "\\\"]";
    }
    catch (...) {
        context = "[Git: Error retrieving context]";
    }
    return context;
}

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \n\r\t");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \n\r\t");
    return str.substr(start, end - start + 1);
}

// --- MD5 Hashing (Lightweight Implementation) ---
class MD5 {
public:
    typedef unsigned int size_type;

    MD5();
    MD5(const std::string& text);
    MD5(const char* text);
    MD5(std::istream& is);

    void update(const unsigned char* buf, size_type length);
    void update(const char* buf, size_type length);
    void update(std::istream& is);
    std::string digest();
    std::string hexdigest() const;
    friend std::ostream& operator<<(std::ostream&, MD5 md5);

private:
    void finalize();
    void init();
    typedef unsigned char uint1;
    typedef unsigned int uint4;
    enum { blocksize = 64 };

    void transform(const uint1 block[blocksize]);
    static void decode(uint4 output[], const uint1 input[], size_type len);
    static void encode(uint1 output[], const uint4 input[], size_type len);

    bool finalized;
    uint1 buffer[blocksize];
    uint4 count[2];
    uint4 state[4];
    uint1 digest_[16];

    static inline uint4 F(uint4 x, uint4 y, uint4 z) { return (x & y) | (~x & z); }
    static inline uint4 G(uint4 x, uint4 y, uint4 z) { return (x & z) | (y & ~z); }
    static inline uint4 H(uint4 x, uint4 y, uint4 z) { return x ^ y ^ z; }
    static inline uint4 I(uint4 x, uint4 y, uint4 z) { return y ^ (x | ~z); }
    static inline uint4 rotate_left(uint4 x, int n) { return (x << n) | (x >> (32 - n)); }
    static inline void FF(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac) {
        a = rotate_left(a + F(b, c, d) + x + ac, s) + b;
    }
    static inline void GG(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac) {
        a = rotate_left(a + G(b, c, d) + x + ac, s) + b;
    }
    static inline void HH(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac) {
        a = rotate_left(a + H(b, c, d) + x + ac, s) + b;
    }
    static inline void II(uint4& a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac) {
        a = rotate_left(a + I(b, c, d) + x + ac, s) + b;
    }
};

MD5::MD5() { init(); }

MD5::MD5(const std::string& text) {
    init();
    update(text.c_str(), text.length());
    finalize();
}

MD5::MD5(const char* text) {
    init();
    update(text, strlen(text));
    finalize();
}

MD5::MD5(std::istream& is) {
    init();
    update(is);
    finalize();
}

void MD5::init() {
    finalized = false;
    count[0] = 0;
    count[1] = 0;
    state[0] = 0x67452301;
    state[1] = 0xefcdab89;
    state[2] = 0x98badcfe;
    state[3] = 0x10325476;
}

void MD5::update(const unsigned char* buf, size_type length) {
    uint4 i, index, partLen;
    if (finalized) return;
    index = (count[0] >> 3) & 0x3F;
    if ((count[0] += (length << 3)) < (length << 3)) count[1]++;
    count[1] += (length >> 29);
    partLen = 64 - index;
    if (length >= partLen) {
        memcpy(&buffer[index], buf, partLen);
        transform(buffer);
        for (i = partLen; i + 63 < length; i += 64) transform(&buf[i]);
        index = 0;
    } else i = 0;
    if (length - i) memcpy(&buffer[index], &buf[i], length - i);
}

void MD5::update(const char* buf, size_type length) {
    update((const unsigned char*)buf, length);
}

void MD5::update(std::istream& is) {
    char buf[64];
    while (is.good() && !is.eof()) {
        is.read(buf, 64);
        update(buf, is.gcount());
    }
}

void MD5::transform(const uint1 block[blocksize]) {
    uint4 a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    decode(x, block, blocksize);

    // Round 1
    FF(a, b, c, d, x[0], 7, 0xd76aa478);
    FF(d, a, b, c, x[1], 12, 0xe8c7b756);
    FF(c, d, a, b, x[2], 17, 0x242070db);
    FF(b, c, d, a, x[3], 22, 0xc1bdceee);
    FF(a, b, c, d, x[4], 7, 0xf57c0faf);
    FF(d, a, b, c, x[5], 12, 0x4787c62a);
    FF(c, d, a, b, x[6], 17, 0xa8304613);
    FF(b, c, d, a, x[7], 22, 0xfd469501);
    FF(a, b, c, d, x[8], 7, 0x698098d8);
    FF(d, a, b, c, x[9], 12, 0x8b44f7af);
    FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    FF(b, c, d, a, x[11], 22, 0x895cd7be);
    FF(a, b, c, d, x[12], 7, 0x6b901122);
    FF(d, a, b, c, x[13], 12, 0xfd987193);
    FF(c, d, a, b, x[14], 17, 0xa679438e);
    FF(b, c, d, a, x[15], 22, 0x49b40821);

    // Round 2
    GG(a, b, c, d, x[1], 5, 0xf61e2562);
    GG(d, a, b, c, x[6], 9, 0xc040b340);
    GG(c, d, a, b, x[11], 14, 0x265e5a51);
    GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
    GG(a, b, c, d, x[5], 5, 0xd62f105d);
    GG(d, a, b, c, x[10], 9, 0x02441453);
    GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
    GG(a, b, c, d, x[9], 5, 0x21e1cde6);
    GG(d, a, b, c, x[14], 9, 0xc33707d6);
    GG(c, d, a, b, x[3], 14, 0xf4d50d87);
    GG(b, c, d, a, x[8], 20, 0x455a14ed);
    GG(a, b, c, d, x[13], 5, 0xa9e3e905);
    GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
    GG(c, d, a, b, x[7], 14, 0x676f02d9);
    GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    // Round 3
    HH(a, b, c, d, x[5], 4, 0xfffa3942);
    HH(d, a, b, c, x[8], 11, 0x8771f681);
    HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    HH(b, c, d, a, x[14], 23, 0xfde5380c);
    HH(a, b, c, d, x[1], 4, 0xa4beea44);
    HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
    HH(c, d, a, b, x[7], 16, 0xf6bb4b60);
    HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    HH(a, b, c, d, x[13], 4, 0x289b7ec6);
    HH(d, a, b, c, x[0], 11, 0xeaa127fa);
    HH(c, d, a, b, x[3], 16, 0xd4ef3085);
    HH(b, c, d, a, x[6], 23, 0x04881d05);
    HH(a, b, c, d, x[9], 4, 0xd9d4d039);
    HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    HH(b, c, d, a, x[2], 23, 0xc4ac5665);

    // Round 4
    II(a, b, c, d, x[0], 6, 0xf4292244);
    II(d, a, b, c, x[7], 10, 0x432aff97);
    II(c, d, a, b, x[14], 15, 0xab9423a7);
    II(b, c, d, a, x[5], 21, 0xfc93a039);
    II(a, b, c, d, x[12], 6, 0x655b59c3);
    II(d, a, b, c, x[3], 10, 0x8f0ccc92);
    II(c, d, a, b, x[10], 15, 0xffeff47d);
    II(b, c, d, a, x[1], 21, 0x85845dd1);
    II(a, b, c, d, x[8], 6, 0x6fa87e4f);
    II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    II(c, d, a, b, x[6], 15, 0xa3014314);
    II(b, c, d, a, x[13], 21, 0x4e0811a1);
    II(a, b, c, d, x[4], 6, 0xf7537e82);
    II(d, a, b, c, x[11], 10, 0xbd3af235);
    II(c, d, a, b, x[2], 15, 0x2ad7d2bb);
    II(b, c, d, a, x[9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    memset(buffer, 0, sizeof(buffer));
}

void MD5::finalize() {
    static unsigned char padding[64] = {
        0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    unsigned char bits[8];
    encode(bits, count, 8);
    size_type index = (count[0] >> 3) & 0x3f;
    size_type padLen = (index < 56) ? (56 - index) : (120 - index);
    update(padding, padLen);
    update(bits, 8);
    encode(digest_, state, 16);
    finalized = true;
}

std::string MD5::hexdigest() const {
    if (!finalized) return "";
    char buf[33];
    for (int i = 0; i < 16; i++)
        sprintf(buf + i * 2, "%02x", digest_[i]);
    buf[32] = 0;
    return std::string(buf);
}

void MD5::decode(uint4 output[], const uint1 input[], size_type len) {
    for (unsigned int i = 0, j = 0; j < len; i++, j += 4)
        output[i] = ((uint4)input[j]) |
                    (((uint4)input[j + 1]) << 8) |
                    (((uint4)input[j + 2]) << 16) |
                    (((uint4)input[j + 3]) << 24);
}

void MD5::encode(uint1 output[], const uint4 input[], size_type len) {
    for (size_type i = 0, j = 0; j < len; i++, j += 4) {
        output[j] = input[i] & 0xff;
        output[j + 1] = (input[i] >> 8) & 0xff;
        output[j + 2] = (input[i] >> 16) & 0xff;
        output[j + 3] = (input[i] >> 24) & 0xff;
    }
}

// --- Obfuscation Helper Functions ---
std::string generateSystemHash() {
    std::string hostname;
#ifdef _WIN32
    char hostnameBuf[256];
    DWORD size = sizeof(hostnameBuf);
    GetComputerNameA(hostnameBuf, &size);
    hostname = hostnameBuf;
#else
    struct utsname sysInfo;
    uname(&sysInfo);
    hostname = sysInfo.nodename;
#endif

    std::string username;
#ifdef _WIN32
    char usernameBuf[256];
    DWORD size = sizeof(usernameBuf);
    GetUserNameA(usernameBuf, &size);
    username = usernameBuf;
#else
    const char* user = getenv("USER");
    if (user) username = user;
    else username = "unknown";
#endif

    std::string systemId = hostname + username + "mistral_cli";
    MD5 md5(systemId);
    return md5.hexdigest();
}

std::string obfuscateString(const std::string& input, const std::string& key) {
    std::string output = input;
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] ^= key[i % key.size()];
    }
    return output;
}

namespace fs = std::filesystem;

fs::path getEncryptedApiKeyPath() {
    fs::path configDir;
#ifdef _WIN32
    const char* appData = getenv("APPDATA");
    if (appData) {
        configDir = fs::path(appData) / "minstral";
    } else {
        configDir = fs::current_path() / ".minstral";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        configDir = fs::path(home) / ".minstral";
    } else {
        configDir = fs::current_path() / ".minstral";
    }
#endif
    return configDir / "api_key.enc";
}

std::string getApiKey() {
    fs::path encryptedKeyPath = getEncryptedApiKeyPath();
    std::string systemHash = generateSystemHash();

    // 1. Try to load and decrypt an existing local key file
    if (fs::exists(encryptedKeyPath)) {
        std::ifstream in(encryptedKeyPath, std::ios::binary);
        if (in) {
            std::string obfuscatedKey((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string apiKey = obfuscateString(obfuscatedKey, systemHash);
            if (!apiKey.empty() && apiKey != "YOUR_DEVELOPER_KEY_HERE") {
                setenv("MISTRAL_API_KEY", apiKey.c_str(), 1);
                return apiKey;
            }
        }
    }

    // 2. Clear out corrupt or default placeholder files
    if (fs::exists(encryptedKeyPath)) {
        std::error_code ec;
        fs::remove(encryptedKeyPath, ec);
    }

    // 3. Prompt user for API key
    std::string key;
    std::cout << "Mistral API Key not set or invalid.\nAPI key: ";
    std::getline(std::cin, key);
    key = trim(key);

    if (key.empty()) {
        throw std::runtime_error("FATAL: No API key provided.");
    }

    // 4. Obfuscate and save the new key
    std::string obfuscatedKey = obfuscateString(key, systemHash);
    fs::path configDir = encryptedKeyPath.parent_path();
    if (!fs::exists(configDir)) {
        fs::create_directories(configDir);
    }

    std::ofstream out(encryptedKeyPath, std::ios::binary);
    if (out) {
        out << obfuscatedKey;
    } else {
        throw std::runtime_error("FATAL: Failed to save the API key configuration.");
    }

    setenv("MISTRAL_API_KEY", key.c_str(), 1);
    return key;
}

void showProgress(size_t current, size_t total, const std::string& message) {
    if (!debugMode && !message.empty()) {
        float percent = (float)current / total * 100;
        std::cout << "\r" << message << " [" << std::string(50 * percent / 100, '=')
                  << std::string(50 - 50 * percent / 100, ' ') << "] " << percent << "%" << std::flush;
    }
}

fs::path allowlistPath() {
    return fs::weakly_canonical(fs::current_path() / ".minstral" / "allowlist.json");
}

void loadAllowlist() {
    if (g_allowlistLoaded) return;
    g_allowlistLoaded = true;
    fs::path p = allowlistPath();
    std::error_code ec;
    if (!fs::exists(p, ec)) return;
    std::ifstream in(p, std::ios::binary);
    if (!in) return;
    try {
        json data = json::parse(in);
        if (!data.is_array()) {
            if (debugMode) {
                std::cerr << "[DEBUG] [loadAllowlist] Allowlist JSON is not an array: " << data.dump() << "\n";
            }
            return;
        }
        for (const auto& e : data) {
            if (e.is_string()) g_allowlist.insert(e.get<std::string>());
        }
    } catch (const json::parse_error& e) {
        if (debugMode) {
            std::cerr << "[DEBUG] [loadAllowlist] JSON parse failed for allowlist at byte " << e.byte
                      << ": " << e.what() << " (Context: Parsing allowlist file)\n";
        }
    } catch (const std::exception& e) {
        if (debugMode) {
            std::cerr << "[DEBUG] [loadAllowlist] Unexpected error loading allowlist: " << e.what() << "\n";
        }
    }
}

void saveAllowlist() {
    fs::path p = allowlistPath();
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary);
    if (!out) return;
    json arr = json::array();
    for (const auto& s : g_allowlist) arr.push_back(s);
    out << arr.dump(2);
}

void parseArguments(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-debug") {
            debugMode = true;
        } else if (arg == "--model" && i + 1 < argc) {
            currentModel = argv[++i];
        } else if (arg == "--temperature" && i + 1 < argc) {
            currentTemperature = std::stod(argv[++i]);
        } else if (arg == "--max-tokens" && i + 1 < argc) {
            currentMaxTokens = std::stoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            currentRandomSeed = std::stoi(argv[++i]);
        }
    }
}

void logJsonExchange(const std::string& label, const json& data, bool isUpstream) {
    if (debugMode) {
        std::string direction = isUpstream ? "UPSTREAM" : "DOWNSTREAM";
        std::cout << "[DEBUG] [" << direction << "] " << label << ": " << data.dump() << "\n";
    }

    std::ofstream log("json_exchanges.log", std::ios::app);
    if (!log) return;
    json logEntry = {
        {"timestamp", std::time(nullptr)},
        {"label", label},
        {"direction", isUpstream ? "upstream" : "downstream"},
        {"data", data}
    };
    log << logEntry.dump() << "\n";
}

std::string getCwd() {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        return std::string(buf);
    }
    return "<unknown>";
}

std::string buildSystemPrompt() {
    std::string prompt = SYSTEM_PROMPT_STATIC;
    size_t cwdPos = prompt.find("{CWD}");
    if (cwdPos != std::string::npos) {
        prompt.replace(cwdPos, 5, getCwd());
    }

    std::string gitContext = getGitContext();
    size_t gitPos = prompt.find("{GIT_CONTEXT}");
    if (gitPos != std::string::npos) {
        prompt.replace(gitPos, 13, gitContext);
    } else {
        prompt += " " + gitContext;
    }

    return prompt;
}

bool isLikelyBinary(const std::string& content) {
    if (content.find('\0') != std::string::npos) return true;
    for (char c : content) {
        if (static_cast<unsigned char>(c) < 32 &&
            c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != '\v') {
            return true;
        }
    }
    const std::vector<std::string> binaryHeaders = {
        std::string("\x7F", 1) + "ELF", "MZ",
        std::string("\x89", 1) + "PNG", std::string("\xFF\xD8\xFF", 3),
        "GIF87a", "GIF89a", "ID3", "RIFF", std::string("\x42\x4D", 2)
    };
    for (const auto& header : binaryHeaders) {
        if (content.rfind(header, 0) == 0) return true;
    }
    return false;
}

Encoding detectEncoding(const std::string& content) {
    if (content.empty()) return Encoding::ASCII;
    if (content.size() >= 2) {
        if (static_cast<uint8_t>(content[0]) == 0xFF && static_cast<uint8_t>(content[1]) == 0xFE) return Encoding::UTF16_LE;
        if (static_cast<uint8_t>(content[0]) == 0xFE && static_cast<uint8_t>(content[1]) == 0xFF) return Encoding::UTF16_BE;
    }
    if (content.size() >= 3 &&
        static_cast<uint8_t>(content[0]) == 0xEF &&
        static_cast<uint8_t>(content[1]) == 0xBB &&
        static_cast<uint8_t>(content[2]) == 0xBF) return Encoding::UTF8;
    if (isLikelyBinary(content)) return Encoding::BINARY;
    bool isUTF8 = true;
    for (size_t i = 0; i < content.size(); ++i) {
        uint8_t c = static_cast<uint8_t>(content[i]);
        if (c <= 0x7F) continue;
        else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= content.size() || (static_cast<uint8_t>(content[i + 1]) & 0xC0) != 0x80) { isUTF8 = false; break; }
            i += 1;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= content.size() || (static_cast<uint8_t>(content[i + 1]) & 0xC0) != 0x80 || (static_cast<uint8_t>(content[i + 2]) & 0xC0) != 0x80) { isUTF8 = false; break; }
            i += 2;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= content.size() || (static_cast<uint8_t>(content[i + 1]) & 0xC0) != 0x80 || (static_cast<uint8_t>(content[i + 2]) & 0xC0) != 0x80 || (static_cast<uint8_t>(content[i + 3]) & 0xC0) != 0x80) { isUTF8 = false; break; }
            i += 3;
        } else { isUTF8 = false; break; }
    }
    if (isUTF8) return Encoding::UTF8;
    bool likelyUTF16LE = true, likelyUTF16BE = true;
    for (size_t i = 0; i < content.size(); i += 2) {
        if (i + 1 < content.size()) {
            if (static_cast<uint8_t>(content[i + 1]) != 0) likelyUTF16LE = false;
            if (static_cast<uint8_t>(content[i]) != 0) likelyUTF16BE = false;
        }
    }
    if (likelyUTF16LE) return Encoding::UTF16_LE;
    if (likelyUTF16BE) return Encoding::UTF16_BE;
    return Encoding::ASCII;
}

std::string toUTF8(const std::string& content, Encoding encoding) {
    switch (encoding) {
        case Encoding::ASCII:
        case Encoding::UTF8: return content;
        case Encoding::UTF16_LE: {
            size_t start = (content.size() >= 2 && static_cast<uint8_t>(content[0]) == 0xFF && static_cast<uint8_t>(content[1]) == 0xFE) ? 2 : 0;
            const char16_t* utf16Data = reinterpret_cast<const char16_t*>(content.data() + start);
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
            return converter.to_bytes(utf16Data, utf16Data + (content.size() - start) / 2);
        }
        case Encoding::UTF16_BE: {
            size_t start = (content.size() >= 2 && static_cast<uint8_t>(content[0]) == 0xFE && static_cast<uint8_t>(content[1]) == 0xFF) ? 2 : 0;
            std::u16string utf16LE;
            for (size_t i = start; i + 1 < content.size(); i += 2) {
                utf16LE.push_back(static_cast<char16_t>(content[i]) | (static_cast<char16_t>(content[i + 1]) << 8));
            }
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
            return converter.to_bytes(utf16LE.data(), utf16LE.data() + utf16LE.size());
        }
        case Encoding::BINARY:
        case Encoding::UNKNOWN: return "";
    }
    return "";
}

std::string fromUTF8(const std::string& utf8Content, Encoding encoding) {
    switch (encoding) {
        case Encoding::ASCII:
        case Encoding::UTF8: return utf8Content;
        case Encoding::UTF16_LE: {
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
            std::u16string utf16 = converter.from_bytes(utf8Content);
            std::string result;
            result.reserve(utf16.size() * 2);
            for (char16_t c : utf16) {
                result.push_back(static_cast<char>(c & 0xFF));
                result.push_back(static_cast<char>((c >> 8) & 0xFF));
            }
            return result;
        }
        case Encoding::UTF16_BE: {
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
            std::u16string utf16 = converter.from_bytes(utf8Content);
            std::string result;
            result.reserve(utf16.size() * 2);
            for (char16_t c : utf16) {
                result.push_back(static_cast<char>((c >> 8) & 0xFF));
                result.push_back(static_cast<char>(c & 0xFF));
            }
            return result;
        }
        case Encoding::BINARY:
        case Encoding::UNKNOWN: return utf8Content;
    }
    return utf8Content;
}

std::string encodingToString(Encoding encoding) {
    switch (encoding) {
        case Encoding::ASCII: return "ASCII";
        case Encoding::UTF8: return "UTF8";
        case Encoding::UTF16_LE: return "UTF16_LE";
        case Encoding::UTF16_BE: return "UTF16_BE";
        case Encoding::BINARY: return "BINARY";
        case Encoding::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

Encoding encodingFromString(const std::string& encodingStr) {
    if (encodingStr == "ASCII") return Encoding::ASCII;
    if (encodingStr == "UTF8") return Encoding::UTF8;
    if (encodingStr == "UTF16_LE") return Encoding::UTF16_LE;
    if (encodingStr == "UTF16_BE") return Encoding::UTF16_BE;
    return Encoding::UNKNOWN;
}

FileMetadata readFileWithEncoding(const fs::path& path, bool showProgressBar) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open file");

    size_t fileSize = in.tellg();
    in.seekg(0, std::ios::beg);

    std::string content;
    content.reserve(fileSize);

    const size_t bufferSize = 4096;
    char buffer[bufferSize];
    size_t bytesRead = 0;

    if (showProgressBar) {
        std::cout << "\rReading " << path.filename().string() << " (0%)" << std::flush;
    }

    while (in.read(buffer, bufferSize)) {
        content.append(buffer, bufferSize);
        bytesRead += bufferSize;
        if (showProgressBar) {
            showProgress(bytesRead, fileSize, "Reading " + path.filename().string());
        }
    }
    content.append(buffer, in.gcount());
    bytesRead += in.gcount();

    if (showProgressBar) {
        showProgress(bytesRead, fileSize, "Reading " + path.filename().string());
        std::cout << "\n";
    }

    Encoding encoding = detectEncoding(content);
    if (encoding == Encoding::BINARY || encoding == Encoding::UNKNOWN) {
        std::cerr << "Warning: Skipping file '" << path << "' ("
                  << (encoding == Encoding::BINARY ? "binary" : "unsupported encoding") << ")\n";
        return {"", Encoding::UNKNOWN};
    }
    return {toUTF8(content, encoding), encoding};
}

size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}


    CurlHandle::CurlHandle(const std::string& apiKey) : curl(curl_easy_init()), headers(nullptr) {
        if (!curl) throw std::runtime_error("Failed to initialize libcurl.");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + apiKey).c_str());
    }

    CurlHandle::~CurlHandle() {
        if (headers) curl_slist_free_all(headers);
        if (curl) curl_easy_cleanup(curl);
    }


json buildChatPayload(const json& messages, bool stream, bool jsonMode) {
    json payload = {
        {"model", currentModel},
        {"messages", messages},
        {"stream", stream},
        {"temperature", currentTemperature}
    };

    if (currentMaxTokens > 0) payload["max_tokens"] = currentMaxTokens;
    if (currentRandomSeed != 0) payload["random_seed"] = currentRandomSeed;

    if (jsonMode) {
        payload["response_format"] = {{{"type", "json_object"}}};
    }

    return payload;
}

void listModels(const std::string& apiKey) {
    CurlHandle h(apiKey);
    std::string responseBody;
    long httpCode = 0;
    curl_easy_setopt(h.curl, CURLOPT_URL, "https://api.mistral.ai/v1/models");
    curl_easy_setopt(h.curl, CURLOPT_HTTPHEADER, h.headers);
    curl_easy_setopt(h.curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(h.curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(h.curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(h.curl, CURLOPT_USERAGENT, "mistral_cli/1.0");
    CURLcode res = curl_easy_perform(h.curl);
    if (res != CURLE_OK) throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(res));
    curl_easy_getinfo(h.curl, CURLINFO_RESPONSE_CODE, &httpCode);

    json resp;
    try {
        resp = json::parse(responseBody);
    } catch (const json::parse_error& e) {
        if (debugMode) {
            std::cerr << "[DEBUG] [listModels] JSON parse failed for models at byte " << e.byte
                      << ": " << e.what() << " (Context: Parsing models list response)\n";
        }
        throw std::runtime_error("[listModels] Failed to parse models response: " + std::string(e.what()));
    }

    if (httpCode != 200) {
        std::string msg = resp.value("message", "API error (HTTP " + std::to_string(httpCode) + ")");
        throw std::runtime_error("[listModels] " + msg);
    }
    std::cout << "Available models:\n";
    if (resp.contains("data")) {
        for (const auto& m : resp["data"]) {
            std::cout << "  " << m.value("id", "?");
            if (m.contains("capabilities")) {
                auto caps = m["capabilities"];
                std::string c;
                if (caps.value("vision", false)) c += " vision";
                if (caps.value("function_calling", false)) c += " tools";
                if (!c.empty()) std::cout << " (" << c.substr(1) << ")";
            }
            std::cout << "\n";
        }
    }
}

//json callMistral(const json& messages, const std::string& apiKey);
json callMistralWithRetry(const json& messages, const std::string& apiKey) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastApiCallTime).count();
    if (elapsed < MIN_API_DELAY_MS) {
        if (debugMode) {
            std::cerr << "[DEBUG] Rate limiting: waiting " << (MIN_API_DELAY_MS - elapsed) << "ms\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(MIN_API_DELAY_MS - elapsed));
    }
    lastApiCallTime = std::chrono::steady_clock::now();

    int retryCount = 0;
    while (true) {
        try {
            // Show spinner while waiting for Mistral
            std::cout << "\rWaiting for Mistral";
            bool spinnerActive = true;
            std::thread spinnerThread([&spinnerActive]() {
                while (spinnerActive) {
                    showSpinner("Waiting for Mistral");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });

            json response = callMistral(messages, apiKey);

            // Stop spinner and clear line
            spinnerActive = false;
            spinnerThread.join();
            std::cout << "\r" << std::string(50, ' ') << "\r" << std::flush;

            // Ensure the response is a JSON object
            if (!response.is_object()) {
                if (response.is_string()) {
                    return {"answer", response.get<std::string>()};
                }
                return {"answer", response.dump()};
            }
            return response;
        } catch (const std::exception& e) {
            // Stop spinner if an error occurs
            std::cout << "\r" << std::string(50, ' ') << "\r" << std::flush;

            if (retryCount >= MAX_RETRIES) {
                throw;
            }
            retryCount++;
            int delayMs = INITIAL_RETRY_DELAY_MS * (1 << (retryCount - 1));
            if (debugMode) {
                std::cerr << "[DEBUG] Retry " << retryCount << "/" << MAX_RETRIES
                          << " after " << delayMs << "ms. Error: " << e.what() << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
}
json callMistral(const json& messages, const std::string& apiKey) {
    json payload = buildChatPayload(messages, false, false);
    std::string payloadStr = payload.dump();
    CurlHandle h(apiKey);
    std::string responseBody;
    long httpCode = 0;

    curl_easy_setopt(h.curl, CURLOPT_URL, "https://api.mistral.ai/v1/chat/completions");
    curl_easy_setopt(h.curl, CURLOPT_POST, 1L);
    curl_easy_setopt(h.curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(h.curl, CURLOPT_HTTPHEADER, h.headers);
    curl_easy_setopt(h.curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(h.curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(h.curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(h.curl, CURLOPT_USERAGENT, "mistral_cli/1.0");

    CURLcode res = curl_easy_perform(h.curl);
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("[callMistral] Network transport failed: ") + curl_easy_strerror(res));
    }

    curl_easy_getinfo(h.curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (httpCode != 200) {
        if (responseBody.empty()) {
            throw std::runtime_error("[callMistral] API error: Server returned HTTP " + std::to_string(httpCode) + " with an empty response body. Verify your API key.");
        }
        try {
            json apiResponse = json::parse(responseBody);
            std::string msg = apiResponse.value("message", "API error (HTTP " + std::to_string(httpCode) + ")");
            throw std::runtime_error("[callMistral] API error: " + msg);
        } catch (const json::parse_error& e) {
            std::cerr << "[ERROR] [callMistral] JSON parse failed for error response: " << e.what() << "\n";
            if (debugMode) {
                std::cerr << "[DEBUG] [callMistral] Failed at byte " << e.byte
                          << " (Context: Parsing error response body)\n";
                std::cerr << "[DEBUG] Raw response: " << responseBody << "\n";
            }
            throw std::runtime_error("[callMistral] API error: Server returned HTTP " + std::to_string(httpCode) + ". Raw response: " + responseBody);
        } catch (const std::exception& e) {
            throw std::runtime_error("[callMistral] API error: Server returned HTTP " + std::to_string(httpCode) + ". Raw response: " + responseBody);
        }
    }

    if (responseBody.empty()) {
        throw std::runtime_error("[callMistral] API error: Server returned 200 OK but the response body was completely empty.");
    }

    json apiResponse;
    try {
        apiResponse = json::parse(responseBody);
    } catch (const json::parse_error& e) {
        std::cerr << "[ERROR] [callMistral] JSON parse failed for API response: " << e.what() << "\n";
        if (debugMode) {
            std::cerr << "[DEBUG] [callMistral] Failed at byte " << e.byte
                      << " (Context: Parsing successful API response body)\n";
            std::cerr << "[DEBUG] Raw response: " << responseBody << "\n";
        }
        throw std::runtime_error("[callMistral] Failed to parse successful API response JSON: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw std::runtime_error("[callMistral] Failed to parse successful API response JSON: " + std::string(e.what()));
    }

    std::string contentStr;
    try {
        if (!apiResponse.contains("choices") || !apiResponse["choices"].is_array() || apiResponse["choices"].empty()) {
            throw std::runtime_error("[callMistral] Invalid response format: 'choices' array not found or empty.");
        }
        json firstChoice = apiResponse["choices"][0];
        if (!firstChoice.contains("message") || !firstChoice["message"].contains("content")) {
            throw std::runtime_error("[callMistral] Invalid response format: 'message.content' map target not found.");
        }
        contentStr = firstChoice["message"]["content"].get<std::string>();
    } catch (const std::exception& e) {
        throw std::runtime_error("[callMistral] Failed to extract content data out of API response: " + std::string(e.what()));
    }

    // Use the helper function to extract JSON from hybrid responses
    return extractJsonFromHybridResponse(contentStr);
}

fs::path resolvePath(const std::string& p) {
    fs::path raw(p);
    if (raw.is_relative()) raw = fs::current_path() / raw;
    return fs::weakly_canonical(raw);
}

bool withinCwd(const fs::path& target) {
    fs::path cwd = fs::weakly_canonical(fs::current_path());
    fs::path t = fs::weakly_canonical(target);
    auto targetIt = t.begin();
    for (const auto& c : cwd) {
        if (targetIt == t.end()) return false;
        if (*targetIt != c) return false;
        ++targetIt;
    }
    return true;
}

bool requestPermission(const std::string& action, const std::string& displayPath, const std::string& allowKey) {
    loadAllowlist();
    if (g_allowlist.count(allowKey)) return true;
    std::cout << "[permission] " << action << " outside CWD: " << displayPath << "\nAllow? (y/N): " << std::flush;
    std::string resp;
    if (!std::getline(std::cin, resp)) return false;
    if (resp == "y" || resp == "Y") {
        g_allowlist.insert(allowKey);
        saveAllowlist();
        return true;
    }
    return false;
}

bool requestPermissionAlways(const std::string& action, const std::string& displayPath) {
    std::cout << "[permission] " << action << ": " << displayPath << "\nAllow? (y/N): " << std::flush;
    std::string resp;
    if (!std::getline(std::cin, resp)) return false;
    return (resp == "y" || resp == "Y");
}

json executeAction(const json& action, std::set<std::string>& processedFiles) {
    if (!action.is_object()) {
        if (debugMode) {
            std::cerr << "[DEBUG] [executeAction] Expected 'action' to be a JSON object, got: " << action.dump()
                      << " (Context: Validating action structure)\n";
        }
        return {{{"status", "error"}, {"reason", "action must be a JSON object"}}};
    }

    if (!action.contains("type")) {
        return {{{"status", "error"}, {"reason", "action requires 'type'"}}};
    }
    std::string type = action.at("type").get<std::string>();

    // Handle Git actions
    if (type == "git") {
        std::string command = action.value("command", "");
        std::string args = action.value("args", "");
        std::string fullCommand = "git " + command + " " + args;

        if (command == "diff" || command == "show" || command == "blame") {
            int startLine = action.value("start_line", -1);
            int endLine = action.value("end_line", -1);
            int maxTokens = action.value("max_tokens", -1);
            if (startLine != -1 || endLine != -1) {
                std::string lineRange;
                if (startLine != -1 && endLine != -1) {
                    lineRange = " | sed -n " + std::to_string(startLine) + "," + std::to_string(endLine) + "p";
                } else if (startLine != -1) {
                    lineRange = " | sed -n " + std::to_string(startLine) + "p";
                } else if (endLine != -1) {
                    lineRange = " | sed -n 1," + std::to_string(endLine) + "p";
                }
                fullCommand += lineRange;
            }
            if (maxTokens != -1) {
                fullCommand += " | head -n " + std::to_string(maxTokens);
            }
        }

        if (isDestructiveGitCommand(command)) {
            if (!requestPermissionAlways("Execute Git command", fullCommand)) {
                return {
                    {"type", "git"},
                    {"command", command},
                    {"status", "denied"},
                    {"reason", "User denied destructive Git command"}
                };
            }
        }

        try {
            std::string output = executeShellCommand(fullCommand);
            return {
                {"type", "git"},
                {"command", command},
                {"status", "success"},
                {"output", trim(output)}
            };
        } catch (const std::exception& e) {
            return {
                {"type", "git"},
                {"command", command},
                {"status", "error"},
                {"reason", e.what()}
            };
        }
    }

    // Handle shell commands
    if (type == "shell") {
        std::string command = action.value("command", "");
        if (command.empty()) {
            return {{{"type", "shell"}, {"status", "error"}, {"reason", "command is empty"}}};
        }

        if (isDestructiveShellCommand(command)) {
            if (!requestPermissionAlways("Execute shell command", command)) {
                return {
                    {"type", "shell"},
                    {"command", command},
                    {"status", "denied"},
                    {"reason", "User denied destructive shell command"}
                };
            }
        }

        try {
            std::string output = executeShellCommand(command);
            return {
                {"type", "shell"},
                {"command", command},
                {"status", "success"},
                {"output", trim(output)}
            };
        } catch (const std::exception& e) {
            return {
                {"type", "shell"},
                {"command", command},
                {"status", "error"},
                {"reason", e.what()}
            };
        }
    }

    // Handle file actions
    if (!action.contains("path")) {
        return {{{"status", "error"}, {"reason", "action requires 'path'"}}};
    }
    std::string p = action.at("path").get<std::string>();
    fs::path resolved = resolvePath(p);
    std::string resolvedStr = resolved.string();

    if (type == "read_file") {
        if (processedFiles.count(resolvedStr)) {
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "skipped"}, {"reason", "already processed in this turn"}}};
        }
        processedFiles.insert(resolvedStr);

        try {
            // Use readFileWithEncoding to handle encoding and progress
            FileMetadata fileMetadata = readFileWithEncoding(resolved, true); // Show progress bar
            if (fileMetadata.encoding == Encoding::UNKNOWN || fileMetadata.encoding == Encoding::BINARY) {
                return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", "unsupported encoding or binary file"}}};
            }

            // Handle line range if specified
            std::string content = fileMetadata.content;
            int startLine = 1;
            int endLine = -1;
            if (action.contains("start_line")) {
                startLine = action["start_line"].get<int>();
                if (startLine < 1) startLine = 1;
            }
            if (action.contains("end_line")) {
                endLine = action["end_line"].get<int>();
            }

            // Extract the requested line range
            std::istringstream iss(content);
            std::string line;
            std::string filteredContent;
            int currentLine = 0;
            while (std::getline(iss, line)) {
                currentLine++;
                if (currentLine >= startLine) {
                    filteredContent += line + "\n";
                    if (endLine != -1 && currentLine >= endLine) {
                        break;
                    }
                }
            }

            return {
                {"type", type},
                {"path", p},
                {"resolved", resolvedStr},
                {"status", "success"},
                {"content", filteredContent},
                {"encoding", encodingToString(fileMetadata.encoding)},
                {"start_line", startLine},
                {"end_line", endLine == -1 ? currentLine : endLine},
                {"truncated", false}
            };
        } catch (const std::exception& e) {
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", e.what()}}};
        }
    }

    bool isDestructive = (type == "delete_file" || type == "delete_dir");
    if (isDestructive) {
        if (!requestPermissionAlways(type, resolvedStr)) {
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "denied"}, {"reason", "user denied destructive action"}}};
        }
    } else if (!withinCwd(resolved)) {
        if (!requestPermission(type, resolvedStr, resolvedStr)) {
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "denied"}, {"reason", "user denied access outside CWD"}}};
        }
    }

    if (type == "write_file") {
        std::string content = action.value("content", "");
        std::string mode = action.value("mode", "write");
        Encoding encoding = Encoding::UTF8;
        if (action.contains("encoding")) {
            encoding = encodingFromString(action["encoding"].get<std::string>());
        }
        if (encoding == Encoding::BINARY || encoding == Encoding::UNKNOWN) {
            std::cerr << "Warning: Cannot write file '" << resolved << "' (unsupported encoding)\n";
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", "unsupported encoding"}}};
        }

        std::string encodedContent = fromUTF8(content, encoding);
        bool append = (mode == "append");

        // Show progress for writing
        showProgress(0, encodedContent.size(), "Writing " + resolved.filename().string());

        if (resolved.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(resolved.parent_path(), ec);
        }

        std::ofstream out(resolved, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!out) {
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", "cannot write file"}}};
        }

        // Write in chunks and update progress
        const size_t chunkSize = 4096;
        for (size_t i = 0; i < encodedContent.size(); i += chunkSize) {
            size_t end = std::min(i + chunkSize, encodedContent.size());
            out.write(encodedContent.data() + i, end - i);
            showProgress(i + chunkSize, encodedContent.size(), "Writing " + resolved.filename().string());
        }

        std::cout << "\n"; // Newline after progress bar
        return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "success"}, {"mode", mode}, {"encoding", encodingToString(encoding)}}};
    }

    if (type == "list_dir") {
        std::error_code ec;
        if (!fs::is_directory(resolved, ec)) {
            std::string reason = ec ? ec.message() : std::string("not a directory");
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", reason}}};
        }

        // Show progress for listing
        std::cout << "\rListing " << resolved.filename().string() << " [Spinning...] " << std::flush;
        json entries = json::array();
        for (auto& e : fs::directory_iterator(resolved, ec)) {
            entries.push_back({{"name", e.path().filename().string()}, {"type", e.is_directory() ? "dir" : "file"}});
        }
        std::cout << "\n"; // Clear progress line
        if (ec) {
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", ec.message()}}};
        }
        return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "success"}, {"entries", entries}}};
    }

    if (type == "delete_file") {
        std::error_code ec;
        if (!fs::remove(resolved, ec)) {
            std::string reason = ec ? ec.message() : std::string("file not found");
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", reason}}};
        }
        return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "success"}}};
    }

    if (type == "delete_dir") {
        std::error_code ec;
        std::uintmax_t n = fs::remove_all(resolved, ec);
        if (ec) {
            return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", ec.message()}}};
        }
        return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "success"}, {"removed", n}}};
    }

    return {{{"type", type}, {"path", p}, {"resolved", resolvedStr}, {"status", "error"}, {"reason", "unknown action type"}}};
}

void printOutput(const std::string& text) {
    std::cout << text << "\n";
}

void handleTurn(const std::string& userInput, json& history, const std::string& apiKey) {
    history.push_back({{"role", "user"}, {"content", userInput}});
    std::set<std::string> processedFiles;

    if (userInput.find("read all files") != std::string::npos || userInput.find("read all the files") != std::string::npos) {
        json listAction = {{{"type", "list_dir"}, {"path", "."}}};
        json listResult = executeAction(listAction, processedFiles);
        if (listResult["status"] != "success") {
            printOutput("Failed to list directory: " + listResult["reason"].get<std::string>());
            return;
        }
        json allFilesContent = json::object();
        for (const auto& entry : listResult["entries"]) {
            std::string fileName = entry["name"].get<std::string>();
            if (entry["type"] != "file") continue;

            fs::path filePath = resolvePath(fileName);

            if (fileName.find(".log") != std::string::npos ||
                fileName.find(".bak") != std::string::npos ||
                (fileName != "mistral_cli" && fileName != "mistral_cli.exe" && fileName.find(".sh") == std::string::npos && fileName.find(".bat") == std::string::npos)) {
                std::error_code ec;
                auto fileStatus = fs::status(filePath, ec);
                if (!ec && (fileStatus.permissions() & fs::perms::owner_exec) != fs::perms::none) {
                    allFilesContent[fileName] = {{{"status", "skipped"}, {"reason", "executable file"}}};
                    continue;
                }
            }

            std::ifstream in(filePath, std::ios::binary);
            if (!in) {
                allFilesContent[fileName] = {{{"status", "error"}, {"reason", "cannot open file"}}};
                continue;
            }
            std::string fileHeader(4, '\0');
            in.read(&fileHeader[0], 4);
            in.close();

            if (isLikelyBinary(fileHeader)) {
                allFilesContent[fileName] = {{{"status", "skipped"}, {"reason", "binary file"}}};
                continue;
            }

            json readAction = {{{"type", "read_file"}, {"path", fileName}}};
            json readResult = executeAction(readAction, processedFiles);
            if (readResult["status"] == "success") {
                allFilesContent[fileName] = {
                    {"status", "success"},
                    {"encoding", readResult.value("encoding", "UTF8")},
                    {"content", readResult["content"]}
                };
            } else {
                allFilesContent[fileName] = {
                    {"status", readResult["status"]},
                    {"reason", readResult["reason"]}
                };
            }
        }

        if (debugMode) {
            printOutput(allFilesContent.dump(2));
        } else {
            for (const auto& file : allFilesContent.items()) {
                std::string fileName = file.key();
                std::string status = file.value()["status"].get<std::string>();
                if (status == "success") {
                    std::cout << "[File: " << fileName << "] - Read successfully (Encoding: " << file.value()["encoding"].get<std::string>() << ")\n";
                } else {
                    std::cout << "[File: " << fileName << "] - " << status << ": " << file.value()["reason"].get<std::string>() << "\n";
                }
            }
        }
        history.push_back({{"role", "assistant"}, {"content", "Processed all relevant files in CWD."}});
        return;
    }

    const int maxIterations = 12;
    for (int iter = 0; iter < maxIterations; ++iter) {
        json messages = json::array();
        messages.push_back({{"role", "system"}, {"content", buildSystemPrompt()}});
        size_t historyStart = (history.size() > 3) ? history.size() - 3 : 0;
        for (size_t i = historyStart; i < history.size(); ++i) {
            messages.push_back(history[i]);
        }

        json content;
        try {
            content = callMistralWithRetry(messages, apiKey);
        } catch (const std::exception& e) {
            json err = {{{"status", "error"}, {"error", e.what()}}};
            logJsonExchange("Error", err, false);
            printOutput(e.what());
            return;
        }

        if (!content.is_object()) {
            if (content.is_string()) {
                std::string contentStr = content.get<std::string>();
                try {
                    content = json::parse(contentStr);
                } catch (const json::parse_error& e) {
                    std::cerr << "[ERROR] [handleTurn] JSON parse failed: " << e.what() << "\n";
                    if (debugMode) {
                        std::cerr << "[DEBUG] [handleTurn] Failed at byte " << e.byte
                                  << " (Context: Parsing content string as JSON)\n";
                        std::cerr << "[DEBUG] Raw content: " << contentStr << "\n";
                    }
                    content = {{{"answer", contentStr}}};
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] [handleTurn] Unexpected error parsing JSON: " << e.what() << "\n";
                    if (debugMode) {
                        std::cerr << "[DEBUG] Raw content: " << contentStr << "\n";
                    }
                    content = {{{"answer", contentStr}}};
                }
            } else {
                std::cerr << "[ERROR] [handleTurn] Unexpected content type: " << content.dump() << "\n";
                if (debugMode) {
                    std::cerr << "[DEBUG] Raw content: " << content.dump() << "\n";
                }
                content = {{{"answer", content.dump()}}};
            }
        }

        if (debugMode) {
            std::cout << "[DEBUG] Content type: "
                      << (content.is_object() ? "Object" :
                          content.is_string() ? "String" :
                          content.is_array() ? "Array" : "Other")
                      << "\n";
            std::cout << "[DEBUG] Content: " << content.dump() << "\n";
        }

        if (content.is_object() && content.contains("answer")) {
            std::string answer = content["answer"].get<std::string>();

            size_t start = 0;
            while ((start = answer.find("```", start)) != std::string::npos) {
                size_t end = answer.find("```", start + 3);
                if (end == std::string::npos) {
                    break;
                }

                std::string blockContent = answer.substr(start + 3, end - start - 3);
                size_t newlinePos = blockContent.find('\n');
                if (newlinePos != std::string::npos) {
                    blockContent = blockContent.substr(newlinePos + 1);
                }

                blockContent.erase(0, blockContent.find_first_not_of(" \n\r\t"));
                blockContent.erase(blockContent.find_last_not_of(" \n\r\t") + 1);

                answer.replace(start, end - start + 3, blockContent);
                start += blockContent.length();
            }

            printOutput(answer);
            history.push_back({{"role", "assistant"}, {"content", answer}});
            return;
        }

        if (content.is_object() && content.contains("action")) {
            if (content.contains("message") && !content["message"].get<std::string>().empty()) {
                printOutput(content["message"].get<std::string>());
            }
            json action = content["action"];
            json result = executeAction(action, processedFiles);
            history.push_back({{"role", "assistant"}, {"content", content.dump()}});
            history.push_back({{"role", "user"}, {"content", "[action_result] " + result.dump()}});
            if (result.contains("status")) {
                if (result["status"] == "success") {
                    if (result.contains("output")) {
                        printOutput(result["output"].get<std::string>());
                    } else if (result.contains("entries")) {
                        for (const auto& entry : result["entries"]) {
                            std::string name = entry["name"].get<std::string>();
                            std::string type = entry["type"].get<std::string>();
                            printOutput(name + " (" + type + ")");
                        }
                    }
                } else {
                    std::string reason = result.contains("reason") ? result["reason"].get<std::string>() : "Unknown error";
                    printOutput("Action failed: " + reason);
                }
            }
            continue;
        }

        if (content.is_string()) {
            printOutput(content.get<std::string>());
            history.push_back({{"role", "assistant"}, {"content", content.get<std::string>()}});
            return;
        }

        if (debugMode) {
            printOutput(content.dump());
        } else {
            printOutput("Unexpected response format.");
        }
        history.push_back({{"role", "assistant"}, {"content", content.dump()}});
        return;
    }
    printOutput("[agent loop limit reached]");
}

#ifndef TEST_BUILD
int main(int argc, char* argv[]) {
    parseArguments(argc, argv);
    try {
        std::string apiKey = getApiKey();
        json history;
        while (true) {
            std::string userInput;
            std::cout << "Input: ";
            if (!std::getline(std::cin, userInput)) break;
            if (userInput == "quit" || userInput == "exit") break;

            // Check for local execution prefixes (e.g., "!run" or "!git")
            if (userInput.rfind("!run ", 0) == 0) {
                std::string command = userInput.substr(5); // Remove "!run " prefix
                if (command.empty()) {
                    std::cout << "Error: No command provided after '!run'.\n";
                    continue;
                }

                if (isDestructiveShellCommand(command)) {
                    if (!requestPermissionAlways("Execute local command", command)) {
                        std::cout << "Command denied: User aborted destructive local command.\n";
                        continue;
                    }
                }

                try {
                    std::cout << "[LOCAL EXECUTION] Running: " << command << "\n";
                    std::string output = executeShellCommand(command);
                    std::cout << "[LOCAL OUTPUT]\n" << output << "\n[END LOCAL OUTPUT]\n";
                } catch (const std::exception& e) {
                    std::cerr << "[LOCAL ERROR] " << e.what() << "\n";
                }
                continue;
            }

            if (userInput.rfind("!git ", 0) == 0) {
                std::string gitCommand = userInput.substr(5); // Remove "!git " prefix
                if (gitCommand.empty()) {
                    std::cout << "Error: No Git command provided after '!git'.\n";
                    continue;
                }

                if (isDestructiveGitCommand(gitCommand)) {
                    if (!requestPermissionAlways("Execute Git command", gitCommand)) {
                        std::cout << "Git command denied: User aborted destructive Git operation.\n";
                        continue;
                    }
                }

                try {
                    std::string fullCommand = "git " + gitCommand;
                    std::cout << "[LOCAL GIT EXECUTION] Running: " << fullCommand << "\n";
                    std::string output = executeShellCommand(fullCommand);
                    std::cout << "[LOCAL GIT OUTPUT]\n" << output << "\n[END LOCAL GIT OUTPUT]\n";
                } catch (const std::exception& e) {
                    std::cerr << "[LOCAL GIT ERROR] " << e.what() << "\n";
                }
                continue;
            }

            if (userInput == "clear" || userInput == "reset") {
                history = json::array();
                std::cout << "[history cleared]\n";
                continue;
            }
            if (userInput == "/models") {
                listModels(apiKey);
                continue;
            }
            if (userInput.rfind("/model ", 0) == 0) {
                currentModel = userInput.substr(7);
                std::cout << "[model set to " << currentModel << "]\n";
                continue;
            }
            if (userInput == "/model") {
                std::cout << "[current model: " << currentModel << "]\n";
                continue;
            }
            if (userInput == "/help") {
                std::cout << "Commands: quit|exit, clear|reset, /models, /model <name>, /model, /help, !run <command>, !git <command>\n";
                std::cout << "Args: --model <name> --temperature <n> --max-tokens <n> --seed <n> -debug\n";
                continue;
            }
            handleTurn(userInput, history, apiKey);
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
#endif
