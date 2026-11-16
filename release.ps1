# GitMenuTool 发布脚本：编译 → 从 main.cpp 读 APP_VERSION → 建同名发布分支（内含新版 exe）→ 强推到 Gitea
# 用法: powershell -ExecutionPolicy Bypass -File release.ps1
# 说明: 在线升级约定 = 发布分支名即版本号(vYYYYMMDDx)，分支根目录只放 GitMenuTool.exe；
#       重新发布同版本号属预期覆盖（旧 exe 无升级代码，不会误弹窗）。
$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

# 1) 从源码读版本号与更新源地址（保证三者天然一致）
$src = Get-Content -Raw -Encoding UTF8 main.cpp
$ver = [regex]::Match($src, 'APP_VERSION\[\] = L"([^"]+)"').Groups[1].Value
$url = [regex]::Match($src, 'UPDATE_REPO_URL\[\] = L"([^"]+)"').Groups[1].Value
if (-not $ver -or -not $url) { Write-Error "main.cpp 里读不到 APP_VERSION / UPDATE_REPO_URL" }
Write-Host "版本: $ver  更新源: $url"

# 2) 编译（exe 被运行中实例占用时先按约定结束进程）
$p = Get-Process GitMenuTool -ErrorAction SilentlyContinue
if ($p) { $p | Stop-Process -Force; Start-Sleep -Milliseconds 500 }
& cmd /c build.bat
if ($LASTEXITCODE -ne 0) { Write-Error "build.bat 编译失败" }

# 3) 孤儿发布分支：临时目录只放 GitMenuTool.exe，push 成 refs/heads/<版本号>
$tmp = Join-Path $env:TEMP "GMT_release_$ver"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory $tmp | Out-Null
Copy-Item GitMenuTool.exe (Join-Path $tmp "GitMenuTool.exe")
Push-Location $tmp
git init -q
git checkout -q -b $ver
git add GitMenuTool.exe
git -c user.name=release -c user.email=release@local commit -q -m "release $ver"
git push -f $url $ver
Pop-Location
Remove-Item -Recurse -Force $tmp
Write-Host "OK: 发布分支 $ver 已推送到 $url"
