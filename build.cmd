@ECHO off
rd /s /q bin
mkdir bin

cl.exe /c /W4 /EHs -nologo -Ic:\src\pdcurses helloworld.c /Zi /Fd"bin\helloworld.pdb" /Fo"bin\helloworld.obj"
link /DEBUG bin\helloworld.obj c:\src\PDCurses\wincon\pdcurses.lib user32.lib Advapi32.lib Kernel32.lib Shlwapi.lib /OUT:bin\helloworld.exe

cl.exe /c /W4 /EHs -nologo -Ic:\src\pdcurses trymove.c /Zi /Fd"bin\trymove.pdb" /Fo"bin\trymove.obj"
link /DEBUG bin\trymove.obj c:\src\PDCurses\wincon\pdcurses.lib user32.lib Advapi32.lib Kernel32.lib Shlwapi.lib /OUT:bin\trymove.exe
