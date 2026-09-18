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

# Mistral CLI

A **lightweight, cross-platform** command-line interface for interacting with Mistral AI's API. Designed for **local use**, it supports chat, file operations, shell commands, Git integration, and more—all while maintaining **platform independence** and **minimal dependencies**.

---

## Table of Contents
1. [Features](#features)
2. [Installation](#installation)
3. [Usage](#usage)
4. [Commands](#commands)
5. [Configuration](#configuration)
6. [Build System](#build-system)
7. [License](#license)

---

## Features

### Core Functionality
- **Chat with Mistral**: Interact with Mistral's API in a conversational manner.
- **Multi-Turn Conversations**: Maintain context across multiple turns.
- **File Operations**: Read, write, and manage files directly from the CLI.
- **Shell Integration**: Execute local shell commands without leaving the CLI.
- **Git Integration**: Run Git commands and retrieve repository context.
- **Image Uploads**: Upload and analyze images (PNG, JPEG, WebP) with Mistral's vision capabilities.

### Chat History Management
- **Save/Load History**: Persist chat history to/from JSON files.
- **Auto-Save**: Automatically save chat history after each turn (configurable).
- **Default History File**: `chat_history.json`.

### Configuration
- **Persistent Settings**: Save and load settings (model, temperature, max tokens, auto-save) to/from `.mistral_cli.json`.
- **Customizable**: Configure API settings, model parameters, and CLI behavior.

### Cross-Platform Support
- **Linux**: Fully supported.
- **macOS**: Fully supported.
- **Windows**: Supported with minor adjustments (e.g., shell commands).

---

## Installation

### Prerequisites
- **C++17 or later**
- **libcurl**: For HTTP requests to Mistral's API.
- **nlohmann/json**: For JSON parsing (included as a header-only library).
- **Mistral API Key**: Required for authentication. Set via environment variable or interactive prompt.

### Dependencies
Install the required dependencies based on your platform:

#### Linux (Debian/Ubuntu)
```bash
sudo apt-get update
sudo apt-get install -y g++ make libcurl4-openssl-dev
```

#### macOS (Homebrew)
```bash
brew install curl
```

#### Windows (MSYS2/MinGW)
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-curl
```

---

## Build System

### Build Scripts
The project includes **platform-specific build scripts** to simplify compilation:

#### Linux/macOS
Run the `BuildLinux.sh` script to compile the CLI:
```bash
chmod +x BuildLinux.sh
./BuildLinux.sh
```

#### Windows
Use the `BuildWindows.bat` script:
```batch
BuildWindows.bat
```

### Manual Build
Alternatively, compile manually with:
```bash
g++ -std=c++17 -o mistral_cli mistral_cli.cpp -lcurl -I./include
```

---

## Usage

### Command-Line Arguments

| Argument            | Description                                                                                     |
|--------------------|-------------------------------------------------------------------------------------------------|
| `-debug`           | Enable debug mode for verbose output (e.g., API requests, errors).                          |
| `-autosave`        | Enable auto-saving of chat history after each turn (default file: `chat_history.json`).       |
| `--model <name>`   | Set the model (e.g., `--model mistral-large-latest`).                                         |
| `--temperature <n>`| Set the temperature for model responses (e.g., `--temperature 0.7`).                          |
| `--max-tokens <n>` | Set the maximum number of tokens for model responses (e.g., `--max-tokens 100`).             |
| `--seed <n>`       | Set the random seed for reproducible outputs (e.g., `--seed 42`).                              |

### Starting the CLI
```bash
./mistral_cli -autosave --model mistral-large-latest --temperature 0.7
```

---

## Commands

### Global Actions (No Prefix)
These commands are **local to the CLI** and do not interact with remote APIs.

| Command                     | Description                                                                                     |
|-----------------------------|-------------------------------------------------------------------------------------------------|
| `quit` or `exit`            | Exit the CLI.                                                                                   |
| `clear` or `reset`          | Clear the current chat history.                                                                 |
| `history save [filename]`   | Save chat history to a file. If no filename is provided, defaults to `chat_history.json`.       |
| `history load [filename]`   | Load chat history from a file. If no filename is provided, defaults to `chat_history.json`.   |
| `history clear`             | Clear the current chat history.                                                                 |
| `history list`              | List all saved history files (`.json`) in the current directory.                              |
| `help`                     | Show the help message with all available commands.                                             |

---

### Local Shell Actions (Prefix: `!`)
These commands execute **local shell or Git operations**.

| Command               | Description                                                                                     |
|-----------------------|-------------------------------------------------------------------------------------------------|
| `!run <command>`      | Execute a local shell command (e.g., `!run ls -l`).                                             |
| `!git <command>`      | Execute a Git command (e.g., `!git status`).                                                    |

---

### Remote Actions (Prefix: `/`)
These commands interact with **Mistral's API** or remote resources.

| Command               | Description                                                                                     |
|-----------------------|-------------------------------------------------------------------------------------------------|
| `/models`             | List all available Mistral models and their capabilities (e.g., vision, tools).                 |
| `/model`              | Show the current model in use.                                                                   |
| `/model <name>`       | Set the current model (e.g., `/model mistral-large-latest`).                                   |
| `/image <path>`       | Upload an image for analysis (PNG/JPEG/WebP, max 10MB). Mistral will analyze the image.         |

---

### Config Actions (Prefix: `/`)
These commands manage **CLI configuration**.

| Command               | Description                                                                                     |
|-----------------------|-------------------------------------------------------------------------------------------------|
| `/config save`        | Save current settings (model, temperature, max tokens, auto-save) to `.mistral_cli.json`.       |
| `/config load`        | Load settings from `.mistral_cli.json`.                                                       |
| `/autosave on|off`    | Enable or disable auto-saving of chat history after each turn.                                  |

---

## Examples

### Basic Chat
```bash
Input: Hello, how are you?
Mistral: I'm doing well, thank you! How can I assist you today?
```

### Save and Load Chat History
```bash
# Save the current chat history
Input: history save my_session.json

# Clear the current history
Input: history clear

# Load a saved history
Input: history load my_session.json
```

### Upload an Image
```bash
Input: /image my_diagram.png
Input: Describe this diagram.
```

### Configure Settings
```bash
# Save current settings to a config file
Input: /config save

# Enable auto-save
Input: /autosave on

# Load settings from the config file
Input: /config load
```

### Local and Remote Commands
```bash
# Run a local shell command
Input: !run ls -l

# Execute a Git command
Input: !git status

# List available Mistral models
Input: /models

# Set the current model
Input: /model mistral-large-latest
```

---

## Configuration

### Configuration File
The CLI uses a **JSON configuration file** (`.mistral_cli.json`) to persist settings. This file is created automatically when you run `/config save`.

#### Example `.mistral_cli.json`
```json
{
  "model": "mistral-large-latest",
  "temperature": 0.7,
  "max_tokens": 100,
  "autoSave": true
}
```

#### Fields
| Field         | Description                                                                                     | Default Value               |
|---------------|-------------------------------------------------------------------------------------------------|-----------------------------|
| `model`       | The Mistral model to use for chat completions.                                                 | `mistral-large-latest`      |
| `temperature` | Controls the randomness of the model's output. Range: `0.0` to `1.0`.                           | `0.7`                       |
| `max_tokens`  | Maximum number of tokens to generate in a response. `0` for no limit.                           | `0`                         |
| `autoSave`    | Enable or disable auto-saving of chat history after each turn.                                  | `false`                     |

---

## Build System

### Overview
The project includes **platform-specific build scripts** to simplify compilation and ensure consistency across platforms. The build system is designed to be **lightweight** and **self-contained**, with minimal dependencies.

### Build Scripts

#### Linux/macOS (`BuildLinux.sh`)
This script:
1. Checks for required dependencies (`g++`, `libcurl`).
2. Compiles the CLI with C++17 support.
3. Links against `libcurl` for HTTP requests.
4. Outputs the binary as `mistral_cli`.

**Usage:**
```bash
chmod +x BuildLinux.sh
./BuildLinux.sh
```

#### Windows (`BuildWindows.bat`)
This script:
1. Checks for `g++` and `curl` dependencies.
2. Compiles the CLI with C++17 support.
3. Links against `libcurl` for HTTP requests.
4. Outputs the binary as `mistral_cli.exe`.

**Usage:**
```batch
BuildWindows.bat
```

### Manual Build
If you prefer to build manually, use the following command:
```bash
g++ -std=c++17 -o mistral_cli mistral_cli.cpp -lcurl -I./include
```

---

## API Key Setup

### Environment Variable
The CLI reads the Mistral API key from the `MISTRAL_API_KEY` environment variable. If not set, it will prompt you to enter the key interactively.

**Set the API key temporarily:**
```bash
export MISTRAL_API_KEY="your_api_key_here"
./mistral_cli
```

**Set the API key permanently:**
Add the following line to your shell configuration file (e.g., `~/.bashrc`, `~/.zshrc`):
```bash
export MISTRAL_API_KEY="your_api_key_here"
```

### Encrypted Storage
The CLI **automatically encrypts and saves** your API key to a local file (`~/.minstral/api_key.enc` on Linux/macOS or `%APPDATA%\minstral\api_key.enc` on Windows) using a **system-specific hash**. This ensures your key is not stored in plaintext.

---

## License

This project is licensed under the **GNU General Public License (GPL) v3.0**. See the [LICENSE](LICENSE) file for details.

```
Copyright (C) 2026 Leith Ketchell

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://gnu.org/licenses/>.
```

---

## Contributing

Contributions are welcome! Please open an issue or submit a pull request on [GitHub](https://github.com/your-repo/mistral_cli).

---

## Support

For issues or questions, please open an issue on the [GitHub repository](https://github.com/your-repo/mistral_cli).
