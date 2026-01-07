#include "../terminal.hpp"

static std::string listDirectory(const std::string& path) {
    std::vector<std::string> entries;

    NUC_DIR* dir = nuc_opendir(path.c_str());
    if (!dir) {
        return "ls: Failed to open directory " + path;
    }
    nuc_dirent* entry;
    while ((entry = nuc_readdir(dir)) != nullptr) {
        entries.push_back(entry->d_name);
    }
    nuc_closedir(dir);

    std::string result;
    result += " Contents of " + path + ":\n";
    result += " Total entries: " + std::to_string(entries.size()) + "\n";
    result += " ---------------------\n";
    for (const auto& name : entries) {
        result += " " + name + "\n";
    }
    return result;
}

CommandHandler GetlsCommandHandler() {
    return CommandHandler{
        "ls",
        [](const std::vector<std::string>& args) -> std::string {
            if (args.size() > 2) {
                return "Usage: ls or ls <directory>";
            }
            if (args.size() == 1) {
                return listDirectory(".");
            }
            const std::string& directory = args[1];
            return listDirectory(directory);
        }
    };
}