#include "ntasklist.h"

typedef struct Command
{
    char *name;
    void (*action)(void);
} Command;

typedef struct KeyBinding
{
    int modifiers;
    unsigned int key;
    Mode mode;
    Command *command;
} KeyBinding;
