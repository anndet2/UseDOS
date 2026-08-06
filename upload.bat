@echo off
chcp 65001 >nul
echo ============================================
echo   UseDOS -> GitHub
echo   https://github.com/anndet2/UseDOS
echo ============================================
echo.

where git >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未检测到 Git，请先安装: https://git-scm.com/downloads
    pause
    exit /b 1
)

cd /d "%~dp0"

if exist ".git" (
    rmdir /s /q ".git"
)

git init
git config user.name "anndet2"
git config user.email "anndet2@users.noreply.github.com"
git add .
git commit -m "Initial commit: UseDOS - A simple operating system built from scratch in C and x86 Assembly"
git remote add origin https://github.com/anndet2/UseDOS.git
git branch -M main

echo.
echo 正在推送到 GitHub，请稍候...
echo.

git push -f -u origin main

if %errorlevel% equ 0 (
    echo.
    echo ============================================
    echo   上传成功！
    echo   https://github.com/anndet2/UseDOS
    echo ============================================
) else (
    echo.
    echo [失败] 请检查网络、GitHub仓库是否已创建、以及登录状态
)

echo.
pause
