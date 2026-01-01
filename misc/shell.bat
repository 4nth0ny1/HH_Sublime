@echo off
subst w: c:\code
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxillary\Build\vcvarsall.bat" x64
set path=w:\handmade\misc:%path%

