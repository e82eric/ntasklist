#include "command.h"

Command g_commands[128];
uint8_t g_numberOfCommands;

Command *command add_command(char *name, void (*action) (void))
{
    uint8_t index = g_numberOfCommands;
    g_commands[index] = {
        name = name,
        action = action,
    };
    g_numberOfCommands++;

    return &g_commands[index];
}
