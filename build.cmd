@ECHO off
rd /s /q bin
mkdir bin

cl.exe /c /W4 /EHsc /nologo fzf\fzf.c /DUNICODE /D_UNICODE /Zi /Fd"bin\fzf.pdb" /Fo"bin\fzf.obj"

cl.exe /c /W4 /EHs -nologo -Ic:\src\pdcurses ntasklist.c /Zi /Fd"bin\ntasklist.pdb" /Fo"bin\ntasklist.obj"
link /DEBUG bin\ntasklist.obj bin\fzf.obj c:\src\PDCurses\wincon\pdcurses.lib user32.lib Advapi32.lib Kernel32.lib Shlwapi.lib /OUT:bin\ntasklist.exe
