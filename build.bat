@echo off

IF NOT EXIST ..\..\build mkdir ..\..\build
pushd ..\..\build
cl -DHANDMADE_WIN32=1 -FC -Zi C:\code\handmade\code\win32_handmade.cpp user32.lib gdi32.lib winmm.lib
REM cl /nologo /DHANDMADE_WIN32=1 /FC /Zi C:\code\handmade\code\win32_handmade.cpp ^
  /link /VERBOSE:LIB user32.lib gdi32.lib winmm.lib
popd
