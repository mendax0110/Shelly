#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <queue>
#include <algorithm>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>

#include "./include/shell.h"

using namespace std;
using namespace shelly;


const string Shell::whitespace = " \t\n\r\f\v";
Shell *Shell::current_instance = nullptr;

Shell::Shell(char **env) : env_ptr(env), userID(getpwuid(getuid())->pw_name), curr_id(0)
{
    auto userID = getpwuid(getuid())->pw_name;
    auto binDir = "/bin:/usr/bin";
    
    current_instance = this;
    signal(SIGCHLD, &Shell::handler);
    signal(SIGTSTP, &Shell::bg_handler);

    commands = {
        {"exit", &Shell::stop_shell},
        {"set", &Shell::setVar},
        {"unset", &Shell::unsetVar},
        {"prt", &Shell::prtWord},
        {"envset", &Shell::setEnv},
        {"envunset", &Shell::unsetEnv},
        {"envprt", &Shell::envPrt},
        {"witch", &Shell::witch},
        {"pwd", &Shell::pwd},
        {"cd", &Shell::cd},
        {"lim", &Shell::limit},
        {"jobs", &Shell::jobs},
        {"fg", &Shell::fg},
        {"bg", &Shell::bg},
        {"kill", &Shell::kill_id},
        {"shmalloc", &Shell::shmalloc},
        {"shmdel", &Shell::shmdel}
    };

    clearEnvironment();
    setenv(userID, binDir, 0);
    setenv("PATH", getCurrentDir(), 0);

    getrlimit(RLIMIT_CPU, &cpu_lim);
    getrlimit(RLIMIT_AS, &mem_lim);
}

char *Shell::getCurrentDir(void)
{
#if defined(__linux__)
    get_current_dir_name();
#endif

#if defined(__APPLE__)
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
    {
        cout << cwd << endl;
    }
    else
    {
        cout << "Error getting current directory" << endl;
    }
#endif
}

void Shell::clearEnvironment()
{
#if defined(__APPLE__)
    for (char **env = environ; *env != nullptr; ++env)
    {
        char *env_var = *env;
        char *name_end = strchr(env_var, '=');
        if (name_end != nullptr)
        {
            *name_end = '\0';
            unsetenv(env_var);
        }
    }
#endif

#if defined(__linux__)
    clearenv();
#endif
}

void Shell::startShell()
{
    string line, cmd;
    size_t pos;
    bool sync;

    while (keepLooping)
    {
        while (!fin_children.empty())
        {
            auto temp = fin_children.front();
            cout << "Finished executing: pid: " << temp.pid << " id: " << temp.id << " prog: " << temp.p_name << endl;
            fin_children.pop();
        }

        sync = true;
        if (isatty(STDIN_FILENO))
        {
            cout << userID << "_sh> ";
        }

        if (getline(cin, line).eof())
        {
            if (isatty(STDIN_FILENO))
                cout << endl;
            break;
        }

        pos = line.find("#");
        line = line.substr(0, pos);
        if (!line.empty())
        {
            interpolate(line);
            pos = line.find('|');
            if (pos != string::npos)
            {
                pipe(pipe_fds);
                if (line.find_last_not_of(whitespace) == '&')
                {
                    sync = false;
                    line = line.substr(0, line.find_last_not_of(whitespace));
                }
                int rc1 = fork();
                if (rc1 == -1)
                {
                    cout << "fork failed" << endl;
                    exit(-1);
                }
                if (rc1 == 0)
                {    // child 1
                    close(pipe_fds[0]);
                    close(1);
                    dup(pipe_fds[1]);
                    close(pipe_fds[1]);
                    line = line.substr(line.find_first_not_of(whitespace), pos);
                    cmd = line.substr(0, line.find(' ', 0));
                    callCommand(cmd, line.substr(cmd.length()));
                    close(1);
                    exit(0);
                }
                int rc2 = fork();
                if (rc2 == -1)
                {
                    cout << "fork failed" << endl;
                    exit(-1);
                }
                if (rc2 == 0)
                {   // child 2
                    close(pipe_fds[1]);
                    close(0);
                    dup(pipe_fds[0]);
                    close(pipe_fds[0]);
                    line = line.substr(pos + 1);
                    line = line.substr(line.find_first_not_of(whitespace));
                    cmd = line.substr(0, line.find(' ', 0));
                    callCommand(cmd, line.substr(cmd.length()));
                    exit(0);
                }
                close(pipe_fds[0]);
                close(pipe_fds[1]);
                if (sync)
                {
                    int s;
                    waitpid(rc2, &s, 0);
                }
            }
            else
            {
                line = line.substr(line.find_first_not_of(whitespace));
                cmd = line.substr(0, line.find(' ', 0));
                callCommand(cmd, line.substr(cmd.length()));
            }
        }
    }
}

void Shell::startShell(string filename)
{
    ifstream myIn(filename);
    string line, cmd;
    size_t pos;
    while (getline(myIn, line) && keepLooping)
    {
        pos = line.find("#");
        line = line.substr(0, pos);
        if (!line.empty())
        {
            interpolate(line);
            cmd = line.substr(0, line.find(' ', 0));
            callCommand(cmd, line.substr(cmd.length()));
        }
    }
    myIn.close();
}

void Shell::stop_shell(string arg)
{
    keepLooping = false;
}

void Shell::setVar(string arg)
{
    string varName, varVal;
    istringstream ss(arg);
    ss >> varName >> varVal;
    shellVars[varName] = varVal;
}

void Shell::unsetVar(string arg)
{
    string varName;
    istringstream ss(arg);
    ss >> varName;
    auto iter = shellVars.find(varName);
    if (iter != shellVars.end())
        shellVars.erase(iter);
    else
        cout << varName << " does not exist" << endl;
}

void Shell::prtWord(string arg)
{
    string wordOrVar;
    istringstream ss(arg);
    while (ss >> wordOrVar)
    {
        cout << wordOrVar << " ";
    }
    cout << endl;
}

void Shell::setEnv(string arg)
{
    string varName, varVal;
    istringstream ss(arg);
    ss >> varName >> varVal;
    setenv(varName.c_str(), varVal.c_str(), 1);
}

void Shell::unsetEnv(string arg)
{
    string varName;
    istringstream ss(arg);
    ss >> varName;
    unsetenv(varName.c_str());
}

void Shell::envPrt(string arg)
{
    for (char **env = environ; *env != nullptr; ++env)
    {
        cout << *env << endl;
    }
}

void Shell::witch(string arg)
{
    istringstream ss(arg);
    DIR *directory;
    string userPath = getenv("PATH");
    int start = 0, pos = userPath.find(":");

    string dirName, cmdName;
    bool found = false, allDirs = false;
    
    ss >> cmdName;

    if (cmdName == "?")
    {
        for (const auto &command : commands)
        {
            cout << command.first << endl;
        }
        return;
    }

    if (commands.find(cmdName) != commands.end())
    {
        cout << "Built-in command" << endl;
    }
    else if (commands.find(cmdName) == commands.end())
    {
        cout << "Command not found" << endl;
    }
    else
    {
        while (!found && !allDirs)
        {
            dirName = userPath.substr(start, pos);
            directory = opendir(dirName.c_str());
            if (directory)
            {
                struct dirent *entry;
                while ((entry = readdir(directory)) != nullptr)
                {
                    if (entry->d_name == cmdName)
                    {
                        cout << dirName << "/" << entry->d_name << endl;
                        found = true;
                    }
                }
            }
            if (pos != string::npos)
            {
                start = start + pos + 1;
                pos = userPath.substr(start).find(":");
            }
            else
            {
                allDirs = true;
            }
        }
    }
}

void Shell::pwd(string arg)
{
    auto userID = getpwuid(getuid())->pw_name;
    cout << getenv(userID) << endl;
}

void Shell::cd(string arg)
{
    istringstream ss(arg);
    arg = arg.substr(arg.find_first_not_of(whitespace));
    string path;
    if (arg.find_first_not_of(whitespace) != string::npos)
    {
        ss >> path;
        if (chdir(path.c_str()) == 0)
            setenv("PATH", getCurrentDir(), 1);
        else
            cout << path << " doesn't exist" << endl;
    }
}

void Shell::limit(string arg)
{
    int mem_MB;
    if (arg.find_first_not_of(whitespace) == string::npos)
    {
        // Print limit
        if (mem_lim.rlim_max != -1)
        {
            mem_MB = mem_lim.rlim_max / (1024 * 1024);
        }
        else
        {
            mem_MB = -1;
        }
        cout << "Limits\nCPU: " << cpu_lim.rlim_max << " seconds\nMem: " << mem_MB << " MB" << endl;
    }
    else
    {
        // Store Limit
        istringstream ss(arg);
        ss >> cpu_lim.rlim_max >> mem_MB;
        cpu_lim.rlim_cur = cpu_lim.rlim_max;
        mem_lim.rlim_max = mem_lim.rlim_cur = mem_MB * 1024 * 1024;
    }
}

void Shell::jobs(string args)
{
    cout << "Jobs:" << endl;
    if (child_list.empty())
    {
        cout << "No background/suspended jobs" << endl;
    }
    else
    {
        for (const auto &child : child_list)
        {
            cout << "id: " << child.id << " pid: " << child.pid << " status: '" << child.status << "' prog: " << child.p_name << endl;
        }
    }
}

void Shell::fg(string args)
{
    int id;
    istringstream s(args);
    s >> id;

    auto iter = find_if(child_list.begin(), child_list.end(), [&id](const child_info &child) {
        return child.id == id;
    });

    if (iter != child_list.end())
    {
        kill(iter->pid, SIGCONT);
        fg_child = &(*iter);
        change_status(iter->pid, "Running");
        waitpid(iter->pid, nullptr, WUNTRACED);
    }
    else
    {
        cout << "Process " << id << " not found" << endl;
    }
}

void Shell::bg(string args)
{
    int id;
    istringstream s(args);
    s >> id;

    auto iter = find_if(child_list.begin(), child_list.end(), [&id](const child_info &child) {
        return child.id == id;
    });

    if (iter != child_list.end())
    {
        kill(iter->pid, SIGCONT);
        change_status(iter->pid, "Running");
    }
    else
    {
        cout << "Process " << id << " not found" << endl;
    }
}

void Shell::kill_id(string args)
{
    int id;
    istringstream s(args);
    s >> id;

    auto iter = find_if(child_list.begin(), child_list.end(), [&id](const child_info &child) {
        return child.id == id;
    });

    if (iter != child_list.end())
    {
        kill(iter->pid, SIGKILL);
    }
    else
    {
        cout << "Process " << id << " not found" << endl;
    }
}

void Shell::shmalloc(string args)
{
    istringstream s(args);
    string name;
    int size;
    s >> name >> size;
    size *= (1024 * 1024);

    int shmfd = shm_open(name.c_str(), O_CREAT | O_RDWR, S_IRWXU);
    if (shmfd == -1)
    {
        perror("shm_open failed: ");
        exit(-1);
    }

    int rc = ftruncate(shmfd, size);
    if (rc == -1)
    {
        perror("ftruncate failed: ");
        exit(-1);
    }
}

void Shell::shmdel(string args)
{
    istringstream s(args);
    string name;
    s >> name;
    shm_unlink(name.c_str());
}

void Shell::change_status(int pid, const string &stat)
{
    auto iter = find_if(child_list.begin(), child_list.end(), [&pid](const child_info &child) {
        return child.pid == pid;
    });

    if (iter != child_list.end())
    {
        iter->status = stat;
    }
}

void Shell::handler(int s)
{
    pid_t pid;
    int status;
    int count = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0 && count < 10)
    {
        auto iter = find_if(current_instance->child_list.begin(), current_instance->child_list.end(), [&pid](const child_info &child) {
            return child.pid == pid;
        });
        if (current_instance->fg_child == nullptr || (current_instance->fg_child && current_instance->fg_child->pid == pid))
        {
            if (iter != current_instance->child_list.end())
            {
                current_instance->fin_children.push(*iter);
                current_instance->child_list.erase(iter);
            }
            else
            {
                current_instance->fg_child = nullptr;
            }
        }
        count++;
    }
    fflush(stdout);
}

void Shell::bg_handler(int s)
{
    cout << endl;
    int rc = kill(current_instance->fg_child->pid, SIGSTOP);
    if (rc == -1)
    {
        cout << "ERROR" << endl;
    }
    else
    {
        current_instance->change_status(current_instance->fg_child->pid, "Suspended");
        current_instance->fg_child = nullptr;
    }
}

void Shell::callCommand(const string &cmd, const string &args)
{
    vector<char *> arg_list;
    stringstream ss(args);

    if (cmd.find('/') != string::npos)
    {
        bool sync = true;
        if (args[args.find_last_not_of(whitespace)] == '&')
        {
            sync = false;
        }

        int rc = fork();
        if (rc == -1)
        {
            cout << "fork failed" << endl;
            exit(-1);
        }

        if (rc == 0)
        {   // child
            setrlimit(RLIMIT_CPU, &cpu_lim);
            setrlimit(RLIMIT_AS, &mem_lim);
            signal(SIGTSTP, SIG_DFL);
            setpgid(0, 0);

            ss << cmd;
            ss << " ";
            ss << args;
            string single_arg;
            while (ss >> single_arg)
            {
                char *arg = new char[single_arg.size() + 1];
                copy(single_arg.begin(), single_arg.end(), arg);
                arg[single_arg.size()] = '\0';
                arg_list.push_back(arg);
            }
            arg_list.push_back(0);

            execve(arg_list[0], &arg_list[0], environ);

            for (char *arg : arg_list)
            {
                delete[] arg;
            }

            exit(0);
        }
        if (sync)
        {
            struct child_info temp;
            temp.pid = rc;
            temp.id = curr_id;
            temp.p_name = cmd;
            temp.status = "Running";
            child_list.push_front(temp);
            curr_id = (curr_id + 1) % 100000;

            fg_child = &temp;

            int s;
            waitpid(rc, &s, WUNTRACED);
        }
    }
    else
    {
        auto iter = commands.find(cmd);
        if (iter != commands.end())
        {
            (this->*(iter->second))(args);
        }
        else
        {
            bool found = false;
            string userpPath = getenv("PATH");
            size_t start = 0, pos;
            pos = userpPath.find(":");
            while (!found && pos != string::npos)
            {
                string dirName = userpPath.substr(start, pos);
                DIR *directory = opendir(dirName.c_str());
                if (directory)
                {
                    struct dirent *entry;
                    while ((entry = readdir(directory)) != nullptr && !found)
                    {
                        if (entry->d_name == cmd)
                        {
                            callCommand(dirName + '/' + string(entry->d_name), args);
                            found = true;
                        }
                    }
                }
                start = start + pos + 1;
                pos = userpPath.substr(start).find(":");
            }
            if (!found)
            {
                cout << "'" << cmd << "' not available in current path" << endl;
            }
        }
    }
}

void Shell::interpolate(string &line)
{
    size_t pos = line.find("$");
    bool found;
    string sub;
    while (pos != string::npos)
    {
        found = false;
        size_t start = pos + 1;
        size_t len = line.length() - start;
        for (; !found && (len > 0); len--)
        {
            sub = line.substr(start, len);
            if (shellVars.find(sub) != shellVars.end())
            {
                line.replace(pos, len + 1, shellVars[sub]);
                found = true;
            }
            else if (getenv(sub.c_str()))
            {
                line.replace(pos, len + 1, getenv(sub.c_str()));
                found = true;
            }
        }

        if (found)
        {
            pos = line.find("$");
        }
        else
        {
            cout << "Variable " << line.substr(pos) << " not found" << endl;
            break;
        }
    }
}