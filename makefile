outdir = tmp
publishdir = bin
winlibs = Gdi32.lib user32.lib ComCtl32.lib
nowarncflags = /c /EHsc /nologo /DUNICODE /D_UNICODE /Zi
cflags = $(nowarncflags) /c /W4 /EHsc /nologo /DUNICODE /D_UNICODE /Zi

all: clean ntasklistnative.exe

clean:
	if exist "$(outdir)" rd /s /q $(outdir)
	mkdir $(outdir)

fzf.obj:
	CL $(nowarncflags) fzf\fzf.c /Fd"$(outdir)\fzf.pdb" /Fo"$(outdir)\fzf.obj"

.c.obj:
	CL /c $(cflags) $*.c /Fd"$(outdir)\$*.pdb" /Fo"$(outdir)\$*.obj"

ntasklistnative.exe: fzf.obj ntasklistnative.obj
	LINK /DEBUG $(outdir)\fzf.obj  $(outdir)\ntasklistnative.obj $(winlibs) Shlwapi.lib Advapi32.lib Dwmapi.lib Shell32.lib /OUT:$(outdir)\ntasklist.exe

publish:
	rd /s /q $(publishdir)
	xcopy $(outdir) $(publishdir)\ /E
