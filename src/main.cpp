#include <iostream>
#include <string>

#include "./include/shell.h"

using namespace shelly;

int main(int argc, char **argv, char **envp)
{
    Shell shell(envp);

    if (argc > 1)
    {
        shell.startShell(argv[1]);
    }
    else
    {
        shell.startShell();
    }
}