#include "../terminal.hpp"

#include <syscall.h>

static int errno_check(int result)
{
	if(result != -1)
		return result;

	errno = *syscall<e_errno_addr, int*>();
	return -1;
}

static int chdir2(const char* path) {
    return errno_check(syscall<e_chdir, int>(path));
}

CommandHandler GetcdCommandHandler() {
    return CommandHandler{
        "cd",
        [](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) {
                return "Usage: cd <directory>";
            }
            const std::string& directory = args[1];
            if(chdir2(directory.c_str())) {
                std::string strerr = strerror(errno);
                return "cd: " + strerr + "\n";
            }
            return "";
        }
    };
}