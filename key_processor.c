#include "command.h"

KeyBinding g_keyBindings[128];
int8_t g_numberOfKeyBindings;

void add_key_bindings(int modifers, WORD key, Mode mode, Command *command)
{
    g_keyBindings[0] = {
        modifiers = 0,
        key = key,
        mode = mode,
        command = command,
    };
}

void key_processor_process(Mode mode, WORD key, int modifiers)
{
    for(int8_t i = 0; i < g_numberOfKeyBindings; i++)
    {
        KeyBinding keyBinding = g_keyBindings[i];
        if(keyBinding[0].mode == mode && keyBinding.modifiers == modifiers && key == keyBinding.key)
        {
            keyBinding.command->action();
            break;
        }
    }
}
