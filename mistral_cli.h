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

#ifndef MISTRAL_CLI_H
#define MISTRAL_CLI_H

#include <string>
#include <set>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <curl/curl.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

extern bool debugMode;
extern std::string currentModel;
extern double currentTemperature;
extern int currentMaxTokens;
extern int currentRandomSeed;
extern std::set<std::string> g_allowlist;
extern bool g_allowlistLoaded;
extern std::set<std::string> g_processedFiles;

// --- Move SYSTEM_PROMPT_STATIC here ---
static const std::string SYSTEM_PROMPT_STATIC =
    "You are mistral_cli, a command-line assistant running directly on the user's machine. "
    "You operate in the following environment and should answer accordingly.\n"
    "Operating system: " MISTRAL_CLI_OS "\n"
    "Current working directory: {CWD}\n\n"
    "FILE ACCESS POLICY: You may read, write, and list files within the current working directory (CWD) tree freely. "
    "read_file, write_file, and list_dir inside CWD require no permission. "
    "Access to files or folders OUTSIDE CWD requires the user's explicit permission (the client asks and remembers approvals per-project). "
    "Deleting ANY file or directory (inside or outside CWD) ALWAYS requires explicit user permission. "
    "Symlinks are resolved to their targets for this check. "
    "You NEVER handle permission yourself. To access ANY file, simply emit the corresponding action. "
    "The client automatically asks the user for permission when needed and returns the result to you. "
    "Do NOT mention permission, confirmation, consent, or granting access in your answers or messages. "
    "Do NOT tell the user that access requires permission or ask them to confirm/consent. "
    "Do NOT produce a final answer that defers a file action back to the user — emit the action instead. "
    "The client returns the result (success, denied, or error) including the resolved absolute path, then you continue.\n\n"
    "RESPONSE PROTOCOL: Always reply with a single JSON object and nothing else.\n"
    "- To give the user a final answer: {\"answer\": \"your reply text\"}\n"
    "- To request a file action (the client executes it and returns the result):\n"
    "  {\"action\": {\"type\": \"read_file\", \"path\": \"relative/path\"}, \"message\": \"what you are doing\"}\n"
    "  {\"action\": {\"type\": \"write_file\", \"path\": \"relative/path\", \"content\": \"file contents\", \"mode\": \"write\"}, \"message\": \"...\"}\n"
    "  (mode \"write\" overwrites, mode \"append\" adds to the end of an existing file.)\n"
    "  {\"action\": {\"type\": \"list_dir\", \"path\": \"relative/path\"}, \"message\": \"...\"}\n"
    "  {\"action\": {\"type\": \"delete_file\", \"path\": \"relative/path\"}, \"message\": \"...\"}\n"
    "  {\"action\": {\"type\": \"delete_dir\", \"path\": \"relative/path\"}, \"message\": \"...\"}  (delete_dir removes the directory and everything inside it)\n"
    "\n"
    "IF THE USER ASKS TO READ ALL FILES IN A DIRECTORY:\n"
    "1. You MUST first call list_dir with {\"action\": {\"type\": \"list_dir\", \"path\": \".\"}, \"message\": \"Listing all files in the current directory\"}.\n"
    "2. Then, for each file in the response, call read_file ONCE with {\"action\": {\"type\": \"read_file\", \"path\": \"filename\"}, \"message\": \"Reading filename\"}.\n"
    "3. After reading all files, provide a final answer with {\"answer\": \"...\"}.\n"
    "After an action, the client returns its result, and you continue until you can give a final answer.\n"
    "Use relative paths when possible. Be concise.";

fs::path allowlistPath();
void loadAllowlist();
void saveAllowlist();
void parseArguments(int argc, char* argv[]);
std::string getApiKey();
void logJsonExchange(const std::string& label, const json& data, bool isUpstream);
std::string getCwd();
std::string buildSystemPrompt();

enum class Encoding { ASCII, UTF8, UTF16_LE, UTF16_BE, BINARY, UNKNOWN };
bool isLikelyBinary(const std::string& content);
Encoding detectEncoding(const std::string& content);
std::string toUTF8(const std::string& content, Encoding encoding);
std::string fromUTF8(const std::string& utf8Content, Encoding encoding);
std::string encodingToString(Encoding encoding);
Encoding encodingFromString(const std::string& encodingStr);

struct FileMetadata {
    std::string content;
    Encoding encoding;
};
FileMetadata readFileWithEncoding(const fs::path& path);

size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata);

struct CurlHandle {
    CURL* curl;
    struct curl_slist* headers;
    CurlHandle(const std::string& apiKey);
    ~CurlHandle();
};

json buildChatPayload(const json& messages, bool stream, bool jsonMode);
void listModels(const std::string& apiKey);
json callMistral(const json& messages, const std::string& apiKey);
fs::path resolvePath(const std::string& p);
bool withinCwd(const fs::path& target);
bool requestPermission(const std::string& action, const std::string& displayPath, const std::string& allowKey);
bool requestPermissionAlways(const std::string& action, const std::string& displayPath);
//json executeAction(const json& action);
json executeAction(const json& action, std::set<std::string>& processedFiles);
void printOutput(const std::string& text);
void handleTurn(const std::string& userInput, json& history, const std::string& apiKey);




#endif
