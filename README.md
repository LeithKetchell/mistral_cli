# Mistral CLI Technical Reference Manual & Developer Guide

```text
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
```

---

## 🔎 Overview & System Architecture

`mistral_cli` is a high-performance, single-executable terminal application written in **ISO C++17**. It translates natural language inputs into autonomous local system interactions by leveraging Mistral AI’s function execution capabilities via structured JSON mode.

Instead of wrapping a complex text user interface (TUI) or graphical layers, the application treats the LLM as an operating core that interprets a strict **JSON-in/JSON-out state protocol**. It safely manages multi-turn tool loops, auto-detects cross-platform file encodings, and intercepts filesystem interactions via a real-time user-approval permission engine.

```
[ User Input ] <---> [ Input / Loop Processing ]
       |v
[ System Prompt Assembly ] (Injects Platform & Resolved CWD)
       |v
[ callMistralWithRetry ] (1s Rate Limiting & Backoff Logic)
       |v
+---------------+---------------+
|                               |
|   {"action": ... }           {"answer": ... }
|                               |
+---------------+---------------+
       |v                               |v
[ executeAction Engine ]            [ Standard Output ]
- Inside CWD: Free Pass
- Outside CWD: Prompt [y/N]
- Destructive: Always Prompt
       |v
[ [action_result] -> Loop ]
```

---

## 📥 Installation Guide

### **Prerequisites**
- **C++17 Compiler**: Required to build from source.
  - Linux: `g++` (recommended) or `clang++`.
  - macOS: `clang++` (included with Xcode Command Line Tools).
  - Windows: **MinGW** or **Visual Studio 2022** (with C++17 support).
- **Dependencies**:
  - **libcurl**: For HTTP requests (API communication).
  - **libreadline**: For command-line input handling (Linux/macOS only).
  - **nlohmann/json**: Header-only JSON library (included in the project).

---

### **1. Build from Source**

For convenience, the repository includes **platform-specific build scripts** (`BuildLinux.sh`, `BuildMac.sh`, `BuildWindows.bat`) that automate the compilation process. Alternatively, you can manually compile using the commands below.

#### **Linux**
```bash
# Install dependencies (Debian/Ubuntu)
sudo apt update && sudo apt install -y g++ libcurl4-openssl-dev libreadline-dev

# Option 1: Use the build script
chmod +x BuildLinux.sh
./BuildLinux.sh

# Option 2: Manual compilation
g++ -o mistral_cli mistral_cli.cpp -lcurl -lreadline -std=c++17
```

#### **macOS**
```bash
# Install dependencies (using Homebrew)
brew install curl readline

# Option 1: Use the build script
chmod +x BuildMac.sh
./BuildMac.sh

# Option 2: Manual compilation
clang++ -o mistral_cli mistral_cli.cpp -lcurl -lreadline -std=c++17
```

#### **Windows**
```batch
:: Install MinGW (if not already installed)
:: Download from https://www.mingw-w64.org/ and add to PATH

:: Option 1: Use the build script
BuildWindows.bat

:: Option 2: Manual compilation
g++ -o mistral_cli.exe mistral_cli.cpp -lcurl -std=c++17
```

---

### **2. Install the Binary**
After building, install `mistral_cli` to a system-wide directory so it can be run from any location.

For convenience, the repository includes **platform-specific install scripts** (`InstallLinux.sh`, `InstallMac.sh`, `InstallWindows.bat`) that automate the installation process.

#### **Linux**
```bash
# Run the install script
sudo ./InstallLinux.sh
```

#### **macOS**
```bash
# Run the install script
./InstallMac.sh
```

#### **Windows**
- Run `InstallWindows.bat` **as Administrator**.

---

### **3. Updating After Rebuilding**
If you **modify and rebuild `mistral_cli`**, you **must reinstall it** to ensure the system-wide binary is updated. Simply running the new binary from the build folder **will not** update the installed version.

After rebuilding:
1. **Reinstall** the binary using the platform-specific install script.
2. **Run `mistral_cli -newkey`** to reset the encrypted API key (required due to changes in the binary's encryption hash).

Example:
```bash
# Linux
sudo ./InstallLinux.sh  # Reinstall
mistral_cli -newkey      # Reset the API key
```

```bash
# macOS
./InstallMac.sh         # Reinstall
mistral_cli -newkey      # Reset the API key
```

```batch
:: Windows
InstallWindows.bat      :: Reinstall (as Administrator)
mistral_cli -newkey     :: Reset the API key
```

---

## ⚠️ API Key Encryption & Security Warning

> **⚠️ IMPORTANT: `mistral_cli` encrypts and stores your Mistral API key locally.**
> The key is **obfuscated** using a **system-specific hash** (hostname + username) and stored in:
> - Linux/macOS: `~/.minstral/api_key.enc`
> - Windows: `%APPDATA%\minstral\api_key.enc`

### **Key Rotation Requirement**
If you **modify and rebuild `mistral_cli`**, the **encryption hash will change** (because it depends on the binary's environment).
**You MUST:**
1. **Reinstall** the binary to update the system-wide version.
2. **Run `mistral_cli -newkey`** to destroy the old encrypted key and generate a new one.

```bash
mistral_cli -newkey
```
**Otherwise**, the old key will fail to decrypt, and you’ll see errors like:
```
FATAL: No API key provided.
```
**Note**: The `-newkey` flag **only deletes the old key file**—it does **not** revoke or invalidate your actual Mistral API key.

---

## 🔧 Runtime Configuration & Arguments

### Command-Line Arguments
Configure behavior at launch using these flags:

| Argument | Description |
|----------|-------------|
| `-debug` | Spits real-time data exchanges to `stdout` and logs raw strings directly into `json_exchanges.log`. |
| `--model <string>` | Changes the targeting model destination (Defaults to `mistral-large-latest`). |
| `--temperature <double>` | Sets sample creativity profiles (Range: `0.0` to `1.0`, Defaults to `0.7`). |
| `--max-tokens <int>` | Caps processing lengths (Defaults to `0` for infinite). |
| `--seed <int>` | Forces deterministic, repeatable generation results. |
| **`-newkey`** | **Deletes the encrypted API key file.** Use this after rebuilding `mistral_cli` to avoid decryption errors. |

### Active REPL Commands
Type these commands directly into the terminal loop:
- `quit` or `exit` : Clean application termination.
- `clear` or `reset` : Wipes chat history strings to reclaim context memory.
- `/models` : Queries and arrays online engine categories along with specialized features (e.g., vision, tools).
- `/model <name>` : Switches active inference engines on the fly.
- `/model` : Prints the current working engine profile.
- `/help` : Displays quick reference tables for variables and arguments.

---

## 🛡️ Security Best Practices

1. **Never commit `api_key.enc` to version control**.
   Add it to `.gitignore`:
   ```bash
   echo "~/.minstral/api_key.enc" >> .gitignore
   echo "%APPDATA%\\minstral\\api_key.enc" >> .gitignore
   ```

2. **Rebuilding `mistral_cli`?**
   - **Reinstall** the binary to update the system-wide version.
   - Run `mistral_cli -newkey` **once** after rebuilding to reset the encryption.

3. **Multi-Device Usage**
   The encrypted key is **tied to your machine**. If you copy `mistral_cli` to another device, you’ll need to:
   - Run `mistral_cli -newkey` on the new device.
   - Re-enter your API key when prompted.

---

## 🛠️ Core Capabilities Explained

### 1. The Client-Side Execution Engine (`executeAction`)
The engine intercepts structured commands issued by the upstream model. Every requested pathway undergoes canonical resolution via `std::filesystem::weakly_canonical` before evaluation, effectively breaking path-traversal exploits (such as `../../`).

### 2. Multi-Platform Security Boundaries
The application reads and saves persistent authorization records to an allowlist stored in a JSON configuration file inside the home directory folder. The filesystem engine enforces three distinct rules:
* 🟢 **Inside the CWD Tree:** Actions like reading (`read_file`), writing (`write_file`), and directory index extraction (`list_dir`) bypass user prompts for high-speed scripting performance.
* 🟡 **Outside the CWD Tree:** Any traversal beyond the running working directory intercepts execution, stops the pipeline, and prints an interactive permission dialogue (`[permission] Outside CWD: Allow? (y/N)`). If accepted, the specific path is logged into `allowlist.json` to prevent repeated prompts.
* 🔴 **Destructive Tasks:** Destructive commands (`delete_file`, `delete_dir`) bypass local directory allowlists entirely. They force a real-time prompt every single time to eliminate data-wiping actions.

### 3. File Encoding Engine & Smart Ingestion
Files read through the system pass through an optimization pipeline to prevent terminal corruption or LLM context poisoning:
* **Magic-Byte Header Inspections:** Evaluates raw headers against structural ciphers to catch executable or compressed packages like `ELF`, `MZ` (Windows executables), `PNG`, `JPEG`, and `GIF`.
* **Automated UTF-8 Sanitisation:** Identifies variations like `UTF-16LE`, `UTF-16BE`, and raw `ASCII` bytes via byte order marks (BOM). The application transforms non-compliant formats into clean `UTF-8` dynamically before transmission.
* **Memory Protection Triage:** Implements a strict 10 KB sliding-buffer ceiling during file inspections. Files over this budget are cleanly truncated with a tracking tail tag (`... [truncated]`) to preserve the model's token context memory.

### 4. Resilient Connection Layers
To accommodate unreliable networks, the application wraps connections inside an integrated retry engine:
* **Rate-Limit Safeguards:** Implements a strict 1000 ms minimum time delta between consecutive network hits via a monotonic steady clock, blocking API rate rejection triggers.
* **Exponential Backoff:** If a connection fails, it initiates up to 3 recovery iterations. The retry window doubles dynamically after each failure event (1s → 2s → 4s).

### 5. Host & Key Obfuscation
The tool prevents cleartext configuration snooping through host environment pinning:
* **Hardware Environment Keying:** Queries local attributes including native hostnames and shell user IDs to compound a runtime-specific profile hash.
* **XOR Storage Cipher:** Obfuscates plain text API keys with the generated system profile via an intuitive byte-shifting XOR layer before writing the ciphered contents to `~/.minstral/api_key.enc`. If the file is moved to another computer, it cannot be read.

---

## 📊 Wire Protocol Specifications (JSON)

Communication between the client binary and the model relies on strict, predictable JSON structures.

### 1. Internal Upstream Action Request
When the model requires system interactions, it generates a message containing either an `action` block or an `answer` block.

#### File System Reads
```json
{
  "action": {
    "type": "read_file",
    "path": "src/main.cpp",
    "start_line": 1,
    "end_line": 50
  },
  "message": "Analyse system initialisation blocks."
}
```
*Note: `start_line` and `end_line` are supported optional attributes used to ingest long files in chunks.*

#### File System Modifications
```json
{
  "action": {
    "type": "write_file",
    "path": "config.json",
    "content": "{\n  \"theme\": \"dark\"\n}",
    "mode": "write",
    "encoding": "UTF8"
  },
  "message": "Writing user configuration overrides."
}
```
*Note: Setting `mode` to `"append"` appends content to the end of the file instead of overwriting. Valid encoding types include `"ASCII"`, `"UTF8"`, `"UTF16_LE"`, and `"UTF16_BE"`.*

#### Directory Queries and Removals
```json
{
  "action": {
    "type": "list_dir",
    "path": "./tests"
  },
  "message": "Auditing test suites."
}
```

```json
{
  "action": {
    "type": "delete_file",
    "path": "temp.bak"
  },
  "message": "Purging temporary session data."
}
```

### 2. Client-Side Result Payload
The binary processes the request, catches system results, and returns an update back to the loop:

```json
{
  "type": "read_file",
  "path": "src/main.cpp",
  "resolved": "/projects/mistral-cli/src/main.cpp",
  "status": "success",
  "content": "#include <iostream>\n...",
  "start_line": 1,
  "end_line": 50,
  "truncated": false
}
```

If a user denies a file access prompt or an operation fails, the failure details are cleanly fed back to the model:
```json
{
  "type": "write_file",
  "path": "/etc/hosts",
  "resolved": "/etc/hosts",
  "status": "denied",
  "reason": "user denied access outside CWD"
}
```

### 3. Upstream Final Answer
When the model has gathered all necessary data, it exits the action loop and prints its final response to the user:
```json
{
  "answer": "I have successfully audited your repository. The configuration parameters match standard operational frameworks."
}
```

---

## 📈 Technical Trace Examples (Agent Loop Lifecycle)

### Trace 1: Multi-Turn Code Generation and Creation
This trace shows how the tool handles a multi-turn task inside the CWD, automatically creating a directory structure and writing code without popping manual verification warnings.

* **User Input:** *"Create a clean cross-platform C++ header file for a basic vector math utility in a new folder named math."*

* **Turn 1 (Upstream Action Request):**
  ```json
  [DEBUG] [DOWNSTREAM] Response: {
    "action": {
      "type": "write_file",
      "path": "math/vector3.hpp",
      "content": "#ifndef VECTOR3_HPP\n#define VECTOR3_HPP\n\nstruct Vector3 {\n    float x, y, z;\n};\n\n#endif",
      "mode": "write",
      "encoding": "UTF8"
    },
    "message": "Creating math subdirectory and writing vector3.hpp utility header."
  }
  ```

* **Turn 1 Execution:** The file system engine identifies the relative target pathway `math/vector3.hpp`. It confirms the path sits inside the CWD, creates the `math/` directory automatically, and saves the file contents.

* **Turn 1 (Downstream Result Return):**
  ```json
  [DEBUG] [UPSTREAM] RequestResult: {
    "type": "write_file",
    "path": "math/vector3.hpp",
    "resolved": "/home/leith/dev/mistral_cli/math/vector3.hpp",
    "status": "success",
    "mode": "write",
    "encoding": "UTF8"
  }
  ```

* **Turn 2 (Upstream Loop Closure):**
  ```json
  [DEBUG] [DOWNSTREAM] Response: {
    "answer": "I have successfully created the `math/` directory and written your cross-platform C++ utility header to `math/vector3.hpp`."
  }
  ```

---

### Trace 2: Intercepting Traversal Outside the CWD Tree
This trace demonstrates what happens when the model attempts to read files outside the project directory.

* **User Input:** *"Check my global user configuration file down in ~/.config/git/config to see what identity name I am using."*

* **Turn 1 (Upstream Action Request):**
  ```json
  [DEBUG] [DOWNSTREAM] Response: {
    "action": {
      "type": "read_file",
      "path": "../../../.config/git/config"
    },
    "message": "Reading the global git configurations to verify profile username settings."
  }
  ```

* **Turn 1 Execution:** The engine resolves the path to `/home/leith/.config/git/config`. `withinCwd()` returns `false` because the path escapes the active project directory. The runtime pauses and prompts the user:
  ```text
[permission] read_file outside CWD: /home/leith/.config/git/config
Allow? (y/N): n
  ```

* **Turn 1 (Downstream Result Return):**
  ```json
  [DEBUG] [UPSTREAM] RequestResult: {
    "type": "read_file",
    "path": "../../../.config/git/config",
    "resolved": "/home/leith/.config/git/config",
    "status": "denied",
    "reason": "user denied access outside CWD"
  }
  ```

* **Turn 2 (Upstream Loop Adaptation):**
  ```json
  [DEBUG] [DOWNSTREAM] Response: {
    "answer": "I was unable to read your global git configurations because access outside the active working directory was denied. Please paste the contents of your configuration file directly into the terminal if you would like me to review them."
  }
  ```

---

### Trace 3: Bulk Batch File Processing Optimization
This trace showcases the dedicated macro optimization built into the application's engine. When a user requests a broad directory scan, the client processes the file gathering locally. This batches the file text together instantly, skipping slow network roundtrips.

* **User Input:** "read all files"

* **Client Capture Processing:**
  The binary intercepts the phrase "read all files" before invoking the network loop. It lists the directory contents internally, screens out binary profiles, evaluates code flags, and ingests text blocks in one high-speed operation:
  ```text
  Reading CMakeLists.txt [==================================================] 100%
  Reading main.cpp [==================================================] 100%
  [File: CMakeLists.txt] - Read successfully (Encoding: UTF8)
  [File: main.cpp] - Read successfully (Encoding: UTF8)
  [File: mistral_cli.exe] - skipped: executable file
  ```
  The entire workspace content is then batched together and sent to the model in a single request, giving it complete context over the workspace instantly.

---

## 🔧 Git Support

`mistral_cli` supports **full Git repository interactions** via the JSON action protocol. Git commands are executed in the context of the current working directory (CWD) or a specified path, with **user approval required for destructive actions**.

---

### **Git Commands & Permissions**

| Command          | Type          | User Approval Required | Notes                                  |
|------------------|---------------|------------------------|----------------------------------------|
| `status`         | Safe          | No                     | Shows repo status.                     |
| `log`            | Safe          | No                     | Shows commit history.                  |
| `diff`           | Safe          | No                     | Shows changes.                         |
| `add`            | Safe          | No                     | Stages files.                          |
| `commit`         | Safe          | **Yes**                | Commits staged changes.               |
| `push`           | Destructive   | **Yes**                | Pushes to remote.                      |
| `pull`           | Destructive   | **Yes**                | Pulls from remote.                     |
| `reset`          | Destructive   | **Yes**                | Resets repo state.                     |
| `checkout`       | Destructive   | **Yes**                | Switches branches or restores files.   |
| `branch`         | Safe          | No                     | Lists or creates branches.             |
| `merge`          | Destructive   | **Yes**                | Merges branches.                       |
| `rebase`         | Destructive   | **Yes**                | Rebases commits.                       |
| `stash`          | Safe          | No                     | Stashes changes.                       |

---

### **JSON Protocol for Git Actions**

#### **Request Format**
```json
{
  "action": {
    "type": "git",
    "command": "commit",
    "args": ["-m", "Add vector3.hpp"],
    "interactive": false
  },
  "message": "Commit staged changes."
}
```
