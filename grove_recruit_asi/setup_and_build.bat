@echo off
setlocal enabledelayedexpansion
REM ════════════════════════════════════════════════════════════════
REM setup_and_build.bat
REM Clona o plugin-sdk, compila o grove_recruit_standalone.asi
REM e copia para a pasta do GTA SA automaticamente.
REM
REM COMO USAR:
REM   1. Edite GTA_SA_PATH em baixo (onde está o seu GTA SA)
REM   2. Clique duas vezes neste ficheiro (ou execute como admin)
REM   3. Aguarde — o .asi será copiado para a pasta do GTA SA
REM ════════════════════════════════════════════════════════════════

REM ── EDITAR AQUI ──────────────────────────────────────────────
set GTA_SA_PATH=C:\Program Files (x86)\Rockstar Games\GTA San Andreas
set PLUGIN_SDK_DIR=C:\dev\plugin-sdk
REM ─────────────────────────────────────────────────────────────

echo [1/4] A verificar Git...
where git >nul 2>&1
if errorlevel 1 (
    echo ERRO: Git nao encontrado. Instale em https://git-scm.com
    pause & exit /b 1
)

echo [2/4] A clonar/actualizar plugin-sdk...
if not exist "%PLUGIN_SDK_DIR%\.git" (
    git clone https://github.com/DK22Pac/plugin-sdk.git "%PLUGIN_SDK_DIR%"
) else (
    echo     plugin-sdk ja existe, a actualizar...
    git -C "%PLUGIN_SDK_DIR%" pull --ff-only
)

echo [3/4] A compilar com MSBuild...
REM Procurar MSBuild do Visual Studio 2019 ou 2022 (x86, x64, ARM64)
set MSBUILD=
for /f "delims=" %%p in (
    "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
) do (
    if exist "%%~p" (
        for /f "delims=" %%i in ('"%%~p" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2^>nul') do (
            if "!MSBUILD!"=="" set MSBUILD=%%i
        )
    )
)
if "%MSBUILD%"=="" (
    echo ERRO: Visual Studio nao encontrado (vswhere.exe nao localizado).
    echo Instale o Visual Studio 2019 ou 2022 com workload "Desktop development with C++"
    echo Download: https://visualstudio.microsoft.com/
    pause & exit /b 1
)

set PLUGIN_SDK=%PLUGIN_SDK_DIR%
"%MSBUILD%" grove_recruit_standalone.vcxproj /p:Configuration=Release /p:Platform=Win32 /t:Build /verbosity:minimal
if errorlevel 1 (
    echo ERRO: Compilacao falhou. Ver mensagens acima.
    pause & exit /b 1
)

echo [4/4] A copiar .asi para GTA SA...
if exist "Release\grove_recruit_standalone.asi" (
    copy /Y "Release\grove_recruit_standalone.asi" "%GTA_SA_PATH%\grove_recruit_standalone.asi"
    echo.
    echo ════════════════════════════════════════════════════════
    echo  SUCESSO! grove_recruit_standalone.asi copiado para:
    echo  %GTA_SA_PATH%
    echo ════════════════════════════════════════════════════════
) else (
    echo AVISO: .asi nao encontrado em Release\. Verificar configuracao.
)

pause
