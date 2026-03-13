@echo off
setlocal enabledelayedexpansion
REM ════════════════════════════════════════════════════════════════
REM setup_and_build.bat
REM Clona o plugin-sdk, compila o grove_recruit_standalone.asi
REM e copia para a pasta do GTA SA automaticamente.
REM
REM REQUISITO OBRIGATORIO:
REM   Visual Studio 2019 ou 2022 com workload
REM   "Desktop development with C++" instalado.
REM   ATENCAO: VS Code NAO serve — precisa do Visual Studio completo.
REM   Download gratis: https://visualstudio.microsoft.com/
REM
REM COMO USAR:
REM   1. Edite GTA_SA_PATH em baixo (onde esta o seu GTA SA)
REM   2. Clique duas vezes neste ficheiro
REM   3. Aguarde — o .asi sera copiado para a pasta do GTA SA
REM ════════════════════════════════════════════════════════════════

REM ── EDITAR AQUI ──────────────────────────────────────────────
set GTA_SA_PATH=C:\Program Files (x86)\Rockstar Games\GTA San Andreas
set PLUGIN_SDK_DIR=C:\dev\plugin-sdk
REM ─────────────────────────────────────────────────────────────

echo.
echo ================================================
echo  Grove Recruit Standalone -- Setup e Build
echo ================================================
echo.

REM ── [1/5] Verificar Git ──────────────────────────────────────
echo [1/5] A verificar Git...
where git >nul 2>&1
if errorlevel 1 (
    echo.
    echo ERRO: Git nao encontrado.
    echo Instale em https://git-scm.com e reinicie o cmd.
    pause & exit /b 1
)
echo       OK

REM ── [2/5] Clonar/actualizar plugin-sdk ───────────────────────
echo [2/5] A clonar/actualizar plugin-sdk em %PLUGIN_SDK_DIR%...
if not exist "%PLUGIN_SDK_DIR%\.git" (
    git clone https://github.com/DK22Pac/plugin-sdk.git "%PLUGIN_SDK_DIR%"
    if errorlevel 1 (
        echo ERRO: Falha ao clonar plugin-sdk. Verifique a sua ligacao a internet.
        pause & exit /b 1
    )
) else (
    echo       ja existe, a actualizar...
    git -C "%PLUGIN_SDK_DIR%" pull --ff-only
)
echo       OK

REM ── [3/5] Localizar MSBuild via vswhere ──────────────────────
echo [3/5] A localizar Visual Studio / MSBuild...
set "MSBUILD="
set "VSWHERE="

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

if "%VSWHERE%"=="" (
    echo.
    echo ERRO: Visual Studio nao encontrado.
    echo       O ficheiro vswhere.exe nao existe. Isso significa que o
    echo       Visual Studio (ou Build Tools) nao esta instalado.
    echo.
    echo  SOLUCAO: Instale o Visual Studio Community 2022 ^(gratis^):
    echo    https://visualstudio.microsoft.com/
    echo    Durante a instalacao, seleccione o workload:
    echo    "Desktop development with C++"
    echo.
    echo  NOTA: VS Code NAO serve -- precisa do Visual Studio completo.
    pause & exit /b 1
)

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2^>nul`) do (
    if "!MSBUILD!"=="" set "MSBUILD=%%i"
)

if "!MSBUILD!"=="" (
    echo.
    echo ERRO: MSBuild nao encontrado no Visual Studio instalado.
    echo       Certifique-se que o workload "Desktop development with C++"
    echo       esta instalado no Visual Studio.
    pause & exit /b 1
)
echo       OK: !MSBUILD!

REM ── [4/5] Compilar plugin_sa.lib (SDK) se necessario ─────────
echo [4/5] A verificar plugin_sa.lib...
if not exist "%PLUGIN_SDK_DIR%\output\plugin_sa.lib" (
    echo       Lib nao encontrada, a compilar plugin_sa a partir do SDK...
    echo       ^(Isso pode demorar 1-2 minutos na primeira vez^)
    if exist "%PLUGIN_SDK_DIR%\plugin_sa\plugin_sa.vcxproj" (
        "!MSBUILD!" "%PLUGIN_SDK_DIR%\plugin_sa\plugin_sa.vcxproj" ^
            /p:Configuration=Release /p:Platform=Win32 ^
            /t:Build /verbosity:minimal
        if errorlevel 1 (
            echo.
            echo ERRO: Falha ao compilar plugin_sa.lib.
            echo       Abra "%PLUGIN_SDK_DIR%\plugin_sa\plugin_sa.vcxproj" no
            echo       Visual Studio, seleccione Release^|Win32 e compile manualmente.
            pause & exit /b 1
        )
    ) else (
        echo AVISO: plugin_sa.vcxproj nao encontrado em %PLUGIN_SDK_DIR%\plugin_sa\
        echo        A tentar continuar -- pode ocorrer erro de linker...
    )
) else (
    echo       OK: plugin_sa.lib ja existe
)

REM ── [5/5] Compilar grove_recruit_standalone.asi ───────────────
echo [5/5] A compilar grove_recruit_standalone.asi...
cd /d "%~dp0"
set "PLUGIN_SDK=%PLUGIN_SDK_DIR%"
"!MSBUILD!" "%~dp0grove_recruit_standalone.vcxproj" ^
    /p:Configuration=Release /p:Platform=Win32 ^
    /p:PLUGIN_SDK="%PLUGIN_SDK_DIR%" ^
    /t:Build /verbosity:minimal
if errorlevel 1 (
    echo.
    echo ERRO: Compilacao falhou. Ver mensagens acima.
    echo       Se o erro for "Cannot open include file: plugin.h":
    echo         - Verifique que o plugin-sdk esta em %PLUGIN_SDK_DIR%
    echo       Se o erro for "cannot open file plugin_sa.lib":
    echo         - Execute novamente este script (o passo 4 deve resolver)
    pause & exit /b 1
)

REM ── Copiar .asi para GTA SA ───────────────────────────────────
set "ASI_FILE=%~dp0Release\grove_recruit_standalone.asi"
if exist "!ASI_FILE!" (
    if exist "%GTA_SA_PATH%" (
        copy /Y "!ASI_FILE!" "%GTA_SA_PATH%\grove_recruit_standalone.asi"
        echo.
        echo ════════════════════════════════════════════════════════
        echo  SUCESSO!
        echo  grove_recruit_standalone.asi copiado para:
        echo  %GTA_SA_PATH%
        echo.
        echo  Falta apenas instalar o ASI Loader se ainda nao o fez:
        echo  https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases
        echo  ^(copie d3d8.dll para a pasta do GTA SA^)
        echo ════════════════════════════════════════════════════════
    ) else (
        echo.
        echo AVISO: Pasta do GTA SA nao encontrada em:
        echo        %GTA_SA_PATH%
        echo.
        echo O .asi foi compilado em:
        echo   !ASI_FILE!
        echo Copie-o manualmente para a pasta do GTA SA.
    )
) else (
    echo AVISO: .asi nao encontrado em Release\. Verificar configuracao.
)

echo.
pause
