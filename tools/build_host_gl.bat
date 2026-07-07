@echo off
setlocal

if not exist outputs mkdir outputs

cl /nologo /Iinclude ^
  src\host\win32_gl_main.c ^
  src\host\host_cselect.c ^
  src\runtime\module_system.c ^
  src\runtime\boot_logo.c ^
  src\render\render_engine.c ^
  src\platform\win32_gl_backend.c ^
  src\resource\resource_manager.c ^
  src\select\csel_mode.c ^
  /Feoutputs\ddrii_host_gl.exe ^
  /link user32.lib gdi32.lib opengl32.lib

endlocal
