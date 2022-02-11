compile = "gcc --std=gnu99 -g -Wall -o smallsh main.c"
run = "./smallsh"

A shell program in C that implements process management, variable expansion, input and output redirection, and signal handling

Features:
- Provide a prompt that handles blank lines and # comments
- Variable expansion of $$ to shell pid
- Custom commands: exit, cd, status
- Execute other commands through child proccesses
- Support input and output redirection
- Support running commands in foreground and background processes
- Implement custom handlers for 2 signals, SIGINT and SIGTSTP
