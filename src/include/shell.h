#pragma once

#include <map>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <unordered_map>
#include <list>
#include <queue>

extern char **environ;

namespace shelly
{
    class Shell;

    extern Shell* current_instance;

    struct child_info
    {
        int pid;
        int id;
        std::string p_name;
        std::string status;
    };

    class Shell
    {
    private:
        static Shell* current_instance;
        std::string userID;
        char **env_ptr;
        int curr_id;
        bool keepLooping = true;
        std::unordered_map<std::string, std::string> shellVars;
        std::unordered_map<std::string, void (Shell::*)(std::string)> commands;
        std::list<child_info> child_list;
        std::queue<child_info> fin_children;
        struct child_info *fg_child = nullptr;
        struct rlimit cpu_lim, mem_lim;
        int pipe_fds[2];

        static const std::string whitespace;

        // Helper functions
        void change_status(int pid, const std::string &stat);
        static void handler(int s);
        static void bg_handler(int s);
        void interpolate(std::string &line);
        void callCommand(const std::string &cmd, const std::string &args);

        // Command functions
        void stop_shell(std::string arg);
        void setVar(std::string arg);
        void unsetVar(std::string arg);
        void prtWord(std::string arg);
        void setEnv(std::string arg);
        void unsetEnv(std::string arg);
        void envPrt(std::string arg);
        void witch(std::string arg);
        void pwd(std::string arg);
        void cd(std::string arg);
        void limit(std::string arg);
        void jobs(std::string args);
        void fg(std::string args);
        void bg(std::string args);
        void kill_id(std::string args);
        void shmalloc(std::string args);
        void shmdel(std::string args);
        void clearEnvironment();
        char *getCurrentDir(void);

    public:
        Shell(char **env);
        void startShell();
        void startShell(std::string filename);
    };
}