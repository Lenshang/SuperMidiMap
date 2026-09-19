@echo off
rem SuperMidiMap 一键构建脚本（MSVC + Ninja + Qt）
setlocal
set "QTDIR=D:\Qt\6.10.3\msvc2022_64"
if not exist "%QTDIR%" (
    echo [错误] 未找到 Qt: %QTDIR%
    exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [错误] 无法加载 MSVC 编译环境
    exit /b 1
)
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%QTDIR%" %*
if errorlevel 1 exit /b 1
cmake --build "%~dp0build"
if errorlevel 1 exit /b 1
rem 收集 Qt 运行库，使 exe 可直接运行/分发
"%QTDIR%\bin\windeployqt.exe" --no-compiler-runtime "%~dp0build\SuperMidiMap.exe" >nul 2>&1
echo.
echo 构建完成: build\SuperMidiMap.exe  和  build\midi-probe.exe
