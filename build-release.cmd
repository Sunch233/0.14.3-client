@echo off
chcp 65001 >nul
setlocal EnableExtensions DisableDelayedExpansion
set "buildExitCode=1"
pushd "%~dp0"
if errorlevel 1 goto finish

rem Prefer the configured JDK, then Android Studio's bundled runtime.
if defined JAVA_HOME if exist "%JAVA_HOME%\bin\java.exe" goto sdk
if exist "%ProgramFiles%\Android\Android Studio\jbr\bin\java.exe" (
    set "JAVA_HOME=%ProgramFiles%\Android\Android Studio\jbr"
    goto sdk
)
if exist "%LOCALAPPDATA%\Programs\Android Studio\jbr\bin\java.exe" (
    set "JAVA_HOME=%LOCALAPPDATA%\Programs\Android Studio\jbr"
    goto sdk
)
set "JAVA_HOME="
where java.exe >nul 2>&1
if not errorlevel 1 goto sdk
echo 未找到 Java，请安装 Android Studio 或配置 JAVA_HOME。
goto done

:sdk
rem Gradle prefers local.properties; otherwise use the configured or default SDK.
if exist "%~dp0local.properties" goto build
if defined ANDROID_HOME goto build
if defined ANDROID_SDK_ROOT (
    set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
    goto build
)
if exist "%LOCALAPPDATA%\Android\Sdk" (
    set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
    goto build
)
echo 未找到 Android SDK，请配置 local.properties 中的 sdk.dir 或 ANDROID_HOME。
goto done

:build
echo 正在构建 MCPE0.14 Sunch Client Release APK...
call "%~dp0gradlew.bat" :app:assembleRelease --console=plain
set "buildExitCode=%ERRORLEVEL%"
if not "%buildExitCode%"=="0" goto failed
if exist "%~dp0app\build\outputs\apk\release\app-release.apk" goto success
set "buildExitCode=1"
echo 未找到预期的 APK 文件。
goto failed

:success
echo.
echo 构建成功！APK 路径：
echo "%~dp0app\build\outputs\apk\release\app-release.apk"
goto done

:failed
echo.
echo 构建失败，退出码：%buildExitCode%。请查看上方错误信息。

:done
popd
:finish
echo.
if /i not "%~1"=="--no-pause" pause
exit /b %buildExitCode%
