@echo off
FOR /F "eol=| delims=" %%I IN ('DIR "%USERPROFILE%\memory_dumps\ntasklist*.dmp" /A-D /B /O-D /TW 2^>nul') DO SET "NewestFile=%%I" & GOTO FoundFile
ECHO No *.dmp file found!
GOTO :EOF

:FoundFile
cdb -z %USERPROFILE%\memory_dumps\%NewestFile%
