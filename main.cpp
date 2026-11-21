// ============================================================================
// Git 菜单工具 C++ 版（纯 Win32）
// - 需求基线见 C:\Users\liuao\.codebuddy\work\git_tool_cpp\需求与UI梳理.md
// - 纯 Win32 + /MT 静态链接，Win7 SP1 ~ Win11 免依赖直接运行
// - 所有操作=真实 git 子进程 + 命令原样回显（学习性核心）
// ============================================================================
// build: build.bat（cl /std:c++17 /utf-8 /EHsc /MT /DUNICODE）
// ============================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <regex>
#include <cstdarg>
#include <fstream>
#include <algorithm>
#include <filesystem>
namespace fs = std::filesystem;
#ifndef EM_SETBKCOLOR
#define EM_SETBKCOLOR (WM_USER + 19)
#endif

// ============================ 控件 ID =======================================
#define IDC_DIR       101
#define IDC_PICKDIR   102
#define IDC_CLEAROUT  103
#define IDC_REFRESH   110
#define IDC_GLOBALCFG 111
#define IDC_INIT      112
#define IDC_IGNORE    114
#define IDC_OPENIGN   115
#define IDC_RESET     116
#define IDC_CMDEDIT   117
#define IDC_CMDRUN    118
#define IDC_STAT      119
#define IDC_OUT       120
#define IDC_LEFT      121
#define IDC_ABOUT     122
#define IDC_PROXYCFG  123
// 关于弹窗内部控件 id
#define IDC_ABOUT_CHECK 3001
#define IDC_ABOUT_INFO  3003
#define IDC_ABOUT_VER     3006
#define IDC_ABOUT_LINK    3007
#define IDC_ABOUT_RESULT  3008
#define IDC_ABOUT_DL      3009
#define IDM_BASE      2000

// ============================ 全局 ==========================================
static const wchar_t APP_TITLE[] = L"Git 菜单工具";
// 版本号：build.bat 构建时从 git describe 生成 app_version.h（与发布 tag 对齐）；
// 无该头文件（直接 cl 编译）时用兜底值
#if __has_include("app_version.h")
#include "app_version.h"
#else
#define APP_VERSION_STR L"v20261117a"
#endif
static const wchar_t APP_VERSION[] = APP_VERSION_STR;
static const wchar_t APP_AUTHOR[] = L"liuaolin";
static const wchar_t UPDATE_API_URL[] = L"https://api.github.com/repos/elliotaldersonX7/GitMenuTool/releases/latest";
static const wchar_t GITEA_HINT[] = L"https://github.com/用户名/仓库名.git";
static const wchar_t PRETTY[] = L"format:%h %ad%d %s";
static const wchar_t DATEFMT[] = L"format:%Y-%m-%d %H:%M";
// 内置 .gitignore 模板（Keil 固件项目通用规则）
static const wchar_t* GITIGNORE_TEMPLATE =
LR"(# ===== Keil MDK 编译产物 =====
*.o
*.d
*.crf
*.axf
*.bin
*.hex
*.elf
*.map
*.lst
*.htm
*.lnp
*.dep
*.iex
*.scvd
# 编译输出目录
**/mdk/output/
**/Objects/
**/Listings/
**/build/
**/DebugConfig/
**/RTE/_*/
# ===== Keil 用户态/临时（每人不同，uvprojx 保留）=====
*.uvguix.*
*.uvoptx
*.bak
*.bak_*
JLinkSettings.ini
JLinkLog.txt
# ===== AI 工具本地配置 =====
.claude/
# ===== 系统/编辑器 =====
.DS_Store
Thumbs.db
*.swp
# ===== Python 缓存 =====
__pycache__/
*.pyc
# ===== tools 目录：只忽略脚本与环境（exe 保留）=====
**/tools/*.py
**/tools/*.spec
**/tools/*.ps1
**/tools/*.txt
**/tools/.venv*/
# ===== Office/PDF 文档（按需增删）=====
*.doc
*.docx
*.xls
*.xlsx
*.ppt
*.pptx
*.pdf
# ===== 压缩包/比对快照 =====
*.zip
*.rar
*.7z
*.schemeB
# ===== 其它常见构建目录（按需增删）=====
dist/
.venv*/
venv/
node_modules/
)";
// 一键 ignore 的类别表：(标题, 默认勾选, 规则块)
struct GiOpt { const wchar_t* title; bool dft; const wchar_t* rules; };
static const GiOpt GI_OPTIONS[] = {
    { L"Keil MDK 编译产物与输出目录", true,
      L"# ===== Keil MDK 编译产物 =====\r\n*.o\r\n*.d\r\n*.crf\r\n*.axf\r\n*.bin\r\n*.hex\r\n*.elf\r\n*.map\r\n*.lst\r\n*.htm\r\n*.lnp\r\n*.dep\r\n*.iex\r\n*.scvd\r\n# 编译输出目录\r\n**/mdk/output/\r\n**/Objects/\r\n**/Listings/\r\n**/build/\r\n**/DebugConfig/\r\n**/RTE/_*/" },
    { L"Keil 个人配置/临时文件（uvprojx 工程文件保留）", true,
      L"# ===== Keil 用户态/临时（每人不同，uvprojx 保留）=====\r\n*.uvguix.*\r\n*.uvoptx\r\n*.bak\r\n*.bak_*\r\nJLinkSettings.ini" },
    { L"JLink 调试日志", true, L"JLinkLog.txt" },
    { L"AI 工具本地配置（.claude/ 等）", true, L"# ===== AI 工具本地配置 =====\r\n.claude/" },
    { L"系统/编辑器临时文件", true, L"# ===== 系统/编辑器 =====\r\n.DS_Store\r\nThumbs.db\r\n*.swp" },
    { L"Python 缓存/虚拟环境", true, L"# ===== Python 缓存 =====\r\n__pycache__/\r\n*.pyc\r\n.venv*/\r\nvenv/" },
    { L"tools 目录：忽略 py/spec/ps1/txt 与 .venv（exe 保留）", true,
      L"# ===== tools 目录：只忽略脚本与环境（exe 保留）=====\r\n**/tools/*.py\r\n**/tools/*.spec\r\n**/tools/*.ps1\r\n**/tools/*.txt\r\n**/tools/.venv*/" },
    { L"Office/PDF 文档（doc/docx/xls/xlsx/ppt/pptx/pdf）", true,
      L"# ===== Office/PDF 文档 =====\r\n*.doc\r\n*.docx\r\n*.xls\r\n*.xlsx\r\n*.ppt\r\n*.pptx\r\n*.pdf" },
    { L"前端/其它构建目录（dist、node_modules）", true, L"# ===== 其它常见构建目录 =====\r\ndist/\r\nnode_modules/" },
    { L"压缩包/比对快照（zip/rar/7z、*.schemeB）", true,
      L"# ===== 压缩包/比对快照 =====\r\n*.zip\r\n*.rar\r\n*.7z\r\n*.schemeB" },
};

static HINSTANCE g_hInst = nullptr;
static HWND g_main = nullptr, g_editDir = nullptr, g_stat = nullptr, g_out = nullptr;
static HWND g_cmdEdit = nullptr, g_leftPanel = nullptr;
static HFONT g_fontUI = nullptr, g_fontUIBold = nullptr, g_fontMono = nullptr;
static HFONT g_fontUIBig = nullptr;                            // 关于弹窗工具名（13pt，比 UI 大 3 号）
static HFONT g_fontUISmall = nullptr;                          // 关于弹窗其余控件（9pt，比 UI 小 1 号）
static HFONT g_fontMenu = nullptr, g_fontMenuBold = nullptr;   // 左侧菜单小两号
static HFONT g_fontSym = nullptr;                              // 符号按钮（✕/🖊，YaHei 无这些字形，须用 Segoe UI Symbol）
static WNDPROC g_dirEditProc = nullptr, g_cmdEditProc = nullptr;
static double g_scale = 1.0;

static std::wstring g_git, g_repo, g_remote, g_statUrl;
static int g_statLineCnt = 0;             // 状态卡当前行数（渲染时更新，Layout 按它自适应高度）
static std::wstring g_statLastText;       // 状态卡上次渲染的纯文本（内容没变则跳过重建，防抖）
static std::atomic<bool> g_busy{ false };
static std::atomic<bool> g_statusRunning{ false };
static std::atomic<int> g_flowBlock{ -1 };  // 流程卡卡点（0=①全局配置 1=②初始化 2=③ignore：左侧栏整体置灰禁点；-1=流程完成。④远端+首推为可选环节，不产生卡点）。状态 worker 线程写、UI 线程读，用原子量
static int g_leftScroll = 0, g_leftTotalH = 0;

// ============================ 基础工具 ======================================
static std::wstring Utf8ToWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring GbkToWide(const std::string& s) {
    int n = MultiByteToWideChar(936, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(936, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
// git 输出解码：整体 UTF-8 优先，失败逐行 UTF-8/GBK 兜底（UTF-8 提交消息与 GB2312 源码 diff 混流）
static std::wstring DecodeGitOutput(const std::string& raw) {
    std::wstring w = Utf8ToWide(raw);
    if (!w.empty() || raw.empty()) return w;
    std::wstring out; size_t pos = 0;
    for (;;) {
        size_t nl = raw.find('\n', pos);
        std::string ln = raw.substr(pos, (nl == std::string::npos ? raw.size() : nl) - pos);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        std::wstring wl = Utf8ToWide(ln);
        if (wl.empty() && !ln.empty()) wl = GbkToWide(ln);
        out += wl; out += L'\n';
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}
// Windows 命令行参数引号包装
static std::wstring QuoteArg(const std::wstring& a) {
    if (a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring q = L"\""; int bs = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++bs; continue; }
        if (c == L'"') { q.append((size_t)bs * 2 + 1, L'\\'); q += L'"'; bs = 0; }
        else { if (bs) { q.append((size_t)bs, L'\\'); bs = 0; } q += c; }
    }
    if (bs) q.append((size_t)bs * 2, L'\\');
    return q + L'"';
}
// 展示用命令行：git 还原成 "git xxx"（跳过 -c 配置与全路径），学习性回显
static std::wstring DispCmd(const std::vector<std::wstring>& argv) {
    bool isGit = !argv.empty() && (argv[0] == L"git" ||
                 (argv[0].size() > 7 && argv[0].rfind(L"git.exe") == argv[0].size() - 7));
    if (argv.size() > 3 && isGit && argv[1] == L"-c") {
        std::wstring s = L"git";
        for (size_t i = 3; i < argv.size(); ++i) s += L' ' + argv[i];
        return s;
    }
    std::wstring s;
    for (size_t i = 0; i < argv.size(); ++i) { if (i) s += L' '; s += argv[i]; }
    return s;
}
static std::wstring FindExe(const wchar_t* name) {
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, name, L".exe", MAX_PATH, buf, nullptr) > 0) return buf;
    return L"";
}
static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf; size_t i = p.find_last_of(L'\\');
    return i == std::wstring::npos ? L"." : p.substr(0, i);
}
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048]; va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
}
// 简单分词：空格分隔，引号成组（去外层引号），反斜杠保留
static std::vector<std::wstring> Tokenize(const std::wstring& s) {
    std::vector<std::wstring> out; std::wstring cur; bool inQ = false; wchar_t qc = 0;
    for (wchar_t c : s) {
        if (inQ) { if (c == qc) inQ = false; else cur += c; }
        else if (c == L'"' || c == L'\'') { inQ = true; qc = c; }
        else if (c == L' ' || c == L'\t') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
// 按行拆分（去 \r，不保留空行由调用方决定）
static std::vector<std::wstring> SplitLines(const std::wstring& s) {
    std::vector<std::wstring> out; size_t pos = 0;
    for (;;) {
        size_t nl = s.find(L'\n', pos);
        std::wstring ln = s.substr(pos, (nl == std::wstring::npos ? s.size() : nl) - pos);
        if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        out.push_back(ln);
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    return out;
}

// ============================ git 子进程执行 ================================
struct GitResult { int rc = -1; std::wstring out; };
// 是否存在以 pid 为父的子进程：git 弹出凭据输入窗口（GCM）时会长时间静默，看门狗须放行等待用户操作
static bool HasChildProcess(HANDLE hProc) {
    DWORD pid = GetProcessId(hProc);
    if (!pid) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do { if (pe.th32ParentProcessID == pid) { found = true; break; } } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
// 终止 git 及其直接子进程（如凭据窗口），避免留下孤儿输入窗
static void KillGitTree(HANDLE hProc) {
    DWORD pid = GetProcessId(hProc);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid) {
                    HANDLE c = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (c) { TerminateProcess(c, 1); CloseHandle(c); }
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    TerminateProcess(hProc, 1);
}
// 同步执行一条命令（capture 输出合并、超时强杀、隐藏窗口）
// idleMs>0 时启用"无响应看门狗"：连续 idleMs 毫秒无任何输出且进程未退出即终止（网络类命令防挂死）；
// 但 git 存在子进程（凭据输入窗口）时放行，等待用户输入
static GitResult RunGit(const std::vector<std::wstring>& argv, const std::wstring& cwd, DWORD timeoutMs, DWORD idleMs = 0) {
    GitResult r;
    std::wstring cl;
    for (size_t i = 0; i < argv.size(); ++i) { if (i) cl += L' '; cl += QuoteArg(argv[i]); }
    STARTUPINFOW si = { sizeof(si) };
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { r.out = L"创建管道失败"; return r; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = wr;
    std::vector<wchar_t> cmd(cl.begin(), cl.end()); cmd.push_back(L'\0');
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); r.out = L"启动进程失败: " + cl; return r; }
    CloseHandle(pi.hThread);
    ULONGLONG deadline = GetTickCount64() + timeoutMs;   // GetTickCount64 防 49.7 天回绕误判超时
    ULONGLONG idleDeadline = idleMs ? GetTickCount64() + idleMs : 0;
    std::string raw; char buf[8192]; DWORD n = 0;
    for (;;) {
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &n, nullptr)) break;
        if (n > 0) {
            idleDeadline = idleMs ? GetTickCount64() + idleMs : 0;   // 有输出=有响应，重置无响应计时
            if (!ReadFile(rd, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            raw.append(buf, n);
        } else {
            DWORD wait = WaitForSingleObject(pi.hProcess, 30);
            if (wait == WAIT_OBJECT_0) {
                while (PeekNamedPipe(rd, nullptr, 0, nullptr, &n, nullptr) && n > 0) {
                    if (!ReadFile(rd, buf, sizeof(buf), &n, nullptr) || n == 0) break;
                    raw.append(buf, n);
                }
                break;
            }
            if (idleDeadline && GetTickCount64() > idleDeadline) {   // 无响应看门狗：网络挂死快速失败
                if (HasChildProcess(pi.hProcess)) {                  // 有凭据输入等子窗口：等人操作，重置计时放行
                    idleDeadline = GetTickCount64() + idleMs;
                } else {
                    KillGitTree(pi.hProcess);
                    CloseHandle(rd); CloseHandle(pi.hProcess);
                    r.rc = 1;
                    r.out = Fmt(L"[无响应 %u 秒，命令已终止]\n网络不通？可在顶部【代理配置】设置代理后重试。", idleMs / 1000);
                    return r;
                }
            }
            if (GetTickCount64() > deadline) {
                KillGitTree(pi.hProcess);
                CloseHandle(rd); CloseHandle(pi.hProcess);
                r.rc = 1; r.out = Fmt(L"[超时 %u 秒，命令已终止]", timeoutMs / 1000);
                return r;
            }
        }
    }
    CloseHandle(rd);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    r.rc = (int)code;
    r.out = DecodeGitOutput(raw);
    return r;
}
// 快速取 git 输出（状态探测/前置检查；失败返回空串）
static std::wstring GitOut(const std::vector<std::wstring>& args, DWORD timeoutMs = 10000) {
    std::vector<std::wstring> argv = { g_git };
    if (!g_repo.empty()) { argv.push_back(L"-C"); argv.push_back(g_repo); }   // 全局命令（如 config --global）不依赖仓库，未选目录也能执行
    for (auto& a : args) argv.push_back(a);
    GitResult r = RunGit(argv, L"", timeoutMs);
    return r.rc == 0 ? r.out : L"";
}
// ============================ 检查更新（GitHub Release）======================
// WinINet 同步 GET（只在后台线程调用）；proxy=true 走系统代理，false 直连；失败返回空串
// onPct 非空且响应带 Content-Length 时按已收字节回调百分比（仅大文件下载用）
static std::string HttpGetVia(bool useProxy, const std::wstring& url, void (*onPct)(int) = nullptr) {
    std::string out;
    HINTERNET hOpen = InternetOpenW(L"GitMenuTool",
        useProxy ? INTERNET_OPEN_TYPE_PRECONFIG : INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hOpen) return out;
    DWORD t = 15000;   // 连接/发送/接收统一 15 秒超时，GitHub 不通时尽快放弃
    InternetSetOptionW(hOpen, INTERNET_OPTION_CONNECT_TIMEOUT, &t, sizeof(t));
    InternetSetOptionW(hOpen, INTERNET_OPTION_SEND_TIMEOUT, &t, sizeof(t));
    InternetSetOptionW(hOpen, INTERNET_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
    HINTERNET hFile = InternetOpenUrlW(hOpen, url.c_str(), L"User-Agent: GitMenuTool\r\n", 0,
                                       INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (hFile) {
        DWORD len = 0, lenSize = sizeof(len);
        BOOL haveLen = HttpQueryInfoW(hFile, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &len, &lenSize, nullptr);
        char buf[4096]; DWORD rd = 0, acc = 0; int lastPct = -1;
        while (InternetReadFile(hFile, buf, sizeof(buf), &rd) && rd) {
            out.append(buf, rd);
            if (onPct && haveLen && len) {                 // 按已收字节回调百分比（变化才发）
                acc += rd;
                int pct = (int)(acc * 100ULL / len);
                if (pct != lastPct) { lastPct = pct; onPct(pct); }
            }
        }
        InternetCloseHandle(hFile);
    }
    InternetCloseHandle(hOpen);
    return out;
}
// 检查更新取 JSON：系统代理优先（未配置代理时 WinINet PRECONFIG 自动等效直连），内容无
// "tag_name" 字段（403/404 错误页）再走纯直连兜底；都无效返回空
// rateLimited 出参：两路响应都是 GitHub 限流页时置 true（匿名 60 次/小时/IP），供文案区分
static std::string HttpGetUtf8(const std::wstring& url, bool* rateLimited = nullptr) {
    std::string s1 = HttpGetVia(true, url);
    if (s1.find("\"tag_name\"") != std::string::npos) return s1;
    std::string s2 = HttpGetVia(false, url);
    if (s2.find("\"tag_name\"") != std::string::npos) return s2;
    if (rateLimited)
        *rateLimited = s1.find("rate limit") != std::string::npos
                    || s2.find("rate limit") != std::string::npos;
    return "";
}
// 下载 exe 二进制：系统代理优先（>100KB 视为有效，错误页只有几百字节），无效再走纯直连兜底；onPct 透传进度回调
static std::string HttpDownload(const std::wstring& url, void (*onPct)(int) = nullptr) {
    std::string s = HttpGetVia(true, url, onPct);
    if (s.size() > 100 * 1024) return s;
    s = HttpGetVia(false, url, onPct);
    if (s.size() > 100 * 1024) return s;
    return "";
}
// UTF-8 字符串 → 宽字符
static std::wstring Utf8ToWideRaw(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
// 极简 JSON 字符串字段提取（仅取 "key":"value" 形态；null/缺失返回空）；转义序列原样保留
static std::wstring JsonGetStringRaw(const std::string& json, const char* key) {
    std::string pat = std::string("\"") + key + "\":";
    size_t p = json.find(pat);
    if (p == std::string::npos) return L"";
    p += pat.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size() || json[p] != '"') return L"";   // null 或非字符串
    ++p;
    std::string val;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) { val += json[p]; val += json[p + 1]; p += 2; }
        else { val += json[p]; ++p; }
    }
    return Utf8ToWideRaw(val);
}
// JSON 转义还原（\n \t \" \\ \/ \b \f \uXXXX；连续代理对拼接即 UTF-16 本身）
static std::wstring JsonUnescape(std::wstring s) {
    std::wstring out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'\\' || i + 1 >= s.size()) { out += s[i]; continue; }
        wchar_t c = s[++i];
        switch (c) {
        case L'n': out += L'\n'; break;
        case L't': out += L'\t'; break;
        case L'r': out += L'\r'; break;
        case L'b': out += L'\b'; break;
        case L'f': out += L'\f'; break;
        case L'"': case L'\\': case L'/': out += c; break;
        case L'u':
            if (i + 4 < s.size()) {
                out += (wchar_t)wcstoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
            }
            break;
        default: out += c; break;
        }
    }
    return out;
}
// 取 assets 里第一个 .exe 附件的下载地址（附件名带不带版本都可以，最终都会改名顶位）
static std::wstring JsonGetAssetExe(const std::string& json) {
    const std::string key = "\"browser_download_url\":\"";
    size_t p = 0;
    while ((p = json.find(key, p)) != std::string::npos) {
        p += key.size();
        size_t e = json.find('"', p);
        if (e == std::string::npos) break;
        std::string url = json.substr(p, e - p);
        if (url.size() > 4 && (url.compare(url.size() - 4, 4, ".exe") == 0 || url.compare(url.size() - 4, 4, ".EXE") == 0))
            return Utf8ToWideRaw(url);
        p = e + 1;
    }
    return L"";
}
// 版本号比较：去 v/V 前缀后按 '.' 分段取数字逐段比较，缺段按 0；返回 >0=a新 <0=b新 0=相同
// 字母后缀忽略（v1.1a 视作 v1.1；v20261117a 整段数字 20261117）
static int VersionCompare(const std::wstring& a, const std::wstring& b) {
    auto segs = [](const std::wstring& s) {
        std::wstring t = s;
        if (!t.empty() && (t[0] == L'v' || t[0] == L'V')) t.erase(0, 1);
        std::vector<unsigned long> v;
        size_t i = 0;
        while (i < t.size()) {
            if (iswdigit(t[i])) {
                unsigned long n = 0;
                while (i < t.size() && iswdigit(t[i])) { n = n * 10 + (unsigned long)(t[i] - L'0'); ++i; }
                v.push_back(n);
            } else ++i;
        }
        return v;
    };
    auto va = segs(a), vb = segs(b);
    size_t n = va.size() > vb.size() ? va.size() : vb.size();
    for (size_t k = 0; k < n; ++k) {
        unsigned long x = k < va.size() ? va[k] : 0;
        unsigned long y = k < vb.size() ? vb[k] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}
// 组装带编码前缀的 git 命令
static std::vector<std::wstring> G(std::initializer_list<std::wstring> args) {
    std::vector<std::wstring> argv = { g_git, L"-c", L"i18n.logoutputencoding=utf-8" };
    for (auto& a : args) argv.push_back(a);
    return argv;
}
// 按仓库配置文件的出现顺序取远端列表（git remote 输出按字母排序；新增顺序更符合直觉）。
// 直接解析 .git/config 的 [remote "名"] 段与 url 键；文件读不到（worktree 等）回退 git remote
static std::vector<std::pair<std::wstring, std::wstring>> RemotesInOrder() {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    std::ifstream f(fs::path(g_repo) / L".git" / L"config");
    if (f) {
        std::string ln;
        std::wstring cur;                                     // 当前所在 [remote "名"] 段名
        while (std::getline(f, ln)) {
            std::wstring line(ln.begin(), ln.end());          // config 为 ASCII/UTF-8，按字节转宽
            line = Trim(line);
            if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
            if (line.front() == L'[') {                       // 段头
                size_t q1 = line.find(L'"');
                size_t q2 = (q1 == std::wstring::npos) ? std::wstring::npos : line.find(L'"', q1 + 1);
                cur = (q1 != std::wstring::npos && q2 != std::wstring::npos && line.rfind(L"[remote", 0) == 0)
                      ? line.substr(q1 + 1, q2 - q1 - 1) : L"";
                continue;
            }
            if (!cur.empty() && line.rfind(L"url", 0) == 0) { // url 键：每段只取第一条（多 push url 忽略）
                size_t eq = line.find(L'=');
                if (eq != std::wstring::npos) out.push_back({ cur, Trim(line.substr(eq + 1)) });
                cur.clear();
            }
        }
        if (!out.empty()) return out;
    }
    for (auto& l : SplitLines(GitOut({ L"remote" }))) {       // 回退：git remote（字母序）
        std::wstring nm = Trim(l);
        if (nm.empty()) continue;
        out.push_back({ nm, Trim(GitOut({ L"config", L"--get", L"remote." + nm + L".url" })) });
    }
    return out;
}
// 默认远端：只认用户在【配置远程】里显式勾选的（仓库级配置 gittool.defaultremote）；无显式选择返回空。
// 不回退上游分支/第一个远端——回退会让"全不勾=纯本机"形同虚设，未勾选时远程操作仍能偷偷成功
static std::wstring DetectRemote() {
    if (g_repo.empty()) return L"";
    std::wstring picked = Trim(GitOut({ L"config", L"--get", L"gittool.defaultremote" }));
    if (picked.empty()) return L"";
    for (auto& r : RemotesInOrder())
        if (r.first == picked) return r.first;   // 显式选的远端已不存在（被删/改名未跟进）视为未选择
    return L"";
}

// ============================ RichEdit 富文本 ===============================
static const COLORREF CLR_CMD  = RGB(0x8f, 0xd1, 0x8f);
static const COLORREF CLR_ERR  = RGB(0xff, 0x80, 0x80);
static const COLORREF CLR_SEP  = RGB(0x66, 0x66, 0x66);
static const COLORREF CLR_TXT  = RGB(0xdd, 0xdd, 0xdd);
static const COLORREF CLR_HINT = RGB(0x9a, 0xa0, 0xa6);
static const COLORREF STAT_TXT = RGB(0x22, 0x22, 0x22);
static const COLORREF STAT_DIM = RGB(0x9a, 0xa0, 0xa6);
static const COLORREF STAT_RED = RGB(0xd3, 0x2f, 0x2f);
static const COLORREF STAT_GRN = RGB(0x13, 0x73, 0x33);
static const COLORREF STAT_BLU = RGB(0x1a, 0x73, 0xe8);
static const COLORREF STAT_ORG = RGB(0xb2, 0x6a, 0x00);
// 胶囊底色
static const COLORREF BG_GRN = RGB(0xe6, 0xf4, 0xea);
static const COLORREF BG_RED = RGB(0xfc, 0xe8, 0xe6);
static const COLORREF BG_GRY = RGB(0xec, 0xec, 0xec);
static const COLORREF BG_BLU = RGB(0xe8, 0xf0, 0xfe);
static const COLORREF BG_YLW = RGB(0xfe, 0xf7, 0xe0);

struct Seg { std::wstring text; COLORREF color; bool bold; COLORREF back; bool link; const wchar_t* face; int pt; };
static Seg SegT(const std::wstring& t, COLORREF c, bool b = false, COLORREF back = 0, bool link = false) {
    Seg s; s.text = t; s.color = c; s.bold = b; s.back = back; s.link = link; s.face = L"Segoe UI"; s.pt = 9; return s;
}
static Seg SegM(const std::wstring& t, COLORREF c, bool b = false) {   // 输出区等宽字体
    Seg s; s.text = t; s.color = c; s.bold = b; s.back = 0; s.link = false; s.face = L"Consolas"; s.pt = 10; return s;
}
// RichEdit 底色：EM_SETBKCOLOR 新系统不可靠——三层组合拳：
// ① GWLP_USERDATA 存底色；② WM_ERASEBKGND 自绘擦背景；③ WM_PAINT 里 RichEdit 画完文字后，
//    用最后一个字符的行底位置把下方空白补成底色（riched20 空白区强制白底，只能这样盖）
// 状态卡远端 URL 的手型光标与点击：不用 CFE_LINK（下划线盖字符、吸附范围大），
// 改为字符级命中测试——光标/点击只在 URL（含 ↗）字符附近生效
static POINT g_statDown = { -1, -1 };
static bool StatUrlAt(HWND h, int x, int y) {
    if (h != g_stat || g_statUrl.empty()) return false;
    int tlen = GetWindowTextLengthW(h);
    if (tlen < 1) return false;
    std::wstring text(tlen, L'\0');
    GetWindowTextW(h, &text[0], tlen + 1);
    size_t p = text.find(g_statUrl);
    if (p == std::wstring::npos) return false;
    size_t uLen = g_statUrl.size();
    // 关键：GetWindowText 的索引按 \r\n（每换行 2 字符）计，richedit 内部按 \r（1 字符）计，
    // 外部索引必须减去之前的换行数，否则 EM_POSFROMCHAR 会落到下一行
    int breaks = 0;
    for (size_t i = 0; i < p && i < text.size(); ++i) if (text[i] == L'\n') ++breaks;
    int iStart = (int)p - breaks;
    int iEnd = iStart + (int)uLen - 1;
    // 几何判定：锚定 URL 首末字符的位置
    POINTL p0 = { 0, 0 }, p1 = { 0, 0 };
    SendMessageW(h, EM_POSFROMCHAR, (WPARAM)&p0, (LPARAM)iStart);
    SendMessageW(h, EM_POSFROMCHAR, (WPARAM)&p1, (LPARAM)iEnd);
    HDC dc = GetDC(h);
    HFONT of = (HFONT)SelectObject(dc, g_fontUI);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(h, dc);
    int lh = tm.tmHeight + tm.tmExternalLeading;
    if (y < p0.y || y > p0.y + lh) return false;              // 严格限定 URL 所在行
    int cw = uLen > 1 ? (p1.x - p0.x) / (int)(uLen - 1) : lh / 2;   // 字符均宽
    if (x < p0.x || x > p1.x + cw) return false;              // 严格限定 URL 字符区（含末字符一格与 ↗）
    return true;
}
static LRESULT CALLBACK RichBaseSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_ERASEBKGND) {
        RECT rc; GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush((COLORREF)GetWindowLongPtrW(h, GWLP_USERDATA));
        FillRect((HDC)w, &rc, b);
        DeleteObject(b);
        return 1;
    }
    if (m == WM_SETCURSOR && h == g_stat) {
        POINT pt; GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        if (StatUrlAt(h, pt.x, pt.y)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
    }
    if (m == WM_MOUSEMOVE && h == g_stat) {
        // RichEdit 内部会在 mousemove 里把光标改回 I 形，覆盖 WM_SETCURSOR 的手型——
        // 命中 URL 时设手型且不把 mousemove 转发给控件
        POINT pt; GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        bool over = StatUrlAt(h, pt.x, pt.y);
        SetCursor(LoadCursor(nullptr, over ? IDC_HAND : IDC_IBEAM));
        return 0;
    }
    if (m == WM_LBUTTONDOWN && h == g_stat) {
        g_statDown.x = (short)LOWORD(l);
        g_statDown.y = (short)HIWORD(l);
    }
    if (m == WM_LBUTTONUP && h == g_stat) {
        POINT pt = { (short)LOWORD(l), (short)HIWORD(l) };
        int dx = pt.x - g_statDown.x, dy = pt.y - g_statDown.y;
        g_statDown.x = -1; g_statDown.y = -1;
        // 按下/松开间距小于 5px 视为点击（拖动选文字不触发打开）
        if (dx * dx + dy * dy < 25 && StatUrlAt(h, pt.x, pt.y) && !g_statUrl.empty())
            ShellExecuteW(nullptr, L"open", g_statUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (m == WM_PAINT) {
        DefSubclassProc(h, m, w, l);   // RichEdit 先自绘（msftedit 引擎会整块刷白，下面逐行盖回黑底）
        COLORREF base = (COLORREF)GetWindowLongPtrW(h, GWLP_USERDATA);
        HDC dc = GetDC(h);
        RECT rc; GetClientRect(h, &rc);
        int lh = (int)(10 * g_scale * 2), cw = (int)(5 * g_scale * 2);   // 行高/字符宽兜底值
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        if (f) {
            HDC tdc = GetDC(h);
            HFONT of = (HFONT)SelectObject(tdc, f);
            TEXTMETRICW tm;
            if (GetTextMetricsW(tdc, &tm)) { lh = tm.tmHeight + tm.tmExternalLeading; cw = tm.tmAveCharWidth; }
            SelectObject(tdc, of);
            ReleaseDC(h, tdc);
        }
        HBRUSH b = CreateSolidBrush(base);
        int total = GetWindowTextLengthW(h);
        // 逐行补齐行尾"段落标记格"：行尾格的 x 由后一个字符位置判定
        // （EM_LINELENGTH 是否含行尾 \r 不确定，用 pa/pb 是否同行来消歧）
        int lineCount = (int)SendMessageW(h, EM_GETLINECOUNT, 0, 0);
        for (int i = 0; i < lineCount; ++i) {
            int start = (int)SendMessageW(h, EM_LINEINDEX, i, 0);
            if (start < 0) break;
            int len = (int)SendMessageW(h, EM_LINELENGTH, start, 0);
            if (len < 1) continue;
            POINTL pa = { 0, 0 };
            SendMessageW(h, EM_POSFROMCHAR, (WPARAM)&pa, (LPARAM)(start + len - 1));
            if (pa.y > rc.bottom) break;         // 已滚出可视区下方，后续行更靠下
            if (pa.y + lh < 0) continue;         // 滚出上方
            int x0;
            if (start + len < total) {
                POINTL pb = { 0, 0 };
                SendMessageW(h, EM_POSFROMCHAR, (WPARAM)&pb, (LPARAM)(start + len));
                x0 = (pb.y == pa.y) ? pb.x : pa.x;   // 同行→pb 是行尾格；否则 pa 本身是行尾格
            } else {
                x0 = pa.x + cw;
            }
            if (x0 < 0) x0 = 0;
            if (x0 < rc.right) {
                RECT fr = { x0, pa.y, rc.right, pa.y + lh };
                FillRect(dc, &fr, b);
            }
        }
        // 最后一个字符行底以下的整块空白
        int len = GetWindowTextLengthW(h);
        int yTop = 0;
        if (len > 0) {
            POINTL pl = { 0, 0 };
            SendMessageW(h, EM_POSFROMCHAR, (WPARAM)&pl, (LPARAM)(len - 1));
            yTop = pl.y + lh;
        }
        if (yTop < rc.bottom) {
            RECT fr = { rc.left, yTop, rc.right, rc.bottom };
            FillRect(dc, &fr, b);
        }
        DeleteObject(b);
        ReleaseDC(h, dc);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
static HWND NewRich(HWND parent, HMENU id, COLORREF bk) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, RICHEDIT_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_READONLY | ES_MULTILINE,
        0, 0, 0, 0, parent, id, g_hInst, nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)bk);   // 控件底色
    SetWindowSubclass(h, RichBaseSub, 1, 0);
    return h;
}
static void RichAppend(HWND h, const std::vector<Seg>& segs) {
    CHARRANGE cr; SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
    cr.cpMin = cr.cpMax = -1;
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&cr);
    for (auto& s : segs) {
        CHARFORMAT2W cf = { sizeof(cf) };
        cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_FACE | CFM_SIZE | CFM_BACKCOLOR;
        cf.crBackColor = s.back ? s.back : (COLORREF)GetWindowLongPtrW(h, GWLP_USERDATA);   // 无胶囊底色时用控件底色
        if (s.link) { cf.dwMask |= CFM_LINK; }
        cf.dwEffects = 0;
        if (s.bold) cf.dwEffects |= CFE_BOLD;
        if (s.link) cf.dwEffects |= CFE_LINK;
        cf.crTextColor = s.color;
        lstrcpynW(cf.szFaceName, s.face, LF_FACESIZE);
        cf.yHeight = s.pt * 20;   // twips：1pt=20twips，RichEdit 随系统 DPI 自行缩放，不能再乘 g_scale
        SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)s.text.c_str());
    }
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
}
static void RichAppendLine(HWND h, const std::vector<Seg>& segs) {
    // 关键：行尾"段落标记格"的底色由 \r 的字符格式决定，且只对 \r\n 选区设格式会被忽略，
    // 必须追加完成后显式选中行尾 \r 再设一次底色，否则每行末尾有白色竖条。
    std::vector<Seg> v = segs;
    if (!v.empty()) {
        v.back().text += L"\r\n";
    } else {
        v.push_back(SegM(L" \r\n", CLR_TXT));
    }
    RichAppend(h, v);
    CHARRANGE cr;
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMax >= 1) {
        CHARRANGE crMark = { cr.cpMax - 1, cr.cpMax };   // 行尾 \r
        SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&crMark);
        CHARFORMAT2W cf = { sizeof(cf) };
        // CFM_EFFECTS + dwEffects=0：显式清掉段落标记继承的链接/加粗等效果，
        // 否则行尾整片区域的点击都会被当成链接（吸附范围异常扩大）
        cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_FACE | CFM_SIZE | CFM_EFFECTS;
        cf.dwEffects = 0;
        cf.crBackColor = (COLORREF)GetWindowLongPtrW(h, GWLP_USERDATA);
        cf.crTextColor = CLR_TXT;
        lstrcpynW(cf.szFaceName, L"Consolas", LF_FACESIZE);
        cf.yHeight = 10 * 20;
        SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        CHARRANGE crEnd = { cr.cpMax, cr.cpMax };
        SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&crEnd);
    }
}
// ============================ 输出区（单窗口自绘黑底日志） ===================
// richedit 的空白区底色在新系统不可控（引擎强制刷白，EM_SETBKCOLOR/字符底色/行尾补色
// 都只能管到局部），输出区改为与左栏同款的自绘方案：日志行存自有结构，
// WM_PAINT 双缓冲画黑底+彩色文字，滚动与复制自管
struct LogSeg { std::wstring text; COLORREF color; };
struct LogLine { std::vector<LogSeg> segs; };
static std::vector<LogLine> g_log;
static int g_logScroll = 0, g_logTotalH = 0;
static int g_logMaxW = 0;       // 日志最宽行像素宽（含左右留白），横向滚动范围
static int g_logScrollX = 0;    // 横向滚动偏移
static std::wstring LineText(int i);                    // 前向声明：LogAppend 维护最宽行宽用
static int LineWidthUpTo(const std::wstring& t, int n);
static int LogLineH() {
    HDC dc = GetDC(nullptr);
    HFONT of = (HFONT)SelectObject(dc, g_fontMono);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(nullptr, dc);
    return tm.tmHeight + tm.tmExternalLeading;
}
static void UpdateOutScroll(HWND h) {
    g_logTotalH = (int)g_log.size() * LogLineH() + (int)(8 * g_scale);
    RECT rc; GetClientRect(h, &rc);
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE, 0, g_logTotalH, (UINT)rc.bottom, 0 };
    SetScrollInfo(h, SB_VERT, &si, TRUE);
    SCROLLINFO sh = { sizeof(sh), SIF_RANGE | SIF_PAGE, 0, g_logMaxW, (UINT)rc.right, 0 };
    SetScrollInfo(h, SB_HORZ, &sh, TRUE);   // 横向：最宽行超出客户区时可拖动
}
static void OutScrollTo(HWND h, int pos) {
    RECT rc; GetClientRect(h, &rc);
    int maxScroll = g_logTotalH - (rc.bottom - rc.top);
    if (maxScroll < 0) maxScroll = 0;
    if (pos < 0) pos = 0;
    if (pos > maxScroll) pos = maxScroll;
    if (pos != g_logScroll) {
        g_logScroll = pos;
        SetScrollPos(h, SB_VERT, pos, TRUE);
        InvalidateRect(h, nullptr, FALSE);
    }
}
// 横向滚动到指定像素（范围 0 ~ 最宽行-客户区宽）
static void OutScrollXTo(HWND h, int pos) {
    RECT rc; GetClientRect(h, &rc);
    int maxScroll = g_logMaxW - (rc.right - rc.left);
    if (maxScroll < 0) maxScroll = 0;
    if (pos < 0) pos = 0;
    if (pos > maxScroll) pos = maxScroll;
    if (pos != g_logScrollX) {
        g_logScrollX = pos;
        SetScrollPos(h, SB_HORZ, pos, TRUE);
        InvalidateRect(h, nullptr, FALSE);
    }
}
static void LogAppend(const std::vector<Seg>& segs) {
    LogLine ln;
    for (auto& s : segs) ln.segs.push_back({ s.text, s.color });
    // 贴底跟随：追加前视图已在底部附近时，新内容后自动滚到底；用户往上翻了则保持位置
    bool follow = false;
    if (g_out) {
        RECT rc; GetClientRect(g_out, &rc);
        follow = (g_logScroll + (rc.bottom - rc.top)) >= g_logTotalH - LogLineH();
    }
    g_log.push_back(std::move(ln));
    {
        std::wstring lt = LineText((int)g_log.size() - 1);   // 维护最宽行宽（横向滚动范围）
        int lw = 2 * (int)(6 * g_scale) + LineWidthUpTo(lt, (int)lt.size());
        if (lw > g_logMaxW) g_logMaxW = lw;
    }
    if (g_out) {
        UpdateOutScroll(g_out);
        RECT rc; GetClientRect(g_out, &rc);
        int maxScroll = g_logTotalH - (rc.bottom - rc.top);
        if (maxScroll < 0) maxScroll = 0;
        if (follow) g_logScroll = maxScroll;
        SetScrollPos(g_out, SB_VERT, g_logScroll, TRUE);
        InvalidateRect(g_out, nullptr, FALSE);
    }
}
static void CopyTextToClipboard(const std::wstring& all) {
    if (all.empty()) return;
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    int bytes = (int)((all.size() + 1) * sizeof(wchar_t));
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* p = GlobalLock(hMem);
        if (p) { memcpy(p, all.c_str(), bytes); GlobalUnlock(hMem); }
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}
static std::wstring LineText(int i) {
    std::wstring s;
    for (auto& sg : g_log[i].segs) s += sg.text;
    return s;
}
// 等宽字体单字符宽度（Consolas 全字等宽，拖选命中与高亮都用它换算）
static int CharW() {
    static int w = -1;
    if (w < 0) {
        HDC dc = GetDC(nullptr);
        HFONT of = (HFONT)SelectObject(dc, g_fontMono);
        SIZE sz; GetTextExtentPoint32W(dc, L"0000000000", 10, &sz);
        SelectObject(dc, of);
        ReleaseDC(nullptr, dc);
        w = sz.cx / 10; if (w < 1) w = 1;
    }
    return w;
}
// 是否全角宽字符（CJK/韩文/兼容表意/全角符号，字体回退渲染为 ASCII 两倍宽）
static bool IsWideChar(wchar_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF)
        || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
}
// 行文本前 n 个字符的像素宽度（ASCII 单宽，宽字符双宽）
static int LineWidthUpTo(const std::wstring& t, int n) {
    int cw = CharW(), w = 0, n2 = n < (int)t.size() ? n : (int)t.size();
    for (int i = 0; i < n2; ++i) w += IsWideChar(t[i]) ? cw * 2 : cw;
    return w;
}
// 输出区拖选状态（锚点/当前点，均为 行,字符），-1 = 无选区
static int g_anchorL = -1, g_anchorC = 0, g_selCurL = -1, g_selCurC = 0;
// 选区归一化：返回是否有非空选区，l0c0=起点 l1c1=终点
static bool SelRange(int& l0, int& c0, int& l1, int& c1) {
    if (g_anchorL < 0 || g_selCurL < 0) return false;
    if (g_anchorL < g_selCurL || (g_anchorL == g_selCurL && g_anchorC <= g_selCurC)) {
        l0 = g_anchorL; c0 = g_anchorC; l1 = g_selCurL; c1 = g_selCurC;
    } else {
        l0 = g_selCurL; c0 = g_selCurC; l1 = g_anchorL; c1 = g_anchorC;
    }
    return (l0 != l1 || c0 != c1);
}
static void CopySelectionOrAll() {
    int l0, c0, l1, c1;
    std::wstring all;
    if (SelRange(l0, c0, l1, c1)) {
        for (int i = l0; i <= l1; ++i) {
            std::wstring line = LineText(i);
            int a = (i == l0) ? c0 : 0;
            int b = (i == l1) ? c1 : (int)line.size();
            if (a < 0) a = 0;
            if (b > (int)line.size()) b = (int)line.size();
            if (b > a) all += line.substr(a, b - a);
            if (i < l1) all += L"\r\n";
        }
        CopyTextToClipboard(all);
        LogAppend({ SegM(L"[已复制选中文本]", CLR_HINT) });
    } else {
        for (auto& ln : g_log)
            for (auto& s : ln.segs) all += s.text;
        CopyTextToClipboard(all);
        LogAppend({ SegM(L"[未选中文字，已复制全部日志]", CLR_HINT) });
    }
}
// 命中测试：客户区坐标 → (行,字符)；x→字符换算感知宽字符双宽
static void OutHitTest(POINT pt, int& line, int& ch) {
    int lh = LogLineH(), pad = (int)(6 * g_scale);
    line = (g_logScroll + pt.y) / lh;
    if (line < 0) line = 0;
    if (line >= (int)g_log.size()) line = (int)g_log.size() - 1;
    std::wstring lt = LineText(line);
    int x = pt.x - pad + g_logScrollX; if (x < 0) x = 0;   // 命中换算含横向偏移
    int cw = CharW(), acc = 0, i = 0;
    for (; i < (int)lt.size(); ++i) {
        int wch = IsWideChar(lt[i]) ? cw * 2 : cw;
        if (x < acc + wch) break;
        acc += wch;
    }
    ch = i;
}
static LRESULT CALLBACK OutProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP oldb = (HBITMAP)SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(RGB(0x11, 0x11, 0x11));
        FillRect(mem, &rc, bg);
        DeleteObject(bg);
        SetBkMode(mem, TRANSPARENT);
        SelectObject(mem, g_fontMono);
        int lh = LogLineH();
        int pad = (int)(6 * g_scale);
        int first = g_logScroll / lh;
        int y = first * lh - g_logScroll;
        int sl0, sc0, sl1, sc1;
        bool hasSel = SelRange(sl0, sc0, sl1, sc1);
        for (int i = first; i < (int)g_log.size() && y < rc.bottom; ++i, y += lh) {
            int x = pad - g_logScrollX;   // 横向滚动偏移
            for (auto& s : g_log[i].segs) {
                if (s.text.empty()) continue;
                SetTextColor(mem, s.color);
                SIZE sz;
                GetTextExtentPoint32W(mem, s.text.c_str(), (int)s.text.size(), &sz);
                TextOutW(mem, x, y, s.text.c_str(), (int)s.text.size());
                x += sz.cx;
            }
            // 选中高亮层：按前缀像素宽铺底色，再按分段原色在正确位置重绘选中字符（中文双宽感知）
            if (hasSel && i >= sl0 && i <= sl1) {
                std::wstring lt = LineText(i);
                int a = (i == sl0) ? sc0 : 0;
                int b = (i == sl1) ? sc1 : (int)lt.size();
                if (b > a) {
                    RECT hl = { pad + LineWidthUpTo(lt, a) - g_logScrollX, y,
                                pad + LineWidthUpTo(lt, b) - g_logScrollX, y + lh };
                    HBRUSH hb = CreateSolidBrush(RGB(38, 79, 120));
                    FillRect(mem, &hl, hb);
                    DeleteObject(hb);
                    int idx = 0;
                    for (auto& s : g_log[i].segs) {
                        int slen = (int)s.text.size();
                        int st = idx > a ? idx : a;
                        int en = idx + slen < b ? idx + slen : b;
                        if (en > st) {
                            SetTextColor(mem, s.color);
                            TextOutW(mem, pad + LineWidthUpTo(lt, st) - g_logScrollX, y, s.text.c_str() + (st - idx), en - st);
                        }
                        idx += slen;
                    }
                }
            }
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);   // 双缓冲回贴到窗口
        SelectObject(mem, oldb);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_VSCROLL: {
        int lh = LogLineH();
        int pos = g_logScroll;
        switch (LOWORD(w)) {
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = g_logTotalH; break;
            case SB_LINEUP: pos -= lh; break;
            case SB_LINEDOWN: pos += lh; break;
            case SB_PAGEUP: pos -= (int)(120 * g_scale); break;
            case SB_PAGEDOWN: pos += (int)(120 * g_scale); break;
            case SB_THUMBTRACK: {
                SCROLLINFO ti = { sizeof(ti), SIF_TRACKPOS, 0, 0, 0, 0 };
                pos = GetScrollInfo(h, SB_VERT, &ti) ? ti.nTrackPos : HIWORD(w);
                break;
            }
        }
        OutScrollTo(h, pos);
        return 0;
    }
    case WM_HSCROLL: {   // 横向拖动条：长行左右拖动查看
        int pos = g_logScrollX;
        switch (LOWORD(w)) {
            case SB_LEFT: pos = 0; break;
            case SB_RIGHT: pos = g_logMaxW; break;
            case SB_LINELEFT: pos -= CharW() * 3; break;
            case SB_LINERIGHT: pos += CharW() * 3; break;
            case SB_PAGELEFT: pos -= (int)(120 * g_scale); break;
            case SB_PAGERIGHT: pos += (int)(120 * g_scale); break;
            case SB_THUMBTRACK: {
                SCROLLINFO ti = { sizeof(ti), SIF_TRACKPOS, 0, 0, 0, 0 };
                pos = GetScrollInfo(h, SB_HORZ, &ti) ? ti.nTrackPos : HIWORD(w);
                break;
            }
        }
        OutScrollXTo(h, pos);
        return 0;
    }
    case WM_SIZE: {
        UpdateOutScroll(h);              // 客户区变化时同步两条滚动条范围
        OutScrollXTo(h, g_logScrollX);   // 变宽/变窄后回夹横向偏移，保持变量与滑块一致
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int pos = g_logScroll - (int)GET_WHEEL_DELTA_WPARAM(w) * LogLineH() * 3 / WHEEL_DELTA;
        OutScrollTo(h, pos);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(h);
        if (!g_log.empty()) {
            SetCapture(h);
            OutHitTest({ (short)LOWORD(l), (short)HIWORD(l) }, g_anchorL, g_anchorC);
            g_selCurL = g_anchorL; g_selCurC = g_anchorC;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if ((w & MK_LBUTTON) && !g_log.empty() && g_anchorL >= 0) {
            int line, ch;
            OutHitTest({ (short)LOWORD(l), (short)HIWORD(l) }, line, ch);
            if (line != g_selCurL || ch != g_selCurC) {
                g_selCurL = line; g_selCurC = ch;
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK: {
        // 双击选中整行
        if (!g_log.empty()) {
            SetFocus(h);
            int line, ch;
            OutHitTest({ (short)LOWORD(l), (short)HIWORD(l) }, line, ch);
            g_anchorL = line; g_anchorC = 0;
            g_selCurL = line; g_selCurC = (int)LineText(line).size();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (w == 'C') CopySelectionOrAll();
            else if (w == 'A' && !g_log.empty()) {
                g_anchorL = 0; g_anchorC = 0;
                g_selCurL = (int)g_log.size() - 1;
                g_selCurC = (int)LineText(g_selCurL).size();
                InvalidateRect(h, nullptr, FALSE);
            }
            else if (w == 'V') {   // 输出区只读：粘贴转交命令输入行
                SetFocus(g_cmdEdit);
                PostMessageW(g_cmdEdit, WM_PASTE, 0, 0);
            }
        }
        return 0;
    case WM_SETFOCUS:
        return 0;   // 只读日志，不显示输入光标
    }
    return DefWindowProcW(h, m, w, l);
}
static void OutSep()  { LogAppend({ SegM(std::wstring(72, L'='), CLR_SEP) }); }
static void OutCmd(const std::wstring& s)  { LogAppend({ SegM(L"> " + s, CLR_CMD) }); }
static void OutTxt(const std::wstring& s)  { LogAppend({ SegM(s, CLR_TXT) }); }
static void OutErr(const std::wstring& s)  { LogAppend({ SegM(s, CLR_ERR) }); }

// ============================ 异步执行引擎 ==================================
struct JobCmd { std::vector<std::wstring> argv; bool optional; };   // optional=失败不中止
struct JobMsg { int kind; std::wstring text; };                     // kind: 0=cmd 1=out 2=err
static const UINT WM_APP_JOBMSG  = WM_APP + 1;                      // wParam=JobMsg*(堆)
static const UINT WM_APP_JOBDONE = WM_APP + 2;
static const UINT WM_APP_STATUS  = WM_APP + 3;                      // wParam=segments(堆)
static const UINT WM_APP_UPDATE  = WM_APP + 4;                      // wParam=UpdateResult*(堆) lParam=1自动/0手动
static const UINT WM_APP_UPGRADE = WM_APP + 5;                      // wParam=UpgradeResult*(堆)
static const UINT WM_APP_DLPCT   = WM_APP + 6;                      // wParam=下载百分比(0~100)
// 单任务串行：逐条执行，非 optional 失败即中止；SPECIAL 命令见 HandleSpecial
static void HandleSpecial(const std::vector<std::wstring>& argv);
static void ExecAsync(std::vector<JobCmd> cmds) {
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) {
        MessageBoxW(g_main, L"上一个操作还在运行，请稍候。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring repo = g_repo;
    std::thread([cmds, repo]() {
        for (auto& c : cmds) {
            if (c.argv.size() >= 2 && c.argv[0] == L"SPECIAL") {
                HandleSpecial(c.argv);
                continue;
            }
            JobMsg* m = new JobMsg{ 0, DispCmd(c.argv) };
            PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)m, 0);
            DWORD idleMs = 0;   // 网络类命令 10 秒无响应即终止报提示（本地命令不启用）
            if (c.argv.size() > 3 && c.argv[1] == L"-c") {
                const wchar_t* v = c.argv[3].c_str();
                if (!_wcsicmp(v, L"push") || !_wcsicmp(v, L"pull") || !_wcsicmp(v, L"fetch") || !_wcsicmp(v, L"ls-remote"))
                    idleMs = 10000;
            }
            GitResult r = RunGit(c.argv, repo, 1800 * 1000, idleMs);
            if (!r.out.empty()) {
                for (auto& ln : SplitLines(r.out)) {
                    if (Trim(ln).empty()) continue;
                    JobMsg* om = new JobMsg{ r.rc != 0 ? 2 : 1, ln };
                    PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)om, 0);
                }
            } else if (r.rc == 0) {
                JobMsg* om = new JobMsg{ 1, L"[无输出] 命令执行成功，但没有内容（例如：没有对应改动、没有新提交）。" };
                PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)om, 0);
            }
            if (r.rc != 0 && !c.optional) {
                // 认证失败单独提示：多半是凭据管理器里缓存的远端凭据失效，新手最易懵
                if (r.out.find(L"Authentication failed") != std::wstring::npos
                    || r.out.find(L"Failed to authenticate") != std::wstring::npos
                    || r.out.find(L"401") != std::wstring::npos) {
                    JobMsg* am = new JobMsg{ 2, L"凭据被拒：Windows 缓存的远端账号可能已失效。可先重试一次（有时凭据会自动刷新）；"
                                             L"若反复失败，请到 控制面板→用户账户→凭据管理器→普通凭据，删除该地址的 git:http://… 条目后重试，会重新弹窗输入账号密码。" };
                    PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)am, 0);
                }
                JobMsg* em = new JobMsg{ 2, Fmt(L"[退出码 %d] 出错，已中止后续步骤。", r.rc) };
                PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)em, 0);
                break;
            }
        }
        PostMessageW(g_main, WM_APP_JOBDONE, 0, 0);
    }).detach();
}
static void Exec(std::initializer_list<std::vector<std::wstring>> cmds) {
    std::vector<JobCmd> v;
    for (auto& c : cmds) v.push_back({ c, false });
    ExecAsync(std::move(v));
}
static void ExecV(std::vector<std::vector<std::wstring>> cmds) {
    std::vector<JobCmd> v;
    for (auto& c : cmds) v.push_back({ c, false });
    ExecAsync(std::move(v));
}
// SPECIAL：init（一键初始化）/ back_main（回主分支）
static std::string WideToUtf8(const std::wstring& w);   // 定义在下方，init 自动提交要用
static void HandleSpecial(const std::vector<std::wstring>& argv) {
    const std::wstring kind = argv[1];
    if (kind == L"init") {
        GitResult r = RunGit(G({ L"init", L"-b", L"main" }), g_repo, 120000);
        if (!r.out.empty()) { JobMsg* m = new JobMsg{ 1, Trim(r.out) }; PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)m, 0); }
        if (r.rc != 0) {
            r = RunGit(G({ L"init" }), g_repo, 120000);   // 老 git 不支持 -b
            if (!r.out.empty()) { JobMsg* m = new JobMsg{ 1, Trim(r.out) }; PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)m, 0); }
            if (r.rc != 0) {
                JobMsg* e = new JobMsg{ 2, L"git init 失败，请检查上面报错。" };
                PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)e, 0);
                return;
            }
        }
        JobMsg* m = new JobMsg{ 1, L"初始化完成（未提交）。下一步按流程卡：③ 点【一键配置ignore】生成忽略规则，"
                                   L"完成后自动执行④首次提交（消息统一为 chore: 初始化仓库），最后【配置远程】配好远端地址，用【推送(commit)】完成首推。" };
        PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)m, 0);
    } else if (kind == L"back_main") {
        bool done = false;
        for (const wchar_t* b : { L"main", L"master" }) {
            JobMsg* m = new JobMsg{ 0, std::wstring(L"git checkout ") + b };
            PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)m, 0);
            GitResult r = RunGit(G({ L"checkout", b }), g_repo, 1800 * 1000);
            if (!r.out.empty()) { JobMsg* om = new JobMsg{ r.rc != 0 ? 2 : 1, Trim(r.out) }; PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)om, 0); }
            if (r.rc == 0) { done = true; break; }
        }
        if (!done) {
            JobMsg* e = new JobMsg{ 2, L"未找到 main 或 master 分支。" };
            PostMessageW(g_main, WM_APP_JOBMSG, (WPARAM)e, 0);
        }
    }
}

// ============================ 通用弹窗 ======================================
static bool Confirm(HWND parent, const std::wstring& msg) {
    return MessageBoxW(parent, msg.c_str(), L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES;
}
static void Warn(HWND parent, const std::wstring& msg) { MessageBoxW(parent, msg.c_str(), L"提示", MB_OK | MB_ICONWARNING); }
static void Info(HWND parent, const std::wstring& msg) { MessageBoxW(parent, msg.c_str(), L"提示", MB_OK | MB_ICONINFORMATION); }
// 居中输入弹窗：OK 返回文本，取消返回 nullopt（对应 Python 版 ask）
// 自建弹窗的模态状态：按钮点击的 WM_COMMAND 走 #32770 系统过程（同步），
// 不经过外层 GetMessage 循环——必须在子类过程里接住，否则点 OK/Cancel 弹窗永不关闭
struct AskState {
    HWND hEdit = nullptr;
    std::optional<std::wstring> result;
    bool done = false;
};
static LRESULT CALLBACK AskDlgSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    AskState* st = (AskState*)data;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK && st->hEdit) {
                int len = GetWindowTextLengthW(st->hEdit);
                std::wstring s(len, L'\0');
                GetWindowTextW(st->hEdit, &s[0], len + 1);
                st->result = Trim(s);
            } else {
                st->result = std::nullopt;
            }
            st->done = true;
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE:
        st->done = true;
        st->result = std::nullopt;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
static std::optional<std::wstring> AskText(HWND parent, const wchar_t* title, const std::wstring& label, const std::wstring& def = L"") {
    int m = (int)(14 * g_scale), bw = (int)(80 * g_scale);
    // 标签支持多行（清空Git重来的确认说明很长），行数决定标签高度
    int lines = 1;
    for (wchar_t c : label) if (c == L'\n') ++lines;
    int lblH = lines * (int)(24 * g_scale) + (int)(6 * g_scale);
    int editH = (int)(24 * g_scale), btnH = (int)(28 * g_scale);
    // 客户区高度按内容反推，再用 AdjustWindowRectEx 补上标题栏/边框——
    // 否则窗口高度不含标题栏，底部按钮会被裁掉下半截
    int ch = m + lblH + (int)(10 * g_scale) + editH + (int)(16 * g_scale) + btnH + m;
    RECT rcFrame = { 0, 0, (int)(470 * g_scale), ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int w = rcFrame.right - rcFrame.left, h = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2, y = pr.top + ((pr.bottom - pr.top) - h) / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    AskState state;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, parent, nullptr, g_hInst, nullptr);
    SetWindowSubclass(dlg, AskDlgSub, 1, (DWORD_PTR)&state);
    HWND hLabel = CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE,
        m, m, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    state.hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        m, m + lblH + (int)(10 * g_scale), w - 2 * m, editH, dlg, (HMENU)100, g_hInst, nullptr);
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        w - 2 * bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDOK, g_hInst, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        w - bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDCANCEL, g_hInst, nullptr);
    SendMessageW(hLabel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(state.hEdit, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.hEdit);
    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        // 回车→默认按钮(IDOK)、Esc→IDCANCEL、Tab 焦点切换，全由 IsDialogMessageW 标准转换
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    return state.result;
}
// 双输入弹窗：OK 返回两个文本（各自可留空），取消返回 nullopt（用于同窗填写 name/email 等成对项）
struct Ask2State {
    HWND hEdit1 = nullptr, hEdit2 = nullptr;
    std::optional<std::pair<std::wstring, std::wstring>> result;
    bool done = false;
};
static LRESULT CALLBACK Ask2DlgSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    Ask2State* st = (Ask2State*)data;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK && st->hEdit1 && st->hEdit2) {
                auto get = [](HWND e) {
                    int len = GetWindowTextLengthW(e);
                    std::wstring s(len, L'\0');
                    GetWindowTextW(e, &s[0], len + 1);
                    return Trim(s);
                };
                st->result = std::make_pair(get(st->hEdit1), get(st->hEdit2));
            } else {
                st->result = std::nullopt;
            }
            st->done = true;
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE:
        st->done = true;
        st->result = std::nullopt;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
static std::optional<std::pair<std::wstring, std::wstring>> AskText2(HWND parent, const wchar_t* title,
    const std::wstring& label1, const std::wstring& def1,
    const std::wstring& label2, const std::wstring& def2) {
    int m = (int)(14 * g_scale), bw = (int)(80 * g_scale);
    int lblH = (int)(24 * g_scale) + (int)(6 * g_scale);
    int editH = (int)(24 * g_scale), btnH = (int)(28 * g_scale), gap = (int)(16 * g_scale);
    // 客户区高度按两行"标签+输入框"反推，再用 AdjustWindowRectEx 补上标题栏/边框
    int ch = m + lblH + (int)(6 * g_scale) + editH + gap + lblH + (int)(6 * g_scale) + editH
           + (int)(16 * g_scale) + btnH + m;
    RECT rcFrame = { 0, 0, (int)(470 * g_scale), ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int w = rcFrame.right - rcFrame.left, h = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2, y = pr.top + ((pr.bottom - pr.top) - h) / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    Ask2State state;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, parent, nullptr, g_hInst, nullptr);
    SetWindowSubclass(dlg, Ask2DlgSub, 1, (DWORD_PTR)&state);
    HWND hLabel1 = CreateWindowExW(0, L"STATIC", label1.c_str(), WS_CHILD | WS_VISIBLE,
        m, m, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    state.hEdit1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def1.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        m, m + lblH + (int)(6 * g_scale), w - 2 * m, editH, dlg, (HMENU)100, g_hInst, nullptr);
    int y2 = m + lblH + (int)(6 * g_scale) + editH + gap;
    HWND hLabel2 = CreateWindowExW(0, L"STATIC", label2.c_str(), WS_CHILD | WS_VISIBLE,
        m, y2, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    state.hEdit2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def2.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        m, y2 + lblH + (int)(6 * g_scale), w - 2 * m, editH, dlg, (HMENU)101, g_hInst, nullptr);
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        w - 2 * bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDOK, g_hInst, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        w - bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDCANCEL, g_hInst, nullptr);
    SendMessageW(hLabel1, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(state.hEdit1, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hLabel2, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(state.hEdit2, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.hEdit1);
    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        // 回车→默认按钮(IDOK)、Esc→IDCANCEL、Tab 焦点切换，全由 IsDialogMessageW 标准转换
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    return state.result;
}
// ============================ 检查更新结果与弹窗 =============================
struct UpdateResult {
    int kind = 2;                  // 0=有新版 1=已最新 2=网络失败
    std::wstring tag, notes;       // 最新版本标签 / 发布说明（已还原转义）
    std::wstring exeUrl;           // assets 里 exe 附件直链（kind=0 时有效）
    bool rateLimit = false;        // kind=2 时区分：GitHub 匿名配额用尽 / 一般网络问题
};
static HWND g_hAboutDlg = nullptr;          // 关于弹窗（开着时检查结果写回弹窗）
static std::atomic<bool> g_updateRunning{ false };
static bool g_updateNagged = false;         // 自动检测的新版提示每会话只弹一次
// 当前显示的版本：即构建时注入的最新 tag（build.bat 从 git describe 剥离 -N-ghash 后缀生成）
// 对外用户随资产携带；开发机未打 tag 时为最近一个 tag（未发布=不提示升级），单一来源无运行时 git 查询
static std::wstring CurAppVersion() { return std::wstring(APP_VERSION); }
/**
  ******************************************************************************
  * @brief    后台线程检查 GitHub 最新 Release（releases/latest）
  * @param    autoMode true=自动检测（启动/10分钟周期）；false=弹窗内手动检查
  * @retval   true=已启动检查；false=已有检查/下载在进行，本次未启动
  * @note     结果经 WM_APP_UPDATE 投递主线程；自动模式失败静默，手动模式弹窗显示网络提示
  ******************************************************************************
  */
static bool CheckUpdateAsync(bool autoMode) {
    bool expected = false;
    if (!g_updateRunning.compare_exchange_strong(expected, true)) return false;   // 已有检查/下载在进行
    std::thread([autoMode]() {
        auto* r = new UpdateResult();
        bool rate = false;
        std::string json = HttpGetUtf8(UPDATE_API_URL, &rate);
        std::wstring tag = json.empty() ? L"" : JsonUnescape(JsonGetStringRaw(json, "tag_name"));
        r->rateLimit = rate;
        if (!tag.empty()) {
            r->tag = tag;
            r->notes = JsonUnescape(JsonGetStringRaw(json, "body"));
            r->kind = VersionCompare(tag, CurAppVersion()) > 0 ? 0 : 1;
            if (r->kind == 0) r->exeUrl = JsonGetAssetExe(json);
        }   // 其余保持 kind=2（网络失败/限流）
        PostMessageW(g_main, WM_APP_UPDATE, (WPARAM)r, (LPARAM)(autoMode ? 1 : 0));
        g_updateRunning = false;   // 线程内清零：自动弹窗的模态循环会卡住 handler，清零不能放 handler 里
    }).detach();
    return true;
}
// 升级说明文本（限行数防弹窗过高：高度自适应仍设上限）：关于弹窗结果区的内容主体
static std::wstring UpdateFoundText(const UpdateResult& r) {
    std::wstring s = L"检测到新版本：" + r.tag + L"（当前 " + CurAppVersion() + L"）\n升级内容：\n";
    std::wstring body = r.notes.empty() ? L"（无详细说明）" : r.notes;
    int lines = 0; size_t cut = 0;
    for (size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == L'\n') {
            if (++lines > 8) { body = body.substr(0, cut) + L"…（说明较长已截断，完整内容见发布页）"; break; }
            cut = i + 1;
        }
    }
    return s + body;
}
// ============================ 一键升级（下载+换身）===========================
struct UpgradeResult {
    bool ok = false;
    std::wstring msg;              // ok=新 exe 临时文件路径；!ok=失败原因
};
static std::atomic<bool> g_upgrading{ false };
/**
  ******************************************************************************
  * @brief    后台线程下载新版本 exe 到 %TEMP%
  * @param    url exe 附件直链（调用前应确认 g_updateRunning 已被本函数占用）
  * @param    onPct 下载进度回调（百分比 0~100，可为 nullptr）
  * @retval   true=已启动下载；false=已有检查/下载在进行，本次未启动
  * @note     结果经 WM_APP_UPGRADE 投递主线程执行换身；>100KB 防把错误页当 exe；
  *           下载期间占用 g_updateRunning，挡住定时自动检查插队
  ******************************************************************************
  */
static bool DownloadAsync(const std::wstring& url, void (*onPct)(int) = nullptr) {
    bool expected = false;
    if (!g_updateRunning.compare_exchange_strong(expected, true)) return false;   // 已有检查/下载在进行
    std::thread([url, onPct]() {
        auto* ur = new UpgradeResult();
        std::string data = HttpDownload(url, onPct);
        if (data.size() > 100 * 1024) {   // exe 至少几百 KB，防 403/404 错误页被误存
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            fs::path f = fs::path(tmp) / L"GitMenuTool_upgrade.exe";
            std::error_code ec;
            fs::remove(f, ec);
            std::ofstream ofs(f, std::ios::binary | std::ios::trunc);
            ofs.write(data.data(), (std::streamsize)data.size());
            ofs.close();
            if (fs::exists(f, ec) && fs::file_size(f, ec) == data.size()) { ur->ok = true; ur->msg = f.wstring(); }
        }
        if (!ur->ok) ur->msg = L"下载失败或文件不完整，请检查网络后重试";
        PostMessageW(g_main, WM_APP_UPGRADE, (WPARAM)ur, 0);
        g_updateRunning = false;   // 与检查共用防重入标志，线程末尾清零（同已知教训）
    }).detach();
    return true;
}
/**
  ******************************************************************************
  * @brief    换身：当前 exe 改名 .old 让位 → 新 exe 顶上原位 → 延迟删 .old → 重启新 exe
  * @param    tmpExe 已下载的新版本 exe 临时路径
  * @retval   空串=成功已安排重启；否则返回失败原因（.old 已回滚原名）
  * @note     Windows 允许改名运行中的 exe；跨卷移动需 MOVEFILE_COPY_ALLOWED；
  *           杀软可能短暂锁文件，各步 5 次×300ms 重试
  ******************************************************************************
  */
static std::wstring DoUpgradeSwap(const std::wstring& tmpExe) {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path cur(exePath), oldf = cur; oldf.replace_extension(L".old");
    std::error_code ec;
    fs::remove(oldf, ec);   // 清掉历史 .old
    bool ok = false;
    for (int i = 0; i < 5 && !ok; ++i) {   // ① 当前 exe 让位（运行中允许改名）
        ok = MoveFileExW(cur.c_str(), oldf.c_str(), MOVEFILE_COPY_ALLOWED);
        if (!ok) Sleep(300);
    }
    if (!ok) return L"无法让位当前 exe（可能被杀毒软件锁定），请手动关闭工具后替换";
    ok = false;
    for (int i = 0; i < 5 && !ok; ++i) {   // ② 新 exe 顶上原位（%TEMP% 与工具目录跨卷时走复制语义）
        ok = MoveFileExW(tmpExe.c_str(), cur.c_str(), MOVEFILE_COPY_ALLOWED);
        if (!ok) Sleep(300);
    }
    if (!ok) {   // 回滚：把 .old 改回原名，工具不受影响
        MoveFileExW(oldf.c_str(), cur.c_str(), MOVEFILE_COPY_ALLOWED);
        return L"新版本文件就位失败，已恢复原版本";
    }
    // ③ 延迟 2 秒删除 .old（当前进程还占着映像）
    STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::wstring args = L"cmd.exe /c timeout /t 2 /nobreak >nul & del /f \"" + oldf.wstring() + L"\"";
    CreateProcessW(nullptr, args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                   nullptr, cur.parent_path().wstring().c_str(), &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    // ④ 启动新版本
    si = { sizeof(si) };
    CreateProcessW(cur.c_str(), nullptr, nullptr, nullptr, FALSE, 0,
                   nullptr, cur.parent_path().wstring().c_str(), &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    return L"";
}
/**
  ******************************************************************************
  * @brief    关于弹窗的过程：检查更新 / 下载 / 关闭
  * @param    h 弹窗句柄
  * @param    data AboutState 指针
  * @retval   None
  * @note     检查结果不走这里——WM_APP_UPDATE 在主窗口过程中直接改本弹窗控件
  ******************************************************************************
  */
struct AboutState {
    bool done = false;
    bool downloading = false;                         // 新版下载进行中（下载按钮显示百分比）
    std::wstring dlUrl;                               // 新版 exe 直链（点下载时用）
    int w = 0, ch = 0, m = 0, bw = 0, btnH = 0;
    int linkY = 0, linkH = 0;                         // github 发布页链接行基线/高度（紧贴按钮行上方）
    int y1 = 0;                                       // 按钮行基线（检查更新）
};
static AboutState g_aboutState;
// 下载进度：后台线程投递百分比到主线程（写回关于弹窗的下载按钮）
static void PostDlPct(int pct) { PostMessageW(g_main, WM_APP_DLPCT, (WPARAM)pct, 0); }
/**
  ******************************************************************************
  * @brief    在关于弹窗内集成检查结果：文本区（+下载按钮），弹窗高度自适应
  * @param    dlg 关于弹窗句柄
  * @param    text 结果文本（居中，自动换行）
  * @param    showDl true=显示下载按钮（检测到新版本时）
  * @param    dlUrl 新版 exe 直链（showDl 时有效）
  * @retval   None
  * @note     文本按弹窗宽实测换行高度；点下载后弹窗保持，下载按钮原地显示百分比，
  *           下载结束由 WM_APP_UPGRADE 恢复按钮并收尾
  ******************************************************************************
  */
static void ShowResultInAbout(HWND dlg, const std::wstring& text, bool showDl,
                              const std::wstring& dlUrl = L"") {
    AboutState& s = g_aboutState;
    HDC dc = GetDC(nullptr);                                   // 结果文本按弹窗宽实测换行后所需高度
    HFONT of = (HFONT)SelectObject(dc, g_fontUISmall);
    RECT tr = { 0, 0, s.w - 2 * s.m, 0 };
    DrawTextW(dc, text.c_str(), -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, of); ReleaseDC(nullptr, dc);
    int textH = tr.bottom - tr.top;
    int gap = (int)(6 * g_scale);
    int yText = s.y1 + s.btnH + gap;                           // 结果文本区顶（检查更新按钮下方）
    int needCh = showDl ? yText + textH + gap + s.btnH + s.m   // 有下载按钮：文本区+下载行
                        : yText + textH + s.m;                 // 无下载按钮：仅文本区
    HWND hText = GetDlgItem(dlg, IDC_ABOUT_RESULT);
    HWND hDl = GetDlgItem(dlg, IDC_ABOUT_DL);
    if (!hText) {                                              // 首次：创建结果文本区
        hText = CreateWindowExW(0, L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX, s.m, yText, s.w - 2 * s.m, textH,
            dlg, (HMENU)IDC_ABOUT_RESULT, g_hInst, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_fontUISmall, TRUE);
    } else {                                                   // 再次检查：更新文本与位置
        SetWindowTextW(hText, text.c_str());
        MoveWindow(hText, s.m, yText, s.w - 2 * s.m, textH, TRUE);
        ShowWindow(hText, SW_SHOW);                            // 点检查时曾隐藏，结果回来要重新显示
    }
    if (showDl) {                                              // 下载按钮：创建或更新后显示
        if (!hDl) {
            hDl = CreateWindowExW(0, L"BUTTON", L"下载", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                (s.w - s.bw) / 2, yText + textH + gap, s.bw, s.btnH, dlg, (HMENU)IDC_ABOUT_DL, g_hInst, nullptr);
            SendMessageW(hDl, WM_SETFONT, (WPARAM)g_fontUISmall, TRUE);
        } else {
            MoveWindow(hDl, (s.w - s.bw) / 2, yText + textH + gap, s.bw, s.btnH, TRUE);
            SetWindowTextW(hDl, L"下载");
            EnableWindow(hDl, TRUE);
        }
        ShowWindow(hDl, SW_SHOW);
        s.dlUrl = dlUrl;
    } else if (hDl) {                                          // 无下载：隐藏按钮
        ShowWindow(hDl, SW_HIDE);
    }
    s.downloading = false;
    if (needCh != s.ch) {                                      // 弹窗高度自适应
        RECT fr; GetWindowRect(dlg, &fr);
        SetWindowPos(dlg, nullptr, 0, 0, fr.right - fr.left, fr.bottom - fr.top + (needCh - s.ch),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        s.ch = needCh;
    }
}
/**
  ******************************************************************************
  * @brief    关于弹窗回填检查结果：三种结果都集成进弹窗结果区，不再弹提示框
  * @param    dlg 关于弹窗句柄
  * @param    r 检查结果（kind 0=新版本 1=已最新 2=失败）
  * @param    fromAuto true=后台自动检测的结果（不写入结果区，结果区只显示手动操作的结果）
  * @retval   None
  * @note     启动自动弹窗走预填（fromAuto=false）；下载进度显示在下载按钮上，完成后恢复按钮
  ******************************************************************************
  */
static void AboutApplyResult(HWND dlg, const UpdateResult& r, bool fromAuto = false) {
    if (fromAuto) return;              // 自动检测结果不写入结果区，避免未点检查就"已是最新版本"
    HWND hCheck = GetDlgItem(dlg, IDC_ABOUT_CHECK);
    if (r.kind == 0) {
        ShowResultInAbout(dlg, UpdateFoundText(r), true, r.exeUrl);   // 新版本号+升级内容+下载
    } else if (r.kind == 1) {
        ShowResultInAbout(dlg, L"已是最新版本。", false);                      // 已最新：结果区显示
    } else {
        // 失败：结果区显示原因
        ShowResultInAbout(dlg,
            r.rateLimit ? L"检查失败：GitHub 接口访问配额已用完（匿名每小时 60 次）。\n请一小时后再试，或到 github 发布页手动查看。"
                        : L"检查失败：无法连接更新服务器（或仓库暂无发布版本）。\n请确认电脑可以上网后，再点一次【检查更新】。",
            false);
    }
    EnableWindow(hCheck, TRUE);        // 恢复检查按钮（换身成功会直接退出进程，走不到这）
    SetWindowTextW(hCheck, L"检查更新");
}
// 关于弹窗 github 链接行：手型光标 + 单击打开（与状态卡"远端地址"同款交互）
static LRESULT CALLBACK AboutLinkSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONUP: {                    // 单击：打开链接（截取文本中的 http 段，跳过"发布页："前缀）
        int len = GetWindowTextLengthW(h);
        std::wstring text(len, L'\0');
        GetWindowTextW(h, &text[0], len + 1);
        size_t p = text.find(L"http");
        if (p != std::wstring::npos)
            ShellExecuteW(nullptr, L"open", text.c_str() + p, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    }
    return DefSubclassProc(h, m, w, l);
}
static LRESULT CALLBACK AboutDlgSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    AboutState* st = (AboutState*)data;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_ABOUT_CHECK) {
            HWND hBtn = GetDlgItem(h, IDC_ABOUT_CHECK);
            AboutState& s = g_aboutState;
            if (!CheckUpdateAsync(false)) {                // 已有检查/下载在进行：不动结果区，恢复按钮即可
                EnableWindow(hBtn, TRUE);
                SetWindowTextW(hBtn, L"检查更新");
                return 0;
            }
            HWND hRes = GetDlgItem(h, IDC_ABOUT_RESULT);   // 启动成功才清上次结果：结果不是常驻，仅显示到下次检查
            HWND hDl = GetDlgItem(h, IDC_ABOUT_DL);
            if (hRes) ShowWindow(hRes, SW_HIDE);
            if (hDl) ShowWindow(hDl, SW_HIDE);
            int baseCh = s.y1 + s.btnH + s.m;              // 弹窗收回基础高度
            if (s.ch != baseCh) {
                RECT fr; GetWindowRect(h, &fr);
                SetWindowPos(h, nullptr, 0, 0, fr.right - fr.left, fr.bottom - fr.top + (baseCh - s.ch),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                s.ch = baseCh;
            }
            EnableWindow(hBtn, FALSE);
            SetWindowTextW(hBtn, L"正在检查…");
            return 0;
        }
        if (id == IDC_ABOUT_DL && !st->downloading) {      // 下载：弹窗保持，下载按钮原地变百分比
            if (!DownloadAsync(st->dlUrl, PostDlPct)) return 0;   // 已有任务在进行：忽略本次点击
            st->downloading = true;
            EnableWindow(GetDlgItem(h, IDC_ABOUT_CHECK), FALSE);
            SetWindowTextW(GetDlgItem(h, IDC_ABOUT_DL), L"0%");
            return 0;
        }
        if (id == IDCANCEL) { st->done = true; DestroyWindow(h); return 0; }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        if ((HWND)l == GetDlgItem(h, IDC_ABOUT_LINK)) {   // 链接行：蓝字，底色取弹窗实际擦底画刷（避免白底）
            SetTextColor(dc, STAT_BLU);
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)SendMessageW(h, WM_CTLCOLORDLG, (WPARAM)dc, (LPARAM)h);
        }
        break;
    }
    case WM_DESTROY:
        g_hAboutDlg = nullptr;
        return 0;
    case WM_CLOSE:
        st->done = true;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
/**
  ******************************************************************************
  * @brief    关于弹窗：工具名 + 当前版本 + 作者 + github发布页链接 + 检查更新（+新版结果区）
  * @param    pre 非空时预填检查结果（自动检测弹出版），可传 nullptr
  * @retval   None
  * @note     模态骨架与 AskText 相同（子类过程收命令 + 循环只泵消息）；自动检测也只弹本窗
  ******************************************************************************
  */
static void ShowAboutDialog(const UpdateResult* pre) {
    AboutState& s = g_aboutState;
    s.done = false;
    s.m = (int)(10 * g_scale); s.btnH = (int)(26 * g_scale);
    s.bw = (int)(95 * g_scale);
    s.w = (int)(300 * g_scale);
    // 链接行整行显示不折行：按 9pt 字体实测文字宽度撑大弹窗最小宽度
    const std::wstring linkText = L"github发布页：https://github.com/elliotaldersonX7/GitMenuTool/releases";
    {
        HDC dc = GetDC(nullptr);
        HFONT of = (HFONT)SelectObject(dc, g_fontUISmall);
        SIZE ts; GetTextExtentPoint32W(dc, linkText.c_str(), (int)linkText.size(), &ts);
        SelectObject(dc, of); ReleaseDC(nullptr, dc);
        int need = ts.cx + 2 * s.m + (int)(12 * g_scale);
        if (need > s.w) s.w = need;
    }
    s.linkH = (int)(18 * g_scale);
    s.linkY = s.m + (int)(62 * g_scale);                   // 链接行：工具名 24 + 间隙 + 版本/作者 36（与正文节距一致）
    s.y1 = s.linkY + s.linkH + (int)(2 * g_scale);         // 按钮行紧随链接行
    s.ch = s.y1 + s.btnH + s.m;                            // 弹窗高度自底向上收紧
    RECT rcFrame = { 0, 0, s.w, s.ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rcFrame.right - rcFrame.left, wh = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(g_main, &pr);
    int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"关于",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, ww, wh, g_main, nullptr, g_hInst, nullptr);
    DWORD ctr = WS_CHILD | WS_VISIBLE | SS_CENTER;         // 全部文字居中
    HWND hTitle = CreateWindowExW(0, L"STATIC", APP_TITLE, ctr,
        s.m, s.m, s.w - 2 * s.m, (int)(24 * g_scale), dlg, (HMENU)IDC_ABOUT_INFO, g_hInst, nullptr);
    HWND hVer = CreateWindowExW(0, L"STATIC",
        (L"当前版本：" + CurAppVersion() + L"\n作者：" + APP_AUTHOR).c_str(), ctr,
        s.m, s.m + (int)(24 * g_scale), s.w - 2 * s.m, (int)(36 * g_scale), dlg, (HMENU)IDC_ABOUT_VER, g_hInst, nullptr);
    HWND hLink = CreateWindowExW(0, L"STATIC", linkText.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,   // SS_NOTIFY：静态可收鼠标消息
        s.m, s.linkY, s.w - 2 * s.m, s.linkH, dlg, (HMENU)IDC_ABOUT_LINK, g_hInst, nullptr);
    HWND hCheck = CreateWindowExW(0, L"BUTTON", L"检查更新", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        (s.w - s.bw) / 2, s.y1, s.bw, s.btnH, dlg, (HMENU)IDC_ABOUT_CHECK, g_hInst, nullptr);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontUIBig, TRUE);   // 工具名大 3 号
    SendMessageW(hVer, WM_SETFONT, (WPARAM)g_fontUISmall, TRUE);
    SendMessageW(hLink, WM_SETFONT, (WPARAM)g_fontUISmall, TRUE);
    SetWindowSubclass(hLink, AboutLinkSub, 1, 0);   // 手型光标 + 单击打开链接
    SendMessageW(hCheck, WM_SETFONT, (WPARAM)g_fontUISmall, TRUE);
    SetWindowSubclass(dlg, AboutDlgSub, 1, (DWORD_PTR)&s);
    EnableWindow(g_main, FALSE);
    g_hAboutDlg = dlg;
    ShowWindow(dlg, SW_SHOW);
    if (pre) AboutApplyResult(dlg, *pre);   // 自动检测到新版：结果直接集成在弹窗内（无二级弹窗）
    MSG msg;
    while (!s.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(g_main, TRUE);
    SetActiveWindow(g_main);
}
// 全局配置弹窗状态：两个输入框 + 默认分支下拉；OK 收集三行文本，取消置空 result
struct Cfg3State {
    HWND hEdit1 = nullptr, hEdit2 = nullptr, hCombo = nullptr;
    struct Val { std::wstring v1, v2, v3; };
    std::optional<Val> result;
    bool done = false;
};
/**
  ******************************************************************************
  * @brief    全局配置弹窗的命令处理：OK/取消时收集三控件内容
  * @param    h 弹窗句柄
  * @param    data Cfg3State 指针（创建弹窗时挂入）
  * @retval   None
  * @note     OK 时读取两个输入框与下拉选中项回填 result；取消时 result 置空
  ******************************************************************************
  */
static LRESULT CALLBACK Cfg3DlgSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    Cfg3State* st = (Cfg3State*)data;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK && st->hEdit1 && st->hEdit2 && st->hCombo) {
                auto get = [](HWND e) {
                    int len = GetWindowTextLengthW(e);
                    std::wstring s(len, L'\0');
                    GetWindowTextW(e, &s[0], len + 1);
                    return Trim(s);
                };
                Cfg3State::Val v;
                v.v1 = get(st->hEdit1);
                v.v2 = get(st->hEdit2);
                int sel = (int)SendMessageW(st->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    int n = (int)SendMessageW(st->hCombo, CB_GETLBTEXTLEN, sel, 0);
                    v.v3.resize(n);
                    SendMessageW(st->hCombo, CB_GETLBTEXT, sel, (LPARAM)v.v3.data());
                }
                st->result = v;
            } else {
                st->result = std::nullopt;
            }
            st->done = true;
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE:
        st->done = true;
        st->result = std::nullopt;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
/**
  ******************************************************************************
  * @brief    三控件模态弹窗：输入框1/输入框2/下拉框3，布局同 AskText2 再加一行
  * @param    parent 父窗口（弹窗期间被禁用）
  * @param    title 弹窗标题
  * @param    label1/def1 第一行标签与输入框预填值
  * @param    label2/def2 第二行标签与输入框预填值
  * @param    label3 第三行标签（下拉说明）
  * @param    choices 下拉候选列表
  * @param    def3 下拉预选值（无匹配则选第一项）
  * @retval   OK 返回三行文本（v1/v2 可为空串，v3 为选中分支）；取消返回 std::nullopt
  * @note     模态循环内 IsDialogMessageW 负责回车/Esc/Tab 转换；回车=OK、Esc=取消
  ******************************************************************************
  */
static std::optional<Cfg3State::Val> AskCfg3(HWND parent, const wchar_t* title,
    const std::wstring& label1, const std::wstring& def1,
    const std::wstring& label2, const std::wstring& def2,
    const std::wstring& label3, const std::vector<std::wstring>& choices, const std::wstring& def3) {
    int m = (int)(14 * g_scale), bw = (int)(80 * g_scale);
    int lblH = (int)(24 * g_scale) + (int)(6 * g_scale);
    int editH = (int)(24 * g_scale), btnH = (int)(28 * g_scale), gap = (int)(16 * g_scale);
    // 客户区高度按三行"标签+控件"反推，再用 AdjustWindowRectEx 补上标题栏/边框
    int ch = m + lblH + (int)(6 * g_scale) + editH + gap + lblH + (int)(6 * g_scale) + editH + gap
           + lblH + (int)(6 * g_scale) + editH + (int)(16 * g_scale) + btnH + m;
    RECT rcFrame = { 0, 0, (int)(470 * g_scale), ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int w = rcFrame.right - rcFrame.left, h = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - w) / 2, y = pr.top + ((pr.bottom - pr.top) - h) / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    Cfg3State state;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, parent, nullptr, g_hInst, nullptr);
    SetWindowSubclass(dlg, Cfg3DlgSub, 1, (DWORD_PTR)&state);
    HWND hLabel1 = CreateWindowExW(0, L"STATIC", label1.c_str(), WS_CHILD | WS_VISIBLE,
        m, m, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    state.hEdit1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def1.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        m, m + lblH + (int)(6 * g_scale), w - 2 * m, editH, dlg, (HMENU)100, g_hInst, nullptr);
    int y2 = m + lblH + (int)(6 * g_scale) + editH + gap;
    HWND hLabel2 = CreateWindowExW(0, L"STATIC", label2.c_str(), WS_CHILD | WS_VISIBLE,
        m, y2, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    state.hEdit2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def2.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        m, y2 + lblH + (int)(6 * g_scale), w - 2 * m, editH, dlg, (HMENU)101, g_hInst, nullptr);
    int y3 = y2 + lblH + (int)(6 * g_scale) + editH + gap;
    HWND hLabel3 = CreateWindowExW(0, L"STATIC", label3.c_str(), WS_CHILD | WS_VISIBLE,
        m, y3, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    // 下拉框高度参数=编辑框+展开列表区（两项候选取 editH+50 缩放像素足够）
    state.hCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        m, y3 + lblH + (int)(6 * g_scale), w - 2 * m, editH + (int)(50 * g_scale), dlg, (HMENU)102, g_hInst, nullptr);
    for (auto& c : choices) SendMessageW(state.hCombo, CB_ADDSTRING, 0, (LPARAM)c.c_str());
    int sel = (int)SendMessageW(state.hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)def3.c_str());
    SendMessageW(state.hCombo, CB_SETCURSEL, sel >= 0 ? sel : 0, 0);
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        w - 2 * bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDOK, g_hInst, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        w - bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDCANCEL, g_hInst, nullptr);
    SendMessageW(hLabel1, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(state.hEdit1, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hLabel2, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(state.hEdit2, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hLabel3, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(state.hCombo, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.hEdit1);
    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        // 回车→默认按钮(IDOK)、Esc→IDCANCEL、Tab 焦点切换，全由 IsDialogMessageW 标准转换
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    return state.result;
}
// 目录选择对话框（IFileDialog：窗口可拉伸、系统记住尺寸，深目录友好；Vista+，本机 Win7 SP1 满足）
static std::wstring LoadLastDir();   // 前置声明，定义在下方
static std::optional<std::wstring> BrowseFolder(HWND parent, const wchar_t* title) {
    IFileDialog* fd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fd));
    if (FAILED(hr)) return std::nullopt;
    std::optional<std::wstring> result;
    fd->SetTitle(title);
    DWORD opts = 0;
    if (SUCCEEDED(fd->GetOptions(&opts))) fd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    std::wstring last = LoadLastDir();
    if (!last.empty()) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(last.c_str(), nullptr, IID_PPV_ARGS(&psi)))) {
            fd->SetDefaultFolder(psi);   // 默认定位到上次使用的目录
            psi->Release();
        }
    }
    if (SUCCEEDED(fd->Show(parent))) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(fd->GetResult(&psi))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            psi->Release();
        }
    }
    fd->Release();
    return result;
}
// 文件选择对话框
static std::wstring OpenFileDialog(HWND parent, const wchar_t* title, const wchar_t* filter) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW of = { sizeof(of) };
    of.hwndOwner = parent; of.lpstrFilter = filter; of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH; of.lpstrTitle = title; of.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&of)) return L"";
    return buf;
}

// ============================ 目录/持久化 ===================================
static std::string WideToUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static void SaveLastDir2(const std::wstring& d) {
    std::string u = WideToUtf8(d);
    std::ofstream f(fs::path(ExeDir()) / L"last_dir.txt", std::ios::binary | std::ios::trunc);
    if (f) f.write(u.data(), (std::streamsize)u.size());
}
static std::wstring LoadLastDir() {
    fs::path p = fs::path(ExeDir()) / L"last_dir.txt";
    std::error_code ec;
    if (!fs::exists(p, ec)) return L"";
    std::ifstream f(p, std::ios::binary);
    if (!f) return L"";
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::wstring w = Utf8ToWide(raw);
    if (w.empty() && !raw.empty()) w = GbkToWide(raw);   // 兼容 Python 版 GBK 写入
    return Trim(w);
}

// ============================ 状态卡 ========================================
static void RefreshStatusAsync(bool force = false);   // 前向声明：FetchTagDiffAsync 刷新缓存后触发重渲
// 标签与远端的领先/落后（按 tag 名集合比较，与 commit 的 ahead/behind 同语义）
// ls-remote 走网络：异步刷新缓存（30 秒 TTL），渲染只读缓存即时完成，状态卡不因网络卡顿
struct TagDiff {
    int localOnly = 0, remoteOnly = 0, localCnt = 0, remoteCnt = 0;
    bool netOk = true;
    std::wstring localLatest, remoteLatest;   // 本地/远端最新 tag（VersionCompare 取最大）
};
static std::mutex g_tagMtx;
static std::wstring cacheKey;
static std::chrono::steady_clock::time_point cacheAt;
static std::vector<std::wstring> cacheRemote;
static bool cacheOk = false, cacheHas = false;   // cacheOk=最近一次网络是否成功；cacheHas=本 key 已拉取过（失败也记，30 秒后再试）
static std::atomic<bool> g_tagFetching{ false };
static std::chrono::steady_clock::time_point NowT() { return std::chrono::steady_clock::now(); }
// 同步拉取远端标签并更新缓存（走 15 秒超时/10 秒无响应看门狗）
static void FetchTagDiffSync() {
    std::vector<std::wstring> rem;
    bool ok = false;
    if (!g_remote.empty()) {
        GitResult r = RunGit({ g_git, L"-C", g_repo, L"ls-remote", L"--tags", g_remote }, L"", 15000, 10000);
        ok = (r.rc == 0);
        if (ok) {
            for (auto& l : SplitLines(r.out)) {
                size_t tab = l.find(L'\t');
                if (tab == std::wstring::npos) continue;
                std::wstring ref = Trim(l.substr(tab + 1));
                const std::wstring pre = L"refs/tags/";
                if (ref.rfind(pre, 0) != 0) continue;
                std::wstring name = ref.substr(pre.size());
                if (name.size() >= 3 && name.compare(name.size() - 3, 3, L"^{}") == 0) continue;  // 注解标签的解引用行去重
                if (!name.empty()) rem.push_back(name);
            }
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_tagMtx);
        cacheRemote = std::move(rem);
        cacheOk = ok;
        cacheHas = true;            // 失败也记：30 秒 TTL 内不反复打网络
        cacheKey = g_repo + L"|" + g_remote;
        cacheAt = NowT();
    }
}
// 后台拉取远端标签刷新缓存；完成后触发一次状态重渲（渲染走缓存即时完成）
static void FetchTagDiffAsync() {
    bool expected = false;
    if (!g_tagFetching.compare_exchange_strong(expected, true)) return;
    std::thread([]() {
        FetchTagDiffSync();
        g_tagFetching = false;
        RefreshStatusAsync();           // 缓存已刷新：重渲一次（即时）
    }).detach();
}
// 标签差异：force=true（手动刷新）同步等网络拿完整数据；false（自动刷新）只读缓存即时渲染，过期后台补拉
static TagDiff ComputeTagDiff(bool force) {
    TagDiff d;
    std::wstring localTags = GitOut({ L"tag", L"-l" });
    std::vector<std::wstring> ltags;
    for (auto& l : SplitLines(localTags)) { std::wstring t = Trim(l); if (!t.empty()) ltags.push_back(t); }
    d.localCnt = (int)ltags.size();
    auto latestOf = [](const std::vector<std::wstring>& v) {
        std::wstring best;
        for (auto& t : v) if (best.empty() || VersionCompare(t, best) > 0) best = t;
        return best;
    };
    d.localLatest = latestOf(ltags);
    auto fillFromCache = [&]() {
        std::lock_guard<std::mutex> lk(g_tagMtx);
        d.netOk = cacheOk;
        d.remoteCnt = (int)cacheRemote.size();
        d.remoteLatest = latestOf(cacheRemote);
        d.localOnly = 0; d.remoteOnly = 0;
        for (auto& t : ltags)
            if (std::find(cacheRemote.begin(), cacheRemote.end(), t) == cacheRemote.end()) ++d.localOnly;
        for (auto& t : cacheRemote)
            if (std::find(ltags.begin(), ltags.end(), t) == ltags.end()) ++d.remoteOnly;
    };
    bool stale = false;
    {
        std::lock_guard<std::mutex> lk(g_tagMtx);
        stale = !(cacheHas && g_repo + L"|" + g_remote == cacheKey &&
                  std::chrono::duration_cast<std::chrono::seconds>(NowT() - cacheAt).count() < 30);
    }
    if (force) FetchTagDiffSync();             // 手动刷新：同步等数据齐，卡片一次渲染完整
    else if (stale) FetchTagDiffAsync();       // 自动刷新：后台补拉，本次先用缓存
    fillFromCache();
    return d;
}
static void RefreshStatusAsync(bool force);
// 状态 worker：探测仓库状态，构建状态卡富文本（含流程卡/分支/上游/改动/远端）
static std::vector<std::vector<Seg>>* BuildStatusSegs(bool force = false) {
    auto* lines = new std::vector<std::vector<Seg>>;
    if (g_repo.empty()) {
        lines->push_back({ SegT(L"（未选定仓库目录，请先在顶部选定并打开）", STAT_RED) });
        g_flowBlock = -1;
        return lines;
    }
    // ① 全局配置：一次 --list 解析 user.name/email（原先两次子进程）
    bool hasName = false, hasMail = false;
    for (auto& l : SplitLines(GitOut({ L"config", L"--global", L"--list" }))) {
        if (l.rfind(L"user.name=", 0) == 0 && !Trim(l.substr(10)).empty()) hasName = true;
        if (l.rfind(L"user.email=", 0) == 0 && !Trim(l.substr(11)).empty()) hasMail = true;
    }
    bool s0 = hasName && hasMail;
    // ②④⑤ 合并探测：一次 status -b --porcelain 拿 在仓库/分支/上游/领先落后/未提交/有无提交
    std::wstring st = GitOut({ L"status", L"-b", L"--porcelain" });
    bool inRepo = !st.empty();   // 非仓库目录 status 报错返回空；仓库内必有 ## 头
    std::vector<std::wstring> sl = SplitLines(st);
    std::wstring head = sl.empty() ? std::wstring() : sl[0];
    bool noCommit = head.find(L"No commits yet") != std::wstring::npos;
    std::wstring branch = L"（空）", upstream;
    int ahead = 0, behind = 0;
    if (head.rfind(L"## ", 0) == 0) {
        std::wstring info = head.substr(3);
        if (noCommit) {                                  // "## No commits yet on main"
            size_t p = info.find(L"on ");
            if (p != std::wstring::npos) branch = Trim(info.substr(p + 3));
        } else {                                         // "## main...QKIE/main [ahead 2, behind 0]"
            size_t dots = info.find(L"...");
            size_t cut = dots == std::wstring::npos ? info.size() : dots;
            branch = Trim(info.substr(0, cut));
            if (dots != std::wstring::npos) {
                std::wstring rest = info.substr(dots + 3);
                size_t b2 = rest.find(L" [");
                upstream = (b2 == std::wstring::npos) ? Trim(rest) : Trim(rest.substr(0, b2));
                if (b2 != std::wstring::npos) {
                    std::wstring ab = rest.substr(b2 + 2);
                    size_t pa = ab.find(L"ahead "), pb = ab.find(L"behind ");
                    if (pa != std::wstring::npos) ahead = _wtoi(ab.c_str() + pa + 6);
                    if (pb != std::wstring::npos) behind = _wtoi(ab.c_str() + pb + 7);
                }
            }
        }
    }
    // 流程行四环节：①全局配置 ②初始化仓库 ③配置ignore ④远端+首推（可选，不强制）
    struct PillS { const wchar_t* name; bool ok; bool optional; };
    if (!s0 || !inRepo) {
        g_flowBlock = !s0 ? 0 : 1;   // ①②未完成：左侧栏整体置灰禁点（全局配置/初始化均可在顶栏完成）
        PillS pills[5] = { { L"① 全局配置", s0, false }, { L"② 初始化仓库", inRepo, false },
                           { L"③ 配置ignore", false, false }, { L"④ 首次提交", false, false },
                           { L"⑤ 远端+首推(可选)", false, true } };
        std::vector<Seg> flow = { SegT(L"流程：", STAT_TXT, true) };
        bool blockedMarked = false;
        for (auto& p : pills) {
            if (p.ok) flow.push_back(SegT(L" " + std::wstring(p.name) + L" ✔ ", STAT_GRN, false, BG_GRN));
            else if (!blockedMarked) { blockedMarked = true; flow.push_back(SegT(L" " + std::wstring(p.name) + L" ✖ ", STAT_RED, false, BG_RED)); }
            else flow.push_back(SegT(L" " + std::wstring(p.name) + L" · ", STAT_DIM, false, BG_GRY));
        }
        lines->push_back(flow);
        if (!s0) lines->push_back({ SegT(L"（请先点顶栏【⚙ 修改全局配置】填写 user.name 与 user.email）", STAT_DIM) });
        else     lines->push_back({ SegT(L"（该目录还不是 git 仓库，请点顶栏【✚ 初始化仓库】）", STAT_DIM) });
        return lines;
    }
    g_remote = DetectRemote();
    // ③④⑤ 环节状态（分支/上游/有无提交 已由 status 合并探测得出）
    bool s2 = false, s3 = false, s4 = false;
    std::error_code ec;
    fs::path gi = fs::path(g_repo) / L".gitignore";
    s2 = fs::exists(gi, ec) && fs::file_size(gi, ec) > 0;
    s4 = !noCommit;   // ④ 首次提交：status 头无 "No commits yet" 即已有提交
    s3 = !g_remote.empty() && !upstream.empty();
    g_flowBlock = !s2 ? 2 : -1;   // ③未完成锁左栏；④首次提交在③完成后自动执行；⑤远端+首推为可选，不配置也解锁
    // 流程行
    std::vector<Seg> flow = { SegT(L"流程：", STAT_TXT, true) };
    PillS pills[5] = { { L"① 全局配置", true, false }, { L"② 初始化仓库", true, false },
                       { L"③ 配置ignore", s2, false }, { L"④ 首次提交", s4, false },
                       { L"⑤ 远端+首推(可选)", s3, true } };
    bool blockedMarked = false;
    for (auto& p : pills) {
        if (p.ok) flow.push_back(SegT(L" " + std::wstring(p.name) + L" ✔ ", STAT_GRN, false, BG_GRN));
        else if (p.optional) flow.push_back(SegT(L" " + std::wstring(p.name) + L" · ", STAT_DIM, false, BG_GRY));
        else if (!blockedMarked) { blockedMarked = true; flow.push_back(SegT(L" " + std::wstring(p.name) + L" ✖ ", STAT_RED, false, BG_RED)); }
        else flow.push_back(SegT(L" " + std::wstring(p.name) + L" · ", STAT_DIM, false, BG_GRY));
    }
    lines->push_back(flow);
    // 分支行
    lines->push_back({ SegT(L"分支：", STAT_TXT, true), SegT(L" " + branch + L" ", STAT_BLU, true, BG_BLU) });
    // 未提交行（工作区）：纯数字，0 绿色、大于 0 橙色
    int nTotal = 0;
    for (size_t i2 = 1; i2 < sl.size(); ++i2) if (!sl[i2].empty()) ++nTotal;
    if (nTotal > 0)
        lines->push_back({ SegT(L"未提交：", STAT_TXT, true), SegT(L" " + std::to_wstring(nTotal) + L" ", STAT_ORG, false, BG_YLW) });
    else
        lines->push_back({ SegT(L"未提交：", STAT_TXT, true), SegT(L" 0 ", STAT_GRN, false, BG_GRN) });
    // 本地commit VS 远端commit：只显示非零方向（领先/落后不同时出现，真分叉才并列）
    std::vector<Seg> cm = { SegT(L"本地commit VS 远端commit：", STAT_TXT, true) };
    if (!upstream.empty()) {
        if (ahead > 0 && behind > 0) {
            cm.push_back(SegT(L" 领先（" + std::to_wstring(ahead) + L"） ", STAT_BLU, true, BG_BLU));
            cm.push_back(SegT(L" / 落后（" + std::to_wstring(behind) + L"） ", STAT_BLU, true, BG_BLU));
        } else if (ahead > 0) {
            cm.push_back(SegT(L" 领先（" + std::to_wstring(ahead) + L"） ", STAT_BLU, true, BG_BLU));
        } else if (behind > 0) {
            cm.push_back(SegT(L" 落后（" + std::to_wstring(behind) + L"） ", STAT_BLU, true, BG_BLU));
        } else {
            cm.push_back(SegT(L" 0 ", STAT_GRN, false, BG_GRN));
        }
    } else {
        cm.push_back(SegT(L"无上游跟踪", STAT_DIM));
    }
    lines->push_back(cm);
    // 本地tag VS 远端tag：本地最新：xxx 远端最新：xxx
    TagDiff td = ComputeTagDiff(force);
    std::vector<Seg> tg = { SegT(L"本地tag VS 远端tag：", STAT_TXT, true) };
    tg.push_back(SegT(L" 本地最新：" + (td.localLatest.empty() ? std::wstring(L"（无）") : td.localLatest) + L" ",
                      STAT_BLU, true, BG_BLU));
    if (g_remote.empty())
        tg.push_back(SegT(L" 远端最新：（未配置远端）", STAT_DIM));
    else if (td.netOk)
        tg.push_back(SegT(L" 远端最新：" + (td.remoteLatest.empty() ? std::wstring(L"（无）") : td.remoteLatest) + L" ",
                          td.remoteLatest == td.localLatest ? STAT_DIM : STAT_BLU, true, BG_BLU));
    else
        tg.push_back(SegT(L" 远端最新：未知（网络不可达，可配置代理）", STAT_DIM));
    lines->push_back(tg);
    g_statUrl.clear();   // 状态卡不再显示远端两行：别名/地址/默认勾选全部收敛到【配置远程】弹窗（打开即实时列出全部远端）
    return lines;
}
static void RefreshStatusAsync(bool force) {
    bool expected = false;
    if (!g_statusRunning.compare_exchange_strong(expected, true)) return;
    std::thread([force]() {
        auto* segs = BuildStatusSegs(force);
        PostMessageW(g_main, WM_APP_STATUS, (WPARAM)segs, 0);
    }).detach();
}

// ============================ 操作前置检查 ==================================
static bool EnsureRepo() {
    if (!g_repo.empty()) return true;
    Warn(g_main, L"请先在顶部选定 git 仓库目录。");
    return false;
}
static bool NeedRemote() {
    // 返回 true = 已弹窗提示且调用方应中止；false = 有远端，继续执行
    if (g_remote.empty()) {
        Warn(g_main, L"远程操作需配置远程，至少选择一个远端。");
        return true;
    }
    return false;
}
// 仓库是否已有提交（刚初始化未首次提交时 HEAD 不存在，推送/拉取均无意义）
static bool HasCommits() {
    return !Trim(GitOut({ L"rev-parse", L"-q", L"--verify", L"HEAD" })).empty();
}
static bool NeedCommit() {
    // 返回 true = 尚无提交已弹窗提示且调用方应中止
    if (!HasCommits()) {
        Warn(g_main, L"当前分支还没有任何提交（分支为空）。\n推送/拉取前请先点【提交修改】完成首次提交。");
        return true;
    }
    return false;
}
// 是否处于游离 HEAD（detached，如点过【跳转到标签/提交】的查看模式）
static bool IsDetachedHead() {
    return Trim(GitOut({ L"rev-parse", L"--symbolic-full-name", L"HEAD" })).empty();
}
static bool NeedBranch() {
    // 返回 true = 游离 HEAD 已弹窗提示，调用方应中止（此时 CurBranch() 返回字面量 HEAD，推送/拉取命令无意义）
    if (IsDetachedHead()) {
        Warn(g_main, L"当前处于游离 HEAD（detached，查看模式）：此刻的提交不属于任何分支，无法推送/拉取。\n先回到分支再操作：\n· 点【回到 main/master 最新】回到主分支（刚用【跳转到标签/提交】查看后常用）\n· 或在底部命令行输入 git switch -c <分支名> 把当前内容建成新分支");
        return true;
    }
    return false;
}
static std::wstring CurBranch() {
    std::wstring b = Trim(GitOut({ L"rev-parse", L"--abbrev-ref", L"HEAD" }));
    return b.empty() ? L"main" : b;
}
// 从 URL 提取组织/用户名作为远端名
static std::wstring RemoteFromUrl(const std::wstring& url) {
    std::wregex re(L"(?:github\\.com[/:]|192\\.168\\.8\\.24:3000/)([^/]+)/");
    std::wsmatch m;
    if (std::regex_search(url, m, re)) return m[1].str();
    return g_remote.empty() ? L"origin" : g_remote;
}

// ============================ 各操作 ========================================
static void OpStatus()      { Exec({ G({ L"status" }) }); }
static void OpDiff()        { Exec({ G({ L"diff" }), G({ L"diff", L"--staged" }) }); }
static void OpPull()        { if (!EnsureRepo() || NeedRemote() || NeedCommit() || NeedBranch()) return; Exec({ G({ L"pull", L"--no-edit", g_remote, CurBranch() }) }); }   // 显式拉自勾选的默认远端（裸 git pull 走上游跟踪的远端）
static void OpTagList()     { Exec({ G({ L"tag", L"-l" }) }); }
static void OpTagPush()     { if (NeedRemote()) return; Exec({ G({ L"push", g_remote, L"--tags" }) }); }
static void OpRemoteInfo()  { if (NeedRemote()) return; Exec({ G({ L"remote", L"-v" }), G({ L"branch", L"-vv" }) }); }
static void OpStash()       { Exec({ G({ L"stash" }) }); }
static void OpStashPop()    { Exec({ G({ L"stash", L"pop" }) }); }
static void OpBackMain()    { std::vector<JobCmd> v; v.push_back({ { L"SPECIAL", L"back_main" }, false }); ExecAsync(std::move(v)); }

static void OpCommit() {
    if (!EnsureRepo()) return;
    // 游离 HEAD 时不硬拦（临时试验提交是合理需求），但必须说清"悬空提交"的后果
    if (IsDetachedHead()
        && !Confirm(g_main, L"当前处于游离 HEAD（查看模式）：这次提交不属于任何分支，\n之后切回别的分支时会成为无名的\"孤儿提交\"，垃圾回收后消失。\n\n· 只是想临时试验，之后不要了 → 继续\n· 想保留这次提交 → 取消，先在底部命令行输入 git switch -c <分支名> 建分支\n\n仍要提交？"))
        return;
    auto msg = AskText(g_main, L"提交修改", L"提交消息（一行，格式 类型(范围): 中文说明）:");
    if (!msg) return;
    if (msg->empty()) { Warn(g_main, L"消息不能为空。"); return; }
    Exec({ G({ L"add", L"-A" }), G({ L"commit", L"-m", *msg }) });
}
static void OpLogAll() {
    Exec({ G({ L"--no-pager", L"log", L"--all", L"--abbrev=10", L"--pretty=" + std::wstring(PRETTY), L"--date=" + std::wstring(DATEFMT) }) });
}
static void OpShow() {
    if (!EnsureRepo()) return;
    auto sha = AskText(g_main, L"查看某次提交", L"提交号（留空=看最近一次）:");
    if (!sha) return;
    if (sha->empty()) Exec({ G({ L"--no-pager", L"show" }) });
    else Exec({ G({ L"--no-pager", L"show", *sha }) });
}
// 推送核心序列（推送按钮共用）：无上游→fetch 后检查无关历史并询问强推
static void DoPushCore() {
    if (!EnsureRepo() || NeedRemote() || NeedCommit() || NeedBranch()) return;
    std::wstring br = CurBranch();
    GitOut({ L"fetch", L"--quiet", g_remote }, 120000);
    std::wstring head = Trim(GitOut({ L"rev-parse", L"HEAD" }));
    std::wstring rh = Trim(GitOut({ L"rev-parse", L"-q", L"--verify", g_remote + L"/" + br }));
    std::vector<std::vector<std::wstring>> cmds;
    if (!rh.empty() && head != rh) {
        std::wstring mb = Trim(GitOut({ L"merge-base", L"HEAD", rh }));
        if (mb.empty()) {
            // 如清空Git重来后重新初始化的场景：远端旧历史与本地无关
            if (!Confirm(g_main, Fmt(L"远端 %s 分支已有与本地无关的旧历史，普通推送会被拒绝。\n\n是否强制覆盖远端？\n· 远端该分支的旧提交将被丢弃\n· 本地代码与标签会完整推上去", br.c_str())))
                return;
            cmds.push_back(G({ L"push", L"-u", g_remote, br, L"--force" }));
        } else {
            cmds.push_back(G({ L"push", L"-u", g_remote, br }));
        }
    } else {
        // 一律显式推到勾选的默认远端：裸 git push 走上游跟踪的远端，勾选就形同虚设；-u 顺带把跟踪关系切到勾选的远端
        cmds.push_back(G({ L"push", L"-u", g_remote, br }));
    }
    ExecV(cmds);
}
static void OpPush() {
    if (!EnsureRepo() || NeedRemote() || NeedBranch()) return;
    DoPushCore();   // 一律走 DoPushCore 显式推到勾选的默认远端（裸 git push 走上游跟踪，勾选无效）
}
/**
  ******************************************************************************
  * @brief    强制推送当前分支，用本地历史整体覆盖远端同源分支
  * @param    None
  * @retval   None
  * @note     危险操作：远端上本地没有的提交将被丢弃且无法恢复；
  *           需在弹窗输入大写 Y 确认，其他输入或取消均不执行
  ******************************************************************************
  */
static void OpForcePush() {
    if (!EnsureRepo() || NeedRemote() || NeedCommit() || NeedBranch()) return;
    std::wstring br = CurBranch();
    std::wstring note = L"强制推送会用本地 " + br + L" 历史整体覆盖远端：\n"
                        L"· 远端上本地没有的提交将被丢弃（无法恢复）\n"
                        L"· 若他人基于远端旧历史工作，之后推送会产生冲突\n\n"
                        L"确认强推请输入大写 Y（其他输入或留空=取消）：";
    auto y = AskText(g_main, L"强制推送（危险操作）", note);
    if (!y || *y != L"Y") return;
    OutSep(); OutCmd(L"强制推送: " + br);
    Exec({ G({ L"push", L"-u", g_remote, br, L"--force" }) });   // 显式推到勾选的默认远端（裸 git push --force 会走上游跟踪的远端）
}
static void OpInitRepo() {
    if (!EnsureRepo()) return;
    if (Trim(GitOut({ L"rev-parse", L"--is-inside-work-tree" })) == L"true") { Info(g_main, L"该目录已经是 git 仓库，无需初始化。"); return; }
    OutSep(); OutCmd(L"一键初始化: " + g_repo);
    std::vector<JobCmd> v; v.push_back({ { L"SPECIAL", L"init" }, false });
    ExecAsync(std::move(v));
}
// ============================ 配置远程（多远端管理弹窗） =====================
// 现有远端以勾选行显示（勾选=保留，去勾选=OK 时删除）；【＋添加远端】动态追加
// "本地名 + 链接" 新行，弹窗 Y 轴随内容自适应；界面软上限 10 个（git 本身无硬限制）
struct RemoteRow {
    std::wstring name, url;
    std::wstring name0, url0;                     // 现有远端打开时的原始别名/链接（🖊 编辑后比对，变了才生成 rename/set-url）
    bool existing = false, checked = true;
    bool del = false;                             // 现有远端被行内【✕】标记：OK 时执行 remote remove（去勾选≠删除）
    int cid = -1;                                 // 行创建序号（删除/重编辑按钮定位用，新旧行统一）
    HWND hChk = nullptr, hName = nullptr, hUrl = nullptr;
    HWND hDel = nullptr, hRe = nullptr;           // ✕ 删除（新旧行都有，行尾）/ 🖊 重编辑（新旧行都有，行尾）
    HWND hLbl = nullptr;                          // 现有远端行的 "名称 → 链接" 文本（点🖊 后销毁，换成链接编辑框）
};
struct RemoteCfgState {
    std::vector<RemoteRow> rows;
    HWND hDlg = nullptr, hAdd = nullptr, hOk = nullptr, hCancel = nullptr, hHint = nullptr;
    std::wstring defaultPick;             // OK 时勾选的那个远端 = 默认远端（只能勾一个）
    std::wstring initPick;                // 打开时的默认远端（事件解锁重建行时预勾用）
    int addY = 0, ch = 0, nextIdx = 0;
    bool done = false;
};
static const int REMOTE_UI_MAX = 10;   // 弹窗界面软上限（git 支持任意多个，超了用底部命令行）
// 弹窗句柄/状态指针/锁定态：供 WM_APP_JOBDONE 事件解锁用（远端操作完成时自动以解锁态重开弹窗）
static HWND g_hRemoteDlg = nullptr;
static RemoteCfgState* g_remoteDlgState = nullptr;
static bool g_remoteDlgLocked = false;
// 行/按钮/弹窗高度统一重排（增删行后调用）；✕/🖊 统一在行尾（避免靠勾选框太近误删）
static void RemoteRowsRelayout(HWND h, RemoteCfgState* st) {
    int mg = (int)(14 * g_scale), rowChkH = (int)(22 * g_scale), rowEditH = (int)(24 * g_scale);
    int gap = (int)(6 * g_scale), hintH = (int)(18 * g_scale), addH = (int)(26 * g_scale), btnH = (int)(28 * g_scale);
    int wd = (int)(520 * g_scale), nameW = (int)(90 * g_scale);
    int symW = (int)(26 * g_scale), symGap = (int)(4 * g_scale);   // ✕/🖊 符号按钮统一尺寸
    int xRe = wd - mg - 2 * (symW + symGap);                       // 🖊 位置（✕ 在其右）
    int contentRight = xRe - symGap;                               // 行内容区右界
    int yy = mg + hintH + (int)(4 * g_scale);
    for (auto& r : st->rows) {
        if (r.existing) {
            if (r.del) continue;                        // 已标记删除的行不占位
            int xText = mg + (int)(26 * g_scale);
            if (r.hChk) MoveWindow(r.hChk, mg, yy, (int)(22 * g_scale), rowChkH, TRUE);
            if (r.hRe) MoveWindow(r.hRe, xRe, yy, symW, rowEditH, TRUE);
            if (r.hDel) MoveWindow(r.hDel, wd - mg - symW, yy, symW, rowEditH, TRUE);
            if (r.hName) {                              // 🖊 编辑态：别名+链接 两框（与添加行同款布局）
                if (r.hName) MoveWindow(r.hName, xText, yy, nameW, rowEditH, TRUE);
                if (r.hUrl) MoveWindow(r.hUrl, xText + nameW + (int)(6 * g_scale), yy,
                    contentRight - xText - nameW - (int)(6 * g_scale), rowEditH, TRUE);
            } else if (r.hLbl) {                        // 常态：名称 → 链接 静态文本
                if (r.hLbl) MoveWindow(r.hLbl, xText, yy, contentRight - xText, rowChkH, TRUE);
            }
            yy += rowChkH + gap;
        } else {
            int xName = mg + (int)(26 * g_scale);       // 草稿行无勾选框，与现有行编辑态对齐
            int xUrl = xName + nameW + (int)(6 * g_scale);
            if (r.hName) MoveWindow(r.hName, xName, yy, nameW, rowEditH, TRUE);
            if (r.hUrl) MoveWindow(r.hUrl, xUrl, yy, contentRight - xUrl, rowEditH, TRUE);
            if (r.hRe) MoveWindow(r.hRe, xRe, yy, symW, rowEditH, TRUE);
            if (r.hDel) MoveWindow(r.hDel, wd - mg - symW, yy, symW, rowEditH, TRUE);
            yy += rowEditH + gap;
        }
    }
    st->addY = yy;
    if (st->hAdd) MoveWindow(st->hAdd, mg, st->addY, (int)(110 * g_scale), addH, TRUE);
    int by = st->addY + addH + (int)(12 * g_scale);
    if (st->hOk) MoveWindow(st->hOk, wd - 2 * (int)(80 * g_scale) - mg, by, (int)(80 * g_scale), btnH, TRUE);
    if (st->hCancel) MoveWindow(st->hCancel, wd - (int)(80 * g_scale) - mg, by, (int)(80 * g_scale), btnH, TRUE);
    int oldCh = st->ch;
    st->ch = st->addY + addH + (int)(12 * g_scale) + btnH + mg;
    RECT fr; GetWindowRect(h, &fr);
    SetWindowPos(h, nullptr, 0, 0, fr.right - fr.left, fr.bottom - fr.top + (st->ch - oldCh),
                 SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(h, nullptr, FALSE);
}
// 建现有远端行控件（首次创建与事件解锁刷新共用）；rows 数据须已就位，建完由 RemoteRowsRelayout 统一摆位
static void RemoteDlgBuildRows(HWND dlg, RemoteCfgState* st) {
    int mg = (int)(14 * g_scale), rowChkH = (int)(22 * g_scale), rowEditH = (int)(24 * g_scale);
    int w = (int)(520 * g_scale);
    int symW = (int)(26 * g_scale), symGap = (int)(4 * g_scale);
    int xRe = w - mg - 2 * (symW + symGap);
    bool preMatch = false;
    for (size_t i = 0; i < st->rows.size(); ++i) {
        RemoteRow& r = st->rows[i];
        std::wstring label = r.name + L" → " + (r.url.empty() ? L"（未取到链接）" : r.url);
        r.hChk = CreateWindowExW(0, L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            mg, (int)(100 * g_scale) + (int)i * rowChkH, (int)(22 * g_scale), rowChkH, dlg,
            (HMENU)(INT_PTR)(400 + (int)i), g_hInst, nullptr);
        r.hRe = CreateWindowExW(0, L"BUTTON", L"🖊", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            xRe, (int)(100 * g_scale) + (int)i * rowChkH, symW, rowEditH, dlg,
            (HMENU)(INT_PTR)(1400 + (int)i), g_hInst, nullptr);
        r.hDel = CreateWindowExW(0, L"BUTTON", L"✕", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            w - mg - symW, (int)(100 * g_scale) + (int)i * rowChkH, symW, rowEditH, dlg,
            (HMENU)(INT_PTR)(1300 + (int)i), g_hInst, nullptr);
        r.hLbl = CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE,
            mg + (int)(26 * g_scale), (int)(100 * g_scale) + (int)i * rowChkH,
            xRe - symGap - mg - (int)(26 * g_scale), rowChkH, dlg, nullptr, g_hInst, nullptr);
        SendMessageW(r.hChk, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SendMessageW(r.hDel, WM_SETFONT, (WPARAM)g_fontSym, TRUE);   // ✕/🖊 须用 Segoe UI Symbol（YaHei 无字形画空白）
        SendMessageW(r.hRe, WM_SETFONT, (WPARAM)g_fontSym, TRUE);
        SendMessageW(r.hLbl, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        bool pre = (r.name == st->initPick);
        if (pre) preMatch = true;
        SendMessageW(r.hChk, BM_SETCHECK, pre ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    // 不做"兜底勾第一个"：预勾只认显式选择（gittool.defaultremote），全不勾 = 纯本机模式要能持久显示
}
// 事件解锁（WM_APP_JOBDONE）：远端操作完成 → 原地解锁，重建现有远端行拿最新列表，无关闭重开闪烁
static void RemoteDlgUnlock(HWND h) {
    RemoteCfgState* st = g_remoteDlgState;
    if (!st || st->hDlg != h) return;
    g_remoteDlgLocked = false;
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);      // 重建过程关重绘，避免行删建闪烁
    for (auto& r : st->rows)
        for (HWND hc : { r.hChk, r.hName, r.hUrl, r.hDel, r.hRe, r.hLbl })
            if (hc) DestroyWindow(hc);
    st->rows.clear();
    st->nextIdx = 0;
    for (auto& pr : RemotesInOrder()) {                   // 重读远端列表（刚完成的操作可能改过它），保持新增顺序
        RemoteRow rr;
        rr.name = pr.first;
        rr.url = pr.second;
        rr.name0 = pr.first; rr.url0 = pr.second;
        rr.existing = true; rr.checked = true;
        rr.cid = (int)st->rows.size();
        st->rows.push_back(std::move(rr));
    }
    st->nextIdx = (int)st->rows.size();
    st->initPick = Trim(GitOut({ L"config", L"--get", L"gittool.defaultremote" }));   // 只认显式选择：DetectRemote 的上游/首远端兜底会造成"全不勾仍预勾"的假象
    RemoteDlgBuildRows(h, st);
    for (auto& r : st->rows)                      // 全部控件恢复可用
        for (HWND hc : { r.hChk, r.hName, r.hUrl, r.hDel, r.hRe, r.hLbl })
            if (hc) EnableWindow(hc, TRUE);
    if (st->hAdd) EnableWindow(st->hAdd, TRUE);
    if (st->hOk) EnableWindow(st->hOk, TRUE);
    if (st->hHint) SetWindowTextW(st->hHint,
        L"勾选默认远端：");
    RemoteRowsRelayout(h, st);                    // 行数可能变化：重摆位并同步弹窗高度
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, nullptr, TRUE);
    SetFocus(st->rows.empty() ? st->hAdd : st->rows[0].hChk);
}
// 结束现有行编辑：读入别名/链接（空值回退原值），销毁编辑框、行退回静态文本显示
static void RemoteRowEndEdit(HWND h, RemoteCfgState* st, RemoteRow& r) {
    auto readEdit = [&](HWND he, std::wstring& out) {
        if (!he) return;
        int len = GetWindowTextLengthW(he);
        std::wstring s(len, L'\0');
        GetWindowTextW(he, &s[0], len + 1);
        out = Trim(s);
    };
    readEdit(r.hName, r.name);
    readEdit(r.hUrl, r.url);
    if (r.name.empty()) r.name = r.name0;   // 空别名回退原值，防产生空名命令
    if (r.url.empty()) r.url = r.url0;
    for (HWND hc : { r.hName, r.hUrl }) if (hc) DestroyWindow(hc);
    r.hName = r.hUrl = nullptr;
    r.hLbl = CreateWindowExW(0, L"STATIC", (r.name + L" → " + r.url).c_str(), WS_CHILD | WS_VISIBLE,
        0, 0, (int)(10 * g_scale), (int)(10 * g_scale), h, nullptr, g_hInst, nullptr);
    SendMessageW(r.hLbl, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    RemoteRowsRelayout(h, st);              // 统一摆位（静态文本宽度按行重算）
}
static LRESULT CALLBACK RemoteCfgDlgSub(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    RemoteCfgState* st = (RemoteCfgState*)data;
    int mg = (int)(14 * g_scale), rowChkH = (int)(22 * g_scale), rowEditH = (int)(24 * g_scale);
    int gap = (int)(6 * g_scale), addH = (int)(26 * g_scale), btnH = (int)(28 * g_scale);
    int wd = (int)(520 * g_scale), nameW = (int)(90 * g_scale);
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (code == EN_KILLFOCUS) {                      // 现有行编辑框失焦：延迟确认焦点真的离开本行 → 自动提交退回静态文本
            int cid = -1;
            if (id >= 1100 && id < 1200) cid = id - 1100;
            if (id >= 1200 && id < 1300) cid = id - 1200;
            for (auto& r : st->rows)
                if (r.cid == cid && r.existing && (r.hName || r.hUrl)) { SetTimer(h, 100 + cid, 80, nullptr); break; }
            return 0;
        }
        if (id == 290 && code == BN_CLICKED) {           // ＋添加远端：追加一行（默认不勾选=暂不加入），弹窗加高
            int visible = 0;                             // 上限只数实际占位的行：✕ 标记删除的行不可见，不占计数
            for (auto& r : st->rows) if (!(r.existing && r.del)) ++visible;
            if (visible >= REMOTE_UI_MAX) {
                Warn(h, Fmt(L"界面最多管理 %d 个远端（git 本身无限制，更多可用底部命令行）。", REMOTE_UI_MAX));
                return 0;
            }
            int n = st->nextIdx++;
            RemoteRow r;
            r.cid = n;
            int symW = (int)(26 * g_scale), symGap = (int)(4 * g_scale);
            int xRe = wd - mg - 2 * (symW + symGap);     // 🖊（✕ 在其右），内容区让到它们左侧，防勾选时误触删除
            int xName = mg + (int)(26 * g_scale);
            int xUrl = xName + nameW + (int)(6 * g_scale);
            r.hChk = CreateWindowExW(0, L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                mg, st->addY, (int)(22 * g_scale), rowEditH, h, (HMENU)(INT_PTR)(400 + n), g_hInst, nullptr);
            r.hName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                xName, st->addY, nameW, rowEditH, h, (HMENU)(INT_PTR)(1100 + n), g_hInst, nullptr);
            r.hUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                xUrl, st->addY, xRe - symGap - xUrl, rowEditH, h, (HMENU)(INT_PTR)(1200 + n), g_hInst, nullptr);
            r.hRe = CreateWindowExW(0, L"BUTTON", L"🖊", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                xRe, st->addY, symW, rowEditH, h, (HMENU)(INT_PTR)(1400 + n), g_hInst, nullptr);
            r.hDel = CreateWindowExW(0, L"BUTTON", L"✕", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                wd - mg - symW, st->addY, symW, rowEditH, h, (HMENU)(INT_PTR)(1300 + n), g_hInst, nullptr);
            SendMessageW(r.hChk, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
            SendMessageW(r.hName, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
            SendMessageW(r.hUrl, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
            SendMessageW(r.hDel, WM_SETFONT, (WPARAM)g_fontSym, TRUE);   // ✕/🖊 须用 Segoe UI Symbol（YaHei 无字形画空白）
            SendMessageW(r.hRe, WM_SETFONT, (WPARAM)g_fontSym, TRUE);
            st->rows.push_back(std::move(r));
            RemoteRowsRelayout(h, st);
            return 0;
        }
        if (id >= 1300 && id < 1300 + (int)st->rows.size() + 20 && code == BN_CLICKED) {   // 删除该行（新旧行统一按 cid 定位）
            int cid = id - 1300;
            for (auto it = st->rows.begin(); it != st->rows.end(); ++it) {
                if (it->cid == cid) {
                    for (HWND hc : { it->hChk, it->hName, it->hUrl, it->hDel, it->hRe, it->hLbl })
                        if (hc) DestroyWindow(hc);
                    if (it->existing) it->del = true;    // 现有远端：标记删除，OK 时执行 remote remove（去勾选≠删除）
                    else st->rows.erase(it);             // 新增草稿行：直接移除
                    RemoteRowsRelayout(h, st);
                    return 0;
                }
            }
            return 0;
        }
        if (id >= 1400 && id < 1400 + (int)st->rows.size() + 20 && code == BN_CLICKED) {   // 🖊 重编辑
            int cid = id - 1400;
            for (auto& r : st->rows) {
                if (r.cid != cid) continue;
                if (!r.existing) {                      // 新增草稿行：清空重填
                    if (r.hName) { SetWindowTextW(r.hName, L""); SetFocus(r.hName); }
                    if (r.hUrl) SetWindowTextW(r.hUrl, L"");
                } else if (r.hLbl && !r.hName) {        // 现有远端：静态文本换成 别名+链接 两个编辑框（预填原值）
                    DestroyWindow(r.hLbl);
                    r.hLbl = nullptr;
                    r.hName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", r.name.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                        (int)(90 * g_scale), (int)(24 * g_scale), h, (HMENU)(INT_PTR)(1100 + r.cid), g_hInst, nullptr);
                    r.hUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", r.url.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                        (int)(300 * g_scale), (int)(24 * g_scale), h, (HMENU)(INT_PTR)(1200 + r.cid), g_hInst, nullptr);
                    SendMessageW(r.hName, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
                    SendMessageW(r.hUrl, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
                    RemoteRowsRelayout(h, st);          // 统一摆位（与添加行同款布局）
                    SetFocus(r.hName);
                }
                return 0;
            }
            return 0;
        }
        if (id >= 400 && id < 400 + (int)st->rows.size() + 20 && code == BN_CLICKED) {   // 勾选默认远端：现有行/草稿行统一单选互斥——勾谁谁是默认
            int cid = id - 400;
            for (auto& r : st->rows) {
                if (r.cid == cid && r.hChk && SendMessageW(r.hChk, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    for (auto& o : st->rows)
                        if (&o != &r && o.hChk) SendMessageW(o.hChk, BM_SETCHECK, BST_UNCHECKED, 0);
                    SetFocus(h);   // 焦点还给弹窗：空文本勾选框聚焦会画虚线框（之前红箭头指的那个）
                    break;
                }
            }
            return 0;
        }
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK) {
                // 读取勾选与新行内容；勾选的现有远端 = 默认远端（行勾选即单选）
                // 去勾选≠删除：远端保留，仅不作为默认；删除只认行内【删除】按钮标记的 del
                for (auto& r : st->rows) {
                    if (r.hChk) r.checked = SendMessageW(r.hChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    auto readEdit = [&](HWND he, std::wstring& out) {
                        if (!he) return;
                        int len = GetWindowTextLengthW(he);
                        std::wstring s(len, L'\0');
                        GetWindowTextW(he, &s[0], len + 1);
                        out = Trim(s);
                    };
                    readEdit(r.hName, r.name);
                    readEdit(r.hUrl, r.url);
                    if (r.checked && !r.del) st->defaultPick = r.name;   // 勾选的行（现有/新增均可）= 默认远端
                }
                // 校验：别名非空且不重名（现有行改名/新行）；新行还须链接非空
                bool bad = false;
                for (auto& r : st->rows) {
                    if (r.del) continue;
                    const std::wstring& effName = r.existing ? r.name0 : std::wstring();   // 该行占用的旧名
                    if (r.existing) {
                        if (r.name.empty()) { Warn(h, Fmt(L"远端 [%s] 的本地别名不能为空。", r.name0.c_str())); bad = true; break; }
                        if (r.name != r.name0) {   // 改了名：与其它行占用的名字查重
                            for (auto& o : st->rows) {
                                if (&o == &r || o.del) continue;
                                const std::wstring& on = o.existing ? o.name0 : o.name;
                                if (!on.empty() && on == r.name) {
                                    Warn(h, Fmt(L"本地别名 [%s] 与其它远端重名。", r.name.c_str()));
                                    bad = true; break;
                                }
                            }
                            if (bad) break;
                        }
                    } else {
                        if (r.name.empty() && r.url.empty()) {
                            if (r.checked) { Warn(h, L"勾选为默认远端的行需要填写 本地名 和 链接。"); bad = true; break; }
                            continue;   // 空草稿行：忽略不生成命令
                        }
                        if (r.name.empty() || r.url.empty()) { Warn(h, L"新增远端需要填写 本地名 和 链接（或点该行 ✕ 移除）。"); bad = true; break; }
                        if (r.name == st->defaultPick) { Warn(h, Fmt(L"本地名 [%s] 与保留的默认远端重名，请换一个。", r.name.c_str())); bad = true; break; }
                    }
                }
                if (bad) return 0;   // 校验不过：弹窗保持打开
            }
            st->done = true;
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        // 点弹窗空白处：静态/背景不抢焦点，编辑框收不到 KILLFOCUS——这里强制提交所有编辑中的行
        bool committed = false;
        for (auto& r : st->rows)
            if (r.existing && (r.hName || r.hUrl)) { RemoteRowEndEdit(h, st, r); committed = true; }
        if (committed) InvalidateRect(h, nullptr, TRUE);
        break;   // 交默认处理
    }
    case WM_TIMER: {
        // 失焦延迟确认：别名框→链接框切换也会触发 KILLFOCUS，定时器到点时检查焦点
        // 真的离开本行两框才提交；若焦点在另一框（仍在编辑）则不动作
        for (auto& r : st->rows) {
            if ((int)w == 100 + r.cid && r.existing && (r.hName || r.hUrl)) {
                KillTimer(h, w);
                HWND f = GetFocus();
                if (f != r.hName && f != r.hUrl) RemoteRowEndEdit(h, st, r);
                return 0;
            }
        }
        return 0;
    }
    case WM_DESTROY:
        if (g_hRemoteDlg == h) { g_hRemoteDlg = nullptr; g_remoteDlgState = nullptr; }
        return 0;
    case WM_CLOSE:
        st->done = true;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, msg, w, l);
}
// 配置远程弹窗：勾选行=默认远端(单选，勾选自动互斥)+保留，去勾选=删除；【＋添加远端】动态行；
// locked=true（有远端操作进行中）时全部内容置灰只读，仅 Cancel 可关；返回 rows 供调用方统一生成命令
static std::vector<RemoteRow> AskRemoteCfg(HWND parent, std::wstring& defaultPick, bool locked) {
    int mg = (int)(14 * g_scale), rowChkH = (int)(22 * g_scale), gap = (int)(6 * g_scale);
    int hintH = (int)(18 * g_scale), rowEditH = (int)(24 * g_scale);
    int addH = (int)(26 * g_scale), btnH = (int)(28 * g_scale);
    int w = (int)(520 * g_scale);
    RemoteCfgState st;
    for (auto& pr : RemotesInOrder()) {                   // 配置文件出现顺序（新增顺序），非字母序
        RemoteRow rr;
        rr.name = pr.first; rr.url = pr.second;
        rr.name0 = pr.first; rr.url0 = pr.second;         // 原始别名/链接：🖊 编辑后与当前值比对，变了才生成 rename/set-url
        rr.existing = true; rr.checked = true;
        rr.cid = (int)st.rows.size();               // 现有行 cid = 建行顺序（弹窗期间行不删除只标记，id 稳定）
        st.rows.push_back(std::move(rr));
    }
    st.nextIdx = (int)st.rows.size();               // 新增行 cid 接在现有行之后，1300+cid 删除按钮 id 不冲突
    int nExist = (int)st.rows.size();
    st.addY = mg + hintH + (int)(4 * g_scale) + nExist * (rowChkH + gap);
    st.ch = st.addY + addH + (int)(12 * g_scale) + btnH + mg;
    RECT rcFrame = { 0, 0, w, st.ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rcFrame.right - rcFrame.left, wh = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"配置远程",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, ww, wh, parent, nullptr, g_hInst, nullptr);
    st.hDlg = dlg;
    SetWindowSubclass(dlg, RemoteCfgDlgSub, 1, (DWORD_PTR)&st);
    g_hRemoteDlg = dlg; g_remoteDlgState = &st; g_remoteDlgLocked = locked;   // JOBDONE 事件解锁用
    HWND hHint = CreateWindowExW(0, L"STATIC",
        locked ? L"有远端操作进行中，完成后才能配置远端（当前内容只读）："
               : L"勾选默认远端：",
        WS_CHILD | WS_VISIBLE, mg, mg, w - 2 * mg, hintH, dlg, nullptr, g_hInst, nullptr);
    SendMessageW(hHint, WM_SETFONT, (WPARAM)g_fontMenu, TRUE);
    st.hHint = hHint;   // 事件解锁时恢复提示文字用
    st.initPick = defaultPick;
    RemoteDlgBuildRows(dlg, &st);
    RemoteRowsRelayout(dlg, &st);   // 统一摆位（含 ＋/OK/Cancel 与弹窗高度）
    st.hAdd = CreateWindowExW(0, L"BUTTON", L"＋ 添加远端", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        mg, st.addY, (int)(110 * g_scale), addH, dlg, (HMENU)290, g_hInst, nullptr);
    int by = st.addY + addH + (int)(12 * g_scale);
    st.hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        w - 2 * (int)(80 * g_scale) - mg, by, (int)(80 * g_scale), btnH, dlg, (HMENU)IDOK, g_hInst, nullptr);
    st.hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        w - (int)(80 * g_scale) - mg, by, (int)(80 * g_scale), btnH, dlg, (HMENU)IDCANCEL, g_hInst, nullptr);
    for (HWND c : { st.hAdd, st.hOk, st.hCancel })
        SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    if (locked) {   // 有远端操作进行中：全部内容置灰不可选，仅 Cancel 可关闭弹窗
        for (auto& r : st.rows)
            for (HWND hc : { r.hChk, r.hName, r.hUrl, r.hDel, r.hRe, r.hLbl })
                if (hc) EnableWindow(hc, FALSE);
        EnableWindow(st.hAdd, FALSE);
        EnableWindow(st.hOk, FALSE);
    }
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(locked ? st.hCancel : (nExist ? st.rows[0].hChk : st.hAdd));
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    defaultPick = st.defaultPick;   // 勾选的那个远端 = 默认远端（校验已保证有且只有一个）
    return st.rows;
}
// 配置远程：OK 后统一生成命令——行内【删除】标记的现有远端 remote remove，勾选的新行 remote add；默认远端变更写仓库级配置
static void OpRemoteCfg() {
    // 只认显式选择：若传 g_remote（带上游/首远端兜底），全不勾后重开仍会被兜底值预勾
    std::wstring defaultPick = Trim(GitOut({ L"config", L"--get", L"gittool.defaultremote" }));
    auto rows = AskRemoteCfg(g_main, defaultPick, g_busy);   // g_busy=远端操作进行中：弹窗只读，OK 禁用必然走 Cancel 无命令
    std::vector<JobCmd> cmds;
    for (auto& r : rows) {
        if (r.existing && r.del) {
            cmds.push_back({ G({ L"remote", L"remove", r.name0 }), false });   // 仅显式 ✕ 才删；去勾选只是取消默认（按原始名删除）
            continue;
        }
        if (r.existing && !r.name.empty() && r.name != r.name0)
            cmds.push_back({ G({ L"remote", L"rename", r.name0, r.name }), false });   // 🖊 改过别名：先改名（后续 set-url 用新名）
        if (r.existing && r.hUrl && !r.url.empty() && r.url != r.url0)
            cmds.push_back({ G({ L"remote", L"set-url", r.name, r.url }), false });    // 🖊 改过链接才生成 set-url（rename 已先执行）
        if (!r.existing && !r.name.empty() && !r.url.empty())
            cmds.push_back({ G({ L"remote", L"add", r.name, r.url }), false });        // 草稿行：填了就加入（✕ 移除的不算）
    }
    if (!defaultPick.empty() && defaultPick != g_remote) {
        cmds.push_back({ G({ L"config", L"--local", L"gittool.defaultremote", defaultPick }), false });
    } else if (defaultPick.empty()
               && !Trim(GitOut({ L"config", L"--get", L"gittool.defaultremote" })).empty()) {
        // 全不勾 = 纯本机模式：清掉残留的默认远端配置，否则重开弹窗仍显示上次勾选（确有残留才发，避免无用报错）
        cmds.push_back({ G({ L"config", L"--local", L"--unset", L"gittool.defaultremote" }), true });   // 本就未设置时报错属正常，不阻断
    }
    if (cmds.empty()) return;
    ExecAsync(std::move(cmds));
    RefreshStatusAsync();   // 默认远端可能变化（被删/新增/手动选择），立即刷新
}
static void OpTagNew() {
    if (!EnsureRepo()) return;
    auto name = AskText(g_main, L"打标签", L"标签名（如 v1.0）:");
    if (!name || name->empty()) return;
    auto msg = AskText(g_main, L"打标签", L"标签说明:");
    if (!msg) return;
    Exec({ G({ L"tag", L"-a", *name, L"-m", *msg }) });
}
static void OpTagDel() {
    if (!EnsureRepo()) return;
    auto name = AskText(g_main, L"删除标签", L"要删除的标签名:");
    if (!name || name->empty()) return;
    if (!Confirm(g_main, Fmt(L"确认删除【本地】标签 [%s]？\n（远端不受影响；要删远端用【推送(删除tag)】）", name->c_str()))) return;
    Exec({ G({ L"tag", L"-d", *name }) });
}
// 推送(删除tag)：删除【远端】标签（推送空引用 :refs/tags/<名>），本地标签不受影响
static void OpTagRemoteDel() {
    if (!EnsureRepo() || NeedRemote()) return;
    auto name = AskText(g_main, L"推送(删除tag)", L"要删除的【远端】标签名:");
    if (!name || name->empty()) return;
    if (!Confirm(g_main, Fmt(L"确认删除远端（%s）上的标签 [%s]？\n本地标签不受影响。", g_remote.c_str(), name->c_str()))) return;
    Exec({ G({ L"push", g_remote, L":refs/tags/" + *name }) });
}
static void OpBranchSw() {
    if (!EnsureRepo()) return;
    auto b = AskText(g_main, L"切换分支", L"要切换到的分支名:");
    if (!b || b->empty()) return;
    Exec({ G({ L"switch", *b }) });
}
static void OpBranchNew() {
    if (!EnsureRepo()) return;
    auto b = AskText(g_main, L"新建分支并切换", L"新分支名（基于当前分支创建）:");
    if (!b || b->empty()) return;
    Exec({ G({ L"switch", L"-c", *b }) });
}
static void OpBranchMerge() {
    if (!EnsureRepo()) return;
    auto b = AskText(g_main, L"合并分支", L"要合并进当前分支的分支名:");
    if (!b || b->empty()) return;
    if (!Confirm(g_main, Fmt(L"确认把 [%s] 合并进当前分支？\n出现冲突时需要手动解决后提交。", b->c_str()))) return;
    Exec({ G({ L"merge", L"--no-edit", *b }) });
}
static void OpBranchDel() {
    if (!EnsureRepo()) return;
    auto b = AskText(g_main, L"删除已合并分支", L"要删除的【已合并】分支名:");
    if (!b || b->empty()) return;
    if (!Confirm(g_main, Fmt(L"确认删除分支 [%s]？（仅限已合并分支）", b->c_str()))) return;
    Exec({ G({ L"branch", L"-d", *b }) });
}
static void OpCheckoutRef() {
    if (!EnsureRepo()) return;
    auto r = AskText(g_main, L"跳转到标签/提交", L"要跳转到的标签或提交号:");
    if (!r || r->empty()) return;
    OutTxt(L"[提示] 查看模式，完成后请用\"回到 main/master 最新\"返回。");
    Exec({ G({ L"checkout", *r }) });
}
static void OpRestoreStaged() {
    if (!EnsureRepo()) return;
    auto f = AskText(g_main, L"撤销已暂存", L"要撤销暂存的文件名（留空=全部）:");
    if (!f) return;
    if (f->empty()) Exec({ G({ L"restore", L"--staged", L"." }) });
    else Exec({ G({ L"restore", L"--staged", *f }) });
}
static void OpRestoreFile() {
    if (!EnsureRepo()) return;
    auto f = AskText(g_main, L"丢弃单个文件改动[危险]", L"要丢弃改动的文件名（在查看状态里复制）:");
    if (!f || f->empty()) return;
    if (!Confirm(g_main, Fmt(L"确认丢弃 [%s] 的未提交改动？不可恢复！", f->c_str()))) return;
    Exec({ G({ L"restore", L"--", *f }) });
}
static void OpDiscardAll() {
    if (!EnsureRepo()) return;
    if (!Confirm(g_main, L"确认丢弃工作区【全部】未提交改动？\n不可恢复！确定都不要了？")) return;
    Exec({ G({ L"checkout", L"--", L"." }) });
}
static void OpUndoLast() {
    if (!EnsureRepo()) return;
    if (!Confirm(g_main, L"撤销最后一次 commit，改动保留在工作区/暂存区。\n（若已 push 过，之后需要强推，公共分支慎用）\n\n继续？")) return;
    Exec({ G({ L"reset", L"--soft", L"HEAD~1" }) });
}
static void OpAmend() {
    if (!EnsureRepo()) return;
    if (!Confirm(g_main, L"将修改最后一次提交。\n注意：已 push 到公共分支时禁止使用！\n\n继续？")) return;
    std::vector<std::vector<std::wstring>> cmds;
    if (Confirm(g_main, L"是否把当前工作区改动一并并入该提交？"))
        cmds.push_back(G({ L"add", L"-A" }));
    auto msg = AskText(g_main, L"修改最后一次提交", L"新的提交消息（留空=沿用原消息）:");
    if (!msg) return;
    if (msg->empty()) cmds.push_back(G({ L"commit", L"--amend", L"--no-edit" }));
    else cmds.push_back(G({ L"commit", L"--amend", L"-m", *msg }));
    ExecV(cmds);
}
static void OpRevert() {
    if (!EnsureRepo()) return;
    auto sha = AskText(g_main, L"反向提交 revert", L"要抵消的提交号（如 afe7655）:");
    if (!sha || sha->empty()) return;
    Exec({ G({ L"revert", L"--no-edit", *sha }) });
}

// ============================ 提交选择弹窗（LISTBOX 点选） ===================
// 弹窗状态：列表句柄 + 选中下标 + 选中行全文（OK 时有效，取消保持空）
struct PickCommitState {
    HWND hList = nullptr;
    int sel = -1;
    std::wstring picked;
    bool done = false, ok = false;
};
static LRESULT CALLBACK PickCommitDlgSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    PickCommitState* st = (PickCommitState*)data;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        // 选中变化即记录（SELCHANGE 时新行已是当前项），不依赖确定瞬间再读
        if (id == 200 && code == LBN_SELCHANGE) {
            st->sel = (int)SendMessageW(st->hList, LB_GETCURSEL, 0, 0);
            return 0;
        }
        // 确定按钮 或 双击列表条目
        if (id == IDOK || (id == 200 && code == LBN_DBLCLK)) {
            int sel = (int)SendMessageW(st->hList, LB_GETCURSEL, 0, 0);
            if (sel < 0 && st->sel >= 0) sel = st->sel;   // 兜底：确定瞬间读不到就用最近一次选中
            if (sel < 0) { MessageBoxW(h, L"请先在列表中点选一条提交。", L"提示", MB_OK | MB_ICONINFORMATION); return 0; }
            st->sel = sel;
            // 选中行全文（SHA 内嵌在行首，直接从文本解析，不依赖索引映射）
            int len = (int)SendMessageW(st->hList, LB_GETTEXTLEN, (WPARAM)sel, 0);
            if (len < 0) len = 0;
            std::wstring txt(len, L'\0');
            if (len > 0) SendMessageW(st->hList, LB_GETTEXT, (WPARAM)sel, (LPARAM)txt.data());
            st->picked = txt;
            st->ok = true;
            st->done = true;
            DestroyWindow(h);
            return 0;
        }
        if (id == IDCANCEL) {
            st->done = true;
            DestroyWindow(h);
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        st->done = true;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
/**
  ******************************************************************************
  * @brief    提交选择弹窗：LISTBOX 点选一条提交
  * @param    parent 父窗口（主窗口，模态期间被禁用）
  * @param    title 弹窗标题
  * @param    items 条目全文列表（完整 SHA 内嵌在行首，与显示文本一体）
  * @retval   选中行的完整条目文本（SHA 从文本解析，不依赖索引）；取消返回 nullopt
  * @note     不设默认选中，必须显式点选；双击条目等同确定
  ******************************************************************************
  */
static std::optional<std::wstring> AskPickCommit(HWND parent, const wchar_t* title,
                                                 const std::vector<std::wstring>& items) {
    int m = (int)(14 * g_scale), bw = (int)(80 * g_scale);
    int lblH = (int)(24 * g_scale);
    int listH = (int)(360 * g_scale);
    int btnH = (int)(28 * g_scale);
    int w = (int)(620 * g_scale);
    int ch = m + lblH + (int)(6 * g_scale) + listH + (int)(16 * g_scale) + btnH + m;
    RECT rcFrame = { 0, 0, w, ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rcFrame.right - rcFrame.left, wh = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    PickCommitState state;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, ww, wh, parent, nullptr, g_hInst, nullptr);
    SetWindowSubclass(dlg, PickCommitDlgSub, 1, (DWORD_PTR)&state);
    HWND hLabel = CreateWindowExW(0, L"STATIC", L"点选要回退到的提交（它之后的所有提交将被删除），双击条目等同确定：", WS_CHILD | WS_VISIBLE,
        m, m, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    SendMessageW(hLabel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
        m, m + lblH + (int)(6 * g_scale), w - 2 * m, listH, dlg, (HMENU)(INT_PTR)200, g_hInst, nullptr);
    SendMessageW(hList, WM_SETFONT, (WPARAM)g_fontMono, TRUE);
    for (auto& s : items) SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)s.c_str());
    // 水平滚动范围按最长条目实测宽度设置（不设 extent 则 WS_HSCROLL 不生效）
    {
        HDC hdc = GetDC(hList);
        HFONT hOld = (HFONT)SelectObject(hdc, g_fontMono);
        int maxW = 0; SIZE sz;
        for (auto& s : items) { GetTextExtentPoint32W(hdc, s.c_str(), (int)s.size(), &sz); if (sz.cx > maxW) maxW = sz.cx; }
        SelectObject(hdc, hOld);
        ReleaseDC(hList, hdc);
        SendMessageW(hList, LB_SETHORIZONTALEXTENT, (WPARAM)(maxW + 16 * g_scale), 0);
    }
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        w - 2 * bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDOK, g_hInst, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        w - bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDCANCEL, g_hInst, nullptr);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(hList);
    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    if (!state.ok || state.picked.empty()) return std::nullopt;
    return state.picked;
}
/**
  ******************************************************************************
  * @brief    删除提交（回退式）：reset --hard 到列表点选的提交，其后所有提交一并删除
  * @param    None
  * @retval   None
  * @note     危险操作：将删提交逐条列出并需输入大写 Y 确认；被删提交已推送远端时
  *           可顺手以 --force-with-lease 强推同步；误删可用 git reflog 找回提交号
  ******************************************************************************
  */
static void OpDeleteCommit() {
    if (!EnsureRepo() || NeedCommit()) return;
    if (g_busy) { Info(g_main, L"有操作正在运行，请等它完成后再删除提交。"); return; }
    // 取最近 30 条候选：短sha(10位)内嵌条目行首 + 日期 + 说明（确定时从选中行文本解析）
    std::vector<std::wstring> items;
    for (auto& ln : SplitLines(GitOut({ L"-c", L"i18n.logoutputencoding=utf-8", L"log", L"-n", L"30",
                                        L"--pretty=format:%H|%ad %s", L"--date=format:%Y-%m-%d %H:%M" }, 30000))) {
        std::wstring t = Trim(ln);
        if (t.size() < 41 || t[40] != L'|') continue;
        items.push_back(t.substr(0, 10) + L"  " + t.substr(41));
    }
    if (items.empty()) { Info(g_main, L"读不到提交历史，请先点【查看状态】检查仓库。"); return; }
    auto picked = AskPickCommit(g_main, L"删除提交（回退）", items);
    if (!picked) return;
    // 从选中行文本行首解析 10 位短哈希（不依赖列表索引，免疫任何显示顺序问题），再还原完整 SHA
    std::wstring abbr = picked->substr(0, 10);
    bool hexOk = (abbr.size() == 10);
    for (wchar_t c : abbr) if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) hexOk = false;
    if (!hexOk) { Warn(g_main, L"选中行解析提交号失败，请重试。"); return; }
    std::wstring sha = Trim(GitOut({ L"rev-parse", abbr }));
    if (sha.size() != 40) { Warn(g_main, Fmt(L"提交号 %s 解析失败（不存在或有歧义）。", abbr.c_str())); return; }
    OutSep(); OutTxt(L"删除提交: 选中 " + sha.substr(0, 10));
    // 计数校验：0=选中的就是最新提交；空=命令异常（与"无内容"区分开，避免误判）
    std::wstring cnt = Trim(GitOut({ L"rev-list", L"--count", sha + L"..HEAD" }));
    if (cnt.empty()) {
        Warn(g_main, Fmt(L"提交计数读取异常（rev-list 无输出）。\n选中提交: %s\n请点【查看状态】确认仓库正常后重试。", sha.substr(0, 10).c_str()));
        return;
    }
    if (cnt == L"0") { Info(g_main, Fmt(L"选中的 [%s] 就是最新提交，它之后没有可删的内容。\n撤销最新提交（保留改动）请用上一个按钮。", sha.substr(0, 10).c_str())); return; }
    // 工作区脏检查：reset --hard 会连未提交改动一起丢
    int dirty = 0;
    for (auto& l : SplitLines(GitOut({ L"status", L"--porcelain" }))) if (!Trim(l).empty()) ++dirty;
    if (dirty > 0 && !Confirm(g_main, Fmt(L"工作区还有 %d 处未提交改动，回退会把它们一并丢弃。\n如需保留请先 stash 收起或提交。继续？", dirty))) return;
    // 危险确认：逐条列出将删除的提交（最多列 10 条，超出只给计数）
    std::wstring note = L"【危险】将回退到所选提交，以下 " + cnt + L" 条提交及其文件内容将被删除：\n";
    int listed = 0;
    for (auto& l : SplitLines(GitOut({ L"-c", L"i18n.logoutputencoding=utf-8", L"log", sha + L"..HEAD", L"--pretty=format:%h %s" }, 30000))) {
        std::wstring t = Trim(l);
        if (t.empty()) continue;
        if (++listed <= 10) note += L"· " + t + L"\n";
    }
    if (listed > 10) note += Fmt(L"· ……（共 %s 条，仅列出前 10 条）\n", cnt.c_str());
    note += L"· 未推送过的提交删了就没了（急用 git reflog 可找回）\n"
            L"· 已推送过远端的，删除后必须强制推送才能同步远端\n\n"
            L"确认删除请输入大写 Y（其他输入或留空=取消）：";
    auto y = AskText(g_main, L"删除提交（危险操作）", note);
    if (!y || *y != L"Y") return;
    std::wstring br = CurBranch();
    // 强推判定：勾选的默认远端分支仍指向将被删除的提交（裸 push --force-with-lease 走上游跟踪，勾选会失效）
    std::vector<std::vector<std::wstring>> cmds;
    cmds.push_back(G({ L"reset", L"--hard", sha }));
    bool doPush = false;
    if (!g_remote.empty()) {
        std::wstring rcnt = Trim(GitOut({ L"rev-list", L"--count", sha + L".." + g_remote + L"/" + br }));
        if (!rcnt.empty() && rcnt != L"0"
            && Confirm(g_main, L"被删的提交已推送过远端，远端仍指向旧历史。\n是否删除后立即强制推送远端？\n（--force-with-lease：远端有别人新提交时会被拒绝，不误覆盖）")) {
            cmds.push_back(G({ L"push", L"--force-with-lease", g_remote, br }));
            doPush = true;
        }
    }
    OutSep(); OutCmd(L"删除提交: 回退到 " + sha.substr(0, 10));
    ExecV(std::move(cmds));
    OutTxt(L"[提示] 若删错了，可在底部命令行输入 git reflog 查看旧提交号找回。");
    if (doPush) OutTxt(L"[提示] 若上方强推被拒（rejected/stale info），说明远端有本地没有的新提交：先点【拉取】核对，确认无误后可改用【强制推送】。");
}
static void OpConflictList() {
    Exec({ G({ L"diff", L"--name-only", L"--diff-filter=U" }) });
}
static void OpNotepad() {
    if (!EnsureRepo()) return;
    auto f = AskText(g_main, L"用记事本编辑冲突文件", L"要用记事本打开的冲突文件名（可先点\"查看冲突文件列表\"复制）:");
    if (!f || f->empty()) return;
    SHELLEXECUTEINFOW se = { sizeof(se) };
    se.lpFile = L"notepad.exe"; se.lpParameters = f->c_str();
    se.lpDirectory = g_repo.c_str(); se.nShow = SW_SHOW;
    if (!ShellExecuteExW(&se)) { Warn(g_main, L"打开失败。"); return; }
    OutTxt(L"已在记事本打开: " + *f);
    OutTxt(L"把 <<<<<<< ======= >>>>>>> 标记段改成最终内容并保存，然后用【标记文件已解决】登记；全部解决后用【提交修改】完成合并。");
}
static void OpMarkResolved() {
    if (!EnsureRepo()) return;
    auto f = AskText(g_main, L"标记文件已解决", L"已解决完毕的文件名（留空=标记全部）:");
    if (!f) return;
    if (f->empty()) Exec({ G({ L"add", L"-A" }), G({ L"status", L"--short" }) });
    else Exec({ G({ L"add", L"--", *f }), G({ L"status", L"--short" }) });
}
static void OpTakeOurs() {
    if (!EnsureRepo()) return;
    auto f = AskText(g_main, L"采用我方版本(当前分支)", L"要采用【我方（当前分支）】版本的文件名:");
    if (!f || f->empty()) return;
    Exec({ G({ L"checkout", L"--ours", L"--", *f }), G({ L"add", L"--", *f }) });
}
static void OpTakeTheirs() {
    if (!EnsureRepo()) return;
    auto f = AskText(g_main, L"采用对方版本(被合并分支)", L"要采用【对方（被合并分支）】版本的文件名:");
    if (!f || f->empty()) return;
    Exec({ G({ L"checkout", L"--theirs", L"--", *f }), G({ L"add", L"--", *f }) });
}
static void OpAbortMerge() {
    if (!EnsureRepo()) return;
    if (!Confirm(g_main, L"确认放弃本次合并？\n仓库将回到合并开始前的状态，双方代码都不会丢。")) return;
    Exec({ G({ L"merge", L"--abort" }) });
}
static void OpGlobalCfg() {
    // 单弹窗同窗填写 name、email 与默认分支；分支下拉默认取当前 init.defaultBranch（未设置=main），main 项带（默认）标注
    std::wstring curBr = Trim(GitOut({ L"config", L"--global", L"init.defaultBranch" }));
    if (curBr.empty()) curBr = L"main";
    std::vector<std::wstring> choices;
    choices.push_back(L"main（默认）");
    choices.push_back(L"master");
    if (curBr != L"main" && curBr != L"master")
        choices.push_back(curBr);   // 现有值非标准时保留原样，避免被静默改写
    std::wstring dispBr = (curBr == L"main") ? L"main（默认）" : curBr;   // 预选用显示文本
    auto r = AskCfg3(g_main, L"修改全局配置",
        L"全局 user.name（提交署名，对所有仓库生效）:", Trim(GitOut({ L"config", L"--global", L"user.name" })),
        L"全局 user.email（提交邮箱）:",              Trim(GitOut({ L"config", L"--global", L"user.email" })),
        L"默认分支（git init 新仓库的初始分支名）:",  choices, dispBr);
    if (!r) return;
    std::vector<std::vector<std::wstring>> cmds;
    if (!r->v1.empty()) cmds.push_back(G({ L"config", L"--global", L"user.name",  r->v1 }));
    if (!r->v2.empty()) cmds.push_back(G({ L"config", L"--global", L"user.email", r->v2 }));
    std::wstring br = r->v3;
    const std::wstring dft = L"（默认）";
    if (br.size() > dft.size() && br.compare(br.size() - dft.size(), dft.size(), dft) == 0)
        br = br.substr(0, br.size() - dft.size());   // 去掉（默认）标注再写入配置
    if (br.empty()) br = curBr;
    cmds.push_back(G({ L"config", L"--global", L"init.defaultBranch", br }));
    cmds.push_back(G({ L"config", L"--global", L"--list" }));
    ExecV(cmds);
}
static void OpOpenIgnore() {
    if (!EnsureRepo()) return;
    fs::path gi = fs::path(g_repo) / L".gitignore";
    std::error_code ec;
    if (!fs::exists(gi, ec)) {
        if (!Confirm(g_main, L"当前仓库没有 .gitignore，是否创建一份？\n（内置 Keil 固件项目通用规则，生成后可随时手工修改）")) return;
        std::string u = WideToUtf8(GITIGNORE_TEMPLATE);
        std::ofstream f(gi, std::ios::binary | std::ios::trunc);
        f.write(u.data(), (std::streamsize)u.size());
        f.close();
        OutTxt(L"已创建 .gitignore（Keil 固件项目通用规则）。");
    }
    ShellExecuteW(nullptr, L"open", gi.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    RefreshStatusAsync();
}
static void OpClone() {
    if (g_busy) { Info(g_main, L"上一个操作还在运行，请稍候。"); return; }
    auto url = AskText(g_main, L"克隆仓库", L"仓库地址:", GITEA_HINT);
    if (!url || url->empty()) return;
    auto parentDir = BrowseFolder(g_main, L"选择克隆到哪个父目录");
    if (!parentDir) return;
    std::wstring name = *url;
    while (name.size() && name.back() == L'/') name.pop_back();
    size_t i = name.find_last_of(L'/');
    name = (i == std::wstring::npos) ? name : name.substr(i + 1);
    if (name.size() > 4 && name.rfind(L".git") == name.size() - 4) name = name.substr(0, name.size() - 4);
    if (name.empty()) { Warn(g_main, L"无法从地址解析仓库名"); return; }
    fs::path target = fs::path(*parentDir) / name;
    std::error_code ec;
    if (fs::exists(target, ec)) { Warn(g_main, L"目标目录已存在:\n" + target.wstring()); return; }
    OutSep(); OutCmd(L"git clone " + *url); OutTxt(L"克隆中，请稍候…");
    g_busy = true;
    GitResult r = RunGit(G({ L"clone", *url, target.wstring() }), *parentDir, 600000, 10000);   // 远端操作：10 秒无响应即终止
    for (auto& ln : SplitLines(r.out)) if (!Trim(ln).empty()) OutTxt(ln);
    if (r.rc != 0 || !fs::exists(target / L".git", ec)) {
        OutErr(L"克隆失败，请检查地址/账号/网络");
        // 认证失败单独提示：Windows 凭据管理器缓存的旧凭据被远端拒绝，新手最易懵
        if (r.out.find(L"Authentication failed") != std::wstring::npos
            || r.out.find(L"Failed to authenticate") != std::wstring::npos) {
            OutErr(L"凭据被拒：Windows 缓存的远端账号可能已失效（如改过密码）。"
                   L"请到 控制面板→用户账户→凭据管理器→Windows 凭据，删除 git:http://<远端地址> 开头的条目后重试，"
                   L"下次克隆会重新弹窗输入账号密码并记住。");
        }
        g_busy = false;
        return;
    }
    OutTxt(L"克隆完成: " + target.wstring());
    g_repo = target.wstring();
    SaveLastDir2(g_repo);
    SetWindowTextW(g_editDir, g_repo.c_str());
    g_remote = DetectRemote();
    g_busy = false;
    RefreshStatusAsync();
}
// 代理自动识别：依次取 Windows 系统代理（代理客户端"系统代理"模式写入的 WinINet 设置）与环境变量
static std::wstring DetectProxy() {
    std::wstring s;
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD en = 0, sz = sizeof(en);
        if (RegQueryValueExW(k, L"ProxyEnable", nullptr, nullptr, (LPBYTE)&en, &sz) == ERROR_SUCCESS && en) {
            wchar_t buf[512] = {}; sz = sizeof(buf);
            if (RegQueryValueExW(k, L"ProxyServer", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS && buf[0]) {
                s = buf;
                if (s.find(L'=') == std::wstring::npos && s.find(L"://") == std::wstring::npos)
                    s = L"http://" + s;   // "127.0.0.1:7890" 形态补协议头
            }
        }
        RegCloseKey(k);
    }
    if (s.empty()) {
        const wchar_t* e = _wgetenv(L"http_proxy");
        if (!e || !*e) e = _wgetenv(L"https_proxy");
        if (e && *e) s = e;
    }
    return s;
}
// 代理配置弹窗状态：开关 + 地址框 + 识别结果提示；OK 时 result=空串(关闭)/地址(启用)，nullopt=取消
struct ProxyState {
    HWND hChk = nullptr, hEdit = nullptr, hDetect = nullptr;
    bool done = false;
    std::optional<std::wstring> result;
};
static LRESULT CALLBACK ProxyDlgSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    ProxyState* st = (ProxyState*)data;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == 200 && code == BN_CLICKED) {          // 开关切换：启用才可编辑地址
            EnableWindow(st->hEdit, SendMessageW(st->hChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK) {
                if (SendMessageW(st->hChk, BM_GETCHECK, 0, 0) != BST_CHECKED) st->result = std::wstring();
                else {
                    int len = GetWindowTextLengthW(st->hEdit);
                    std::wstring s(len, L'\0');
                    GetWindowTextW(st->hEdit, &s[0], len + 1);
                    s = Trim(s);
                    if (s.empty()) { Warn(h, L"已启用但代理地址为空：请填写地址，或取消勾选。"); return 0; }
                    st->result = s;
                }
            } else st->result = std::nullopt;
            st->done = true;
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        if ((HWND)l == st->hDetect) {                     // 检测结果行：灰字，底色取弹窗实际擦底画刷（避免白底）
            SetTextColor(dc, STAT_DIM);
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)SendMessageW(h, WM_CTLCOLORDLG, (WPARAM)dc, (LPARAM)h);
        }
        break;
    }
    case WM_CLOSE:
        st->done = true; st->result = std::nullopt;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
// 代理配置弹窗：on=当前是否启用，addr=预填地址（一般传自动识别结果），detected=识别结果提示文本
static std::optional<std::wstring> AskProxy(HWND parent, bool on,
                                            const std::wstring& addr, const std::wstring& detected) {
    int m = (int)(14 * g_scale), lblH = (int)(20 * g_scale), editH = (int)(24 * g_scale);
    int chkH = (int)(22 * g_scale), btnH = (int)(28 * g_scale), tipH = (int)(18 * g_scale);
    int w = (int)(480 * g_scale);
    int ch = m + chkH + (int)(8 * g_scale) + lblH + editH + (int)(10 * g_scale) + tipH
           + (int)(14 * g_scale) + btnH + m;
    RECT rcFrame = { 0, 0, w, ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rcFrame.right - rcFrame.left, wh = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    ProxyState st;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"代理配置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, ww, wh, parent, nullptr, g_hInst, nullptr);
    st.hChk = CreateWindowExW(0, L"BUTTON", L"启用代理", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        m, m, [&]{   // 控件宽=文字实测+方框余量：勾选命中区只覆盖方框与文字，整行其余位置点击无效
            HDC dc = GetDC(parent);
            HFONT of = (HFONT)SelectObject(dc, g_fontUI);
            SIZE sz; GetTextExtentPoint32W(dc, L"启用代理", 4, &sz);
            SelectObject(dc, of); ReleaseDC(parent, dc);
            return sz.cx + (int)(26 * g_scale);
        }(), chkH, dlg, (HMENU)200, g_hInst, nullptr);
    HWND hLbl = CreateWindowExW(0, L"STATIC", L"代理地址:", WS_CHILD | WS_VISIBLE,
        m, m + chkH + (int)(8 * g_scale), w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    st.hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", addr.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, m, m + chkH + (int)(8 * g_scale) + lblH, w - 2 * m, editH,
        dlg, (HMENU)101, g_hInst, nullptr);
    st.hDetect = CreateWindowExW(0, L"EDIT",
        (detected.empty() ? L"未检测到本地代理，可手动填写" : L"检测到本地代理：" + detected).c_str(),
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE, m, m + chkH + (int)(8 * g_scale) + lblH + editH + (int)(10 * g_scale), w - 2 * m, tipH,
        dlg, nullptr, g_hInst, nullptr);   // 只读编辑框：检测结果可拖选复制；多行模式无横向自动滚，选区不抖动
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        w - 2 * (int)(80 * g_scale) - m, ch - m - btnH, (int)(80 * g_scale), btnH, dlg, (HMENU)IDOK, g_hInst, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        w - (int)(80 * g_scale) - m, ch - m - btnH, (int)(80 * g_scale), btnH, dlg, (HMENU)IDCANCEL, g_hInst, nullptr);
    for (HWND c : { st.hChk, hLbl, st.hEdit, st.hDetect, hOk, hCancel })
        SendMessageW(c, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(st.hDetect, WM_SETFONT, (WPARAM)g_fontMenu, TRUE);   // 检测结果行：比 UI 小 2 号
    SendMessageW(st.hChk, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(st.hEdit, on);
    SetWindowSubclass(dlg, ProxyDlgSub, 1, (DWORD_PTR)&st);
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(on ? st.hEdit : st.hChk);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    return st.result;
}
// 代理配置：仅对 github.com 生效（内网 Gitea 的直连覆盖不受影响）
// 自动识别：预填依次取 当前 git 代理 → Windows 系统代理 → http_proxy/https_proxy 环境变量
static void OpProxyCfg() {
    std::wstring cur = Trim(GitOut({ L"config", L"--global", L"--get", L"http.https://github.com/.proxy" }));
    std::wstring detected = DetectProxy();
    auto r = AskProxy(g_main, !cur.empty(), cur.empty() ? detected : cur, detected);
    if (!r) return;                                          // 取消
    if (r->empty()) {                                        // 关闭代理
        if (cur.empty()) { Info(g_main, L"当前未设置代理，无需关闭。"); return; }
        OutSep(); OutCmd(L"git config --global --unset http.https://github.com/.proxy");
        Exec({ G({ L"config", L"--global", L"--unset", L"http.https://github.com/.proxy" }) });
        OutTxt(L"已关闭 github.com 代理（恢复直连）。");
    } else {                                                 // 启用并写入
        OutSep(); OutCmd(L"git config --global http.https://github.com/.proxy " + *r);
        ExecV({ G({ L"config", L"--global", L"http.https://github.com/.proxy", *r }),
                G({ L"config", L"--global", L"--get", L"http.https://github.com/.proxy" }) });
        OutTxt(L"已启用 github.com 代理: " + *r + L"，后续 推送/拉取 走该代理。");
    }
}
static void OpResetGit() {
    if (!EnsureRepo()) return;
    fs::path gitdir = fs::path(g_repo) / L".git";
    std::error_code ec;
    if (!fs::is_directory(gitdir, ec)) { Info(g_main, L"该目录还没有 Git 历史，无需清空。"); return; }
    if (g_busy) { Info(g_main, L"有操作正在运行，请等它完成后再清空。"); return; }
    std::wstring note = L"【危险】将删除该目录的 .git 与 .gitignore：\n"
                        L"· 全部提交历史消失（含未推送的提交，reflog 也救不回）\n"
                        L"· 远端配置、分支、标签全部消失\n"
                        L"· 目录里的代码/文档文件全部保留\n"
                        L"· .gitignore 一并删除（重走流程③时按默认规则重新生成）\n";
    if (!g_remote.empty()) note += L"· 该仓库曾推送过远端：清空后重推必须强制覆盖远端旧历史\n";
    note += L"\n确认清空请输入大写 Y（其他输入或留空=取消）：";
    auto y = AskText(g_main, L"清空Git重来（危险操作）", note.c_str());
    if (!y || *y != L"Y") return;
    fs::remove_all(gitdir, ec);
    if (ec) { Warn(g_main, std::wstring(L"删除 .git 失败：\n") + GbkToWide(ec.message())); return; }
    fs::path gi = fs::path(g_repo) / L".gitignore";
    bool hadGi = fs::exists(gi, ec);
    fs::remove(gi, ec);
    g_remote.clear();
    OutSep(); OutTxt(L"已清空 Git 历史（.git 已删除），所有文件保留未动。");
    if (hadGi) OutTxt(L"旧 .gitignore 也已一并删除；重走流程③【一键配置ignore】时会重新生成。");
    OutTxt(L"需要重来可点【✚ 初始化仓库】。");
    RefreshStatusAsync();
}
// ============================ 左侧菜单表 ====================================
enum {
    IDM_REMOTE_ADD = IDM_BASE,
    IDM_CLONE, IDM_STATUS, IDM_DIFF, IDM_COMMIT, IDM_PUSH, IDM_FORCE_PUSH, IDM_PULL, IDM_LOG_ALL, IDM_SHOW, IDM_REMOTE_INFO,
    IDM_TAG_LIST, IDM_TAG_NEW, IDM_TAG_PUSH, IDM_TAG_DEL, IDM_TAG_RDEL,
    IDM_BRANCH_SW, IDM_BRANCH_NEW, IDM_BRANCH_MERGE, IDM_BRANCH_DEL, IDM_CHECKOUT_REF, IDM_BACK_MAIN, IDM_STASH, IDM_STASH_POP,
    IDM_RESTORE_STAGED, IDM_RESTORE_FILE, IDM_DISCARD_ALL, IDM_UNDO_LAST, IDM_AMEND, IDM_REVERT, IDM_DELETE_COMMIT,
    IDM_CONFLICT_LIST, IDM_NOTEPAD, IDM_MARK_RESOLVED, IDM_TAKE_OURS, IDM_TAKE_THEIRS, IDM_ABORT_MERGE,
};
struct MenuItem { int id; const wchar_t* name; void (*fn)(); };
struct MenuCat  { const wchar_t* title; std::vector<MenuItem> items; };
static const MenuCat MENU[] = {
    { L"远端", {
        { IDM_REMOTE_ADD,    L"配置远程",         OpRemoteCfg },
    } },
    // 克隆仓库已移到顶栏（IDM_CLONE 显式分发），左侧不再显示
    { L"日常", {
        { IDM_STATUS,       L"查看状态",   OpStatus },
        { IDM_DIFF,         L"查看改动",   OpDiff },
        { IDM_COMMIT,       L"提交修改",   OpCommit },
        { IDM_PUSH,         L"推送(commit)", OpPush },
        { IDM_FORCE_PUSH,   L"强制推送",    OpForcePush },
        { IDM_PULL,         L"拉取",       OpPull },
        { IDM_LOG_ALL,      L"查看历史(全量)", OpLogAll },
        { IDM_SHOW,         L"查看某次提交", OpShow },
        { IDM_REMOTE_INFO,  L"查看远端与分支对应", OpRemoteInfo },
    } },
    { L"标签", {
        { IDM_TAG_LIST,     L"查看当前标签", OpTagList },
        { IDM_TAG_NEW,      L"打标签",     OpTagNew },
        { IDM_TAG_PUSH,     L"推送(tag)",  OpTagPush },
        { IDM_TAG_DEL,      L"删除标签",   OpTagDel },
        { IDM_TAG_RDEL,     L"推送(删除tag)", OpTagRemoteDel },
    } },
    { L"分支", {
        { IDM_BRANCH_SW,     L"切换分支",     OpBranchSw },
        { IDM_BRANCH_NEW,    L"新建分支并切换", OpBranchNew },
        { IDM_BRANCH_MERGE,  L"合并分支",     OpBranchMerge },
        { IDM_BRANCH_DEL,    L"删除已合并分支", OpBranchDel },
        { IDM_CHECKOUT_REF,  L"跳转到标签/提交", OpCheckoutRef },
        { IDM_BACK_MAIN,     L"回到 main/master 最新", OpBackMain },
        { IDM_STASH,         L"stash 收起当前改动", OpStash },
        { IDM_STASH_POP,     L"stash 恢复最近收起", OpStashPop },
    } },
    { L"撤销", {
        { IDM_RESTORE_STAGED, L"撤销已暂存(退回工作区)", OpRestoreStaged },
        { IDM_RESTORE_FILE,   L"丢弃单个文件改动[危险]", OpRestoreFile },
        { IDM_DISCARD_ALL,    L"撤销全部未提交改动[危险]", OpDiscardAll },
        { IDM_UNDO_LAST,      L"撤销最后一次提交(保留改动)", OpUndoLast },
        { IDM_AMEND,          L"修改最后一次提交(--amend)", OpAmend },
        { IDM_REVERT,         L"反向提交 revert", OpRevert },
        { IDM_DELETE_COMMIT,  L"删除提交(回退)[危险]", OpDeleteCommit },
    } },
    { L"冲突处理", {
        { IDM_CONFLICT_LIST,  L"查看冲突文件列表", OpConflictList },
        { IDM_NOTEPAD,        L"用记事本编辑冲突文件", OpNotepad },
        { IDM_MARK_RESOLVED,  L"标记文件已解决", OpMarkResolved },
        { IDM_TAKE_OURS,      L"采用我方版本(当前分支)", OpTakeOurs },
        { IDM_TAKE_THEIRS,    L"采用对方版本(被合并分支)", OpTakeTheirs },
        { IDM_ABORT_MERGE,    L"放弃本次合并", OpAbortMerge },
    } },
};

// ============================ ignore 勾选弹窗 ===============================
// 勾选弹窗状态：复选框句柄表 + 结果（OK 时逐项回填 checked，取消只置 done）
struct GiCheckState {
    std::vector<HWND> hCbs;
    std::vector<bool> checked;
    bool done = false, ok = false;
};
static LRESULT CALLBACK GiCheckDlgSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    GiCheckState* st = (GiCheckState*)data;
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK) {
                for (size_t i = 0; i < st->hCbs.size(); ++i)
                    st->checked[i] = SendMessageW(st->hCbs[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
                st->ok = true;
            }
            st->done = true;
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE:
        st->done = true;
        st->ok = false;
        DestroyWindow(h);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
// ignore 规则勾选弹窗：按 GI_OPTIONS 逐项生成复选框（默认勾选取 dft），OK 返回各项勾选态，取消返回 nullopt
static std::optional<std::vector<bool>> AskGiChecks(HWND parent, const wchar_t* title) {
    int n = (int)(sizeof(GI_OPTIONS) / sizeof(GI_OPTIONS[0]));
    int m = (int)(14 * g_scale), bw = (int)(80 * g_scale);
    int lblH = (int)(24 * g_scale);
    int cbH = (int)(28 * g_scale), cbGap = (int)(4 * g_scale);
    int btnH = (int)(28 * g_scale);
    int w = (int)(470 * g_scale);
    int ch = m + lblH + (int)(6 * g_scale) + n * cbH + (n - 1) * cbGap
           + (int)(16 * g_scale) + btnH + m;
    RECT rcFrame = { 0, 0, w, ch };
    AdjustWindowRectEx(&rcFrame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rcFrame.right - rcFrame.left, wh = rcFrame.bottom - rcFrame.top;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - ww) / 2, y = pr.top + ((pr.bottom - pr.top) - wh) / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    GiCheckState state;
    state.checked.assign(n, false);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, ww, wh, parent, nullptr, g_hInst, nullptr);
    SetWindowSubclass(dlg, GiCheckDlgSub, 1, (DWORD_PTR)&state);
    HWND hLabel = CreateWindowExW(0, L"STATIC", L"勾选要写入 .gitignore 的规则类别：", WS_CHILD | WS_VISIBLE,
        m, m, w - 2 * m, lblH, dlg, nullptr, g_hInst, nullptr);
    SendMessageW(hLabel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    int cy = m + lblH + (int)(6 * g_scale);
    for (int i = 0; i < n; ++i) {
        HWND cb = CreateWindowExW(0, L"BUTTON", GI_OPTIONS[i].title,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            m, cy, w - 2 * m, cbH, dlg, (HMENU)(UINT_PTR)(100 + i), g_hInst, nullptr);
        SendMessageW(cb, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SendMessageW(cb, BM_SETCHECK, GI_OPTIONS[i].dft ? BST_CHECKED : BST_UNCHECKED, 0);
        state.hCbs.push_back(cb);
        cy += cbH + cbGap;
    }
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        w - 2 * bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDOK, g_hInst, nullptr);
    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        w - bw - m, ch - m - btnH, bw, btnH, dlg, (HMENU)IDCANCEL, g_hInst, nullptr);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.hCbs.empty() ? dlg : state.hCbs[0]);
    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    if (!state.ok) return std::nullopt;
    return state.checked;
}
// ============================ 一键配置 ignore ===============================
static void OpIgnoreCfg() {
    if (!EnsureRepo()) return;
    std::error_code gec;
    if (!fs::exists(fs::path(g_repo) / L".git", gec)) {
        Warn(g_main, L"该目录还不是 git 仓库（.git 不存在），请先点顶栏【✚ 初始化仓库】。");
        return;
    }
    int n = (int)(sizeof(GI_OPTIONS) / sizeof(GI_OPTIONS[0]));
    auto picks = AskGiChecks(g_main, L"一键配置 ignore");
    if (!picks) return;
    fs::path gi = fs::path(g_repo) / L".gitignore";
    std::error_code ec;
    if (fs::exists(gi, ec) && !Confirm(g_main, L"已存在 .gitignore，覆盖它吗？\n（覆盖前可先点【>_ 打开 .gitignore】查看现有内容）")) return;
    std::wstring text;
    for (int i = 0; i < n; ++i) {
        if (!(*picks)[i]) continue;
        if (!text.empty()) text += L"\r\n\r\n";
        text += GI_OPTIONS[i].rules;
    }
    std::string u = WideToUtf8(text);
    bool hasHead = !Trim(GitOut({ L"rev-parse", L"-q", L"--verify", L"HEAD" })).empty();
    // 与磁盘现有内容比对：完全一致则不重写文件（沿用磁盘上的）
    bool changed = true;
    {
        std::ifstream rf(gi, std::ios::binary);
        if (rf) { std::string old((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>()); changed = (old != u); }
    }
    if (!hasHead) {
        // 流程④首次提交：仓库尚无任何提交时，无论 .gitignore 是否新写都要完成首次提交
        if (changed) {
            std::ofstream f(gi, std::ios::binary | std::ios::trunc);
            f.write(u.data(), (std::streamsize)u.size());
            f.close();
            OutTxt(L".gitignore 已生成，正在自动完成首次提交…");
        } else {
            OutTxt(L".gitignore 内容与现有一致（沿用），仓库尚无提交，正在自动完成首次提交…");
        }
        ExecV({ G({ L"add", L"-A" }), G({ L"commit", L"-m", L"chore: 初始化仓库" }) });
        RefreshStatusAsync();
        return;
    }
    if (!changed) {
        OutTxt(L"勾选的规则与现有 .gitignore 内容一致，无需改动。");
        RefreshStatusAsync();
        return;
    }
    std::ofstream f(gi, std::ios::binary | std::ios::trunc);
    f.write(u.data(), (std::streamsize)u.size());
    f.close();
    // 已有历史：只提交 ignore 这一个文件的改动
    OutTxt(L".gitignore 已更新，正在自动提交改动…");
    ExecV({ G({ L"add", L".gitignore" }), G({ L"commit", L"-m", L"chore: 更新 .gitignore 忽略规则" }) });
    RefreshStatusAsync();
}

// ============================ 命令输入行 ====================================
static void RunCmdLine() {
    if (!EnsureRepo()) return;
    int len = GetWindowTextLengthW(g_cmdEdit);
    if (!len) return;
    std::wstring s(len, L'\0');
    GetWindowTextW(g_cmdEdit, &s[0], len + 1);
    s = Trim(s);
    if (s.empty()) return;
    auto tokens = Tokenize(s);
    if (tokens.empty()) return;
    if (_wcsicmp(tokens[0].c_str(), L"git") != 0) tokens.insert(tokens.begin(), L"git");
    SetWindowTextW(g_cmdEdit, L"");
    std::vector<std::wstring> argv = { g_git, L"-c", L"i18n.logoutputencoding=utf-8" };
    for (size_t i = 1; i < tokens.size(); ++i) argv.push_back(tokens[i]);
    std::vector<JobCmd> v; v.push_back({ argv, false });
    ExecAsync(std::move(v));
}
// 子类化输入框：回车即运行
static LRESULT CALLBACK CmdEditSubProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { RunCmdLine(); return 0; }
    return CallWindowProcW(g_cmdEditProc, h, m, w, l);
}
static void OpenDirFromEdit();
static LRESULT CALLBACK DirEditSubProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { OpenDirFromEdit(); return 0; }
    return CallWindowProcW(g_dirEditProc, h, m, w, l);
}

// ============================ 打开目录 ======================================
static bool IsGitRepo(const std::wstring& d) {
    std::error_code ec;
    return fs::is_directory(fs::path(d) / L".git", ec);
}
static void OpenDirFromEdit() {
    int len = GetWindowTextLengthW(g_editDir);
    if (!len) return;
    std::wstring s(len, L'\0');
    GetWindowTextW(g_editDir, &s[0], len + 1);
    std::wstring dir = Trim(s);
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(fs::path(dir), ec)) { Warn(g_main, L"目录不存在：" + dir); return; }
    g_repo = fs::path(dir).wstring();
    SaveLastDir2(g_repo);
    OutSep();
    OutCmd(L"操作目录: " + g_repo + (IsGitRepo(g_repo) ? L"" : L"（未初始化）"));
    g_remote = DetectRemote();
    OutTxt(L"默认远端: " + (g_remote.empty() ? L"（该仓尚未配置远端）" : g_remote));
    RefreshStatusAsync();
}
static void PickDir() {
    auto d = BrowseFolder(g_main, L"选定 git 仓库目录");
    if (!d) return;
    std::error_code ec;
    std::wstring norm = fs::path(*d).wstring();
    SetWindowTextW(g_editDir, norm.c_str());
    OpenDirFromEdit();
}

// ============================ 流程卡锁定 ====================================
// 流程①②③（全局配置/初始化/ignore）未完成时左侧栏整体置灰禁点（LeftLocked），
// ④ 远端+首推为可选环节：不配置不锁定任何功能，
// 仅在用到远端相关操作时由 NeedRemote() 弹窗提示配置。

// ============================ UI 构建 =======================================
static HWND MkBtn(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h, HFONT f, bool flat = false) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(b, WM_SETFONT, (WPARAM)f, TRUE);
    SetWindowLongPtrW(b, GWLP_USERDATA, (LONG_PTR)y);   // 记录基准 y，滚动时重定位
    return b;
}
static HWND MkStatic(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h, HFONT f) {
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    SetWindowLongPtrW(c, GWLP_USERDATA, (LONG_PTR)y);
    return c;
}
// 左侧面板：单窗口自绘（无子窗口按钮），滚动/悬停全走重绘，从机制上根除残影与闪烁
struct LeftItem { bool cat; std::wstring name; int id; int y; int h; };
static std::vector<LeftItem> g_leftItems;
static int g_leftHover = -1, g_leftPressed = -1;
static bool g_leftTracking = false;

// 滚动条几何：nMax=内容总高、nPage=可视高，滑块到底才会真正到底
static void UpdateLeftScroll(HWND h) {
    RECT rc; GetClientRect(h, &rc);
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE, 0, g_leftTotalH, (UINT)rc.bottom, 0 };
    SetScrollInfo(h, SB_VERT, &si, TRUE);
}
static int LeftHitTest(HWND h, int my) {
    int yy = my + g_leftScroll;   // 客户区 y → 内容 y
    for (int i = 0; i < (int)g_leftItems.size(); ++i)
        if (!g_leftItems[i].cat && yy >= g_leftItems[i].y && yy < g_leftItems[i].y + g_leftItems[i].h)
            return i;
    return -1;
}
// 流程①②③（全局配置/初始化/ignore）任一未完成：左侧栏整体置灰禁点（三项配置都在顶栏完成，不会死锁）
static bool LeftLocked() {
    return g_flowBlock >= 0 && g_flowBlock <= 2;
}
static void LeftScrollTo(HWND h, int pos) {
    RECT rc; GetClientRect(h, &rc);
    int maxScroll = g_leftTotalH - (rc.bottom - rc.top);
    if (maxScroll < 0) maxScroll = 0;
    if (pos < 0) pos = 0;
    if (pos > maxScroll) pos = maxScroll;
    if (pos != g_leftScroll) {
        g_leftScroll = pos;
        SetScrollPos(h, SB_VERT, pos, TRUE);
        InvalidateRect(h, nullptr, FALSE);
    }
}
static LRESULT CALLBACK LeftPanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND:
        return 1;   // WM_PAINT 双缓冲全绘，不再单独擦背景（免闪烁）
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP oldb = (HBITMAP)SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(RGB(0xf0, 0xf0, 0xf0));
        FillRect(mem, &rc, bg);
        DeleteObject(bg);
        SetBkMode(mem, TRANSPARENT);
        int bw = (int)(182 * g_scale);
        if (bw > rc.right - 8) bw = rc.right - 8;
        for (int i = 0; i < (int)g_leftItems.size(); ++i) {
            LeftItem& it = g_leftItems[i];
            int y = it.y - g_leftScroll;
            if (y + it.h < 0 || y > rc.bottom) continue;
            if (it.cat) {
                SelectObject(mem, g_fontMenuBold);
                SetTextColor(mem, RGB(0x33, 0x33, 0x33));
                std::wstring t = L"— " + it.name + L" —";
                RECT tr = { 0, y, rc.right, y + it.h };
                DrawTextW(mem, t.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                bool lock = LeftLocked();
                bool hov = !lock && (i == g_leftHover), prs = !lock && (i == g_leftPressed);
                RECT br = { 4, y, 4 + bw, y + it.h };
                HBRUSH fill = CreateSolidBrush(lock ? RGB(0xf2, 0xf2, 0xf2)
                                                    : prs ? RGB(0xdc, 0xe9, 0xfd)
                                                    : hov ? RGB(0xe8, 0xf0, 0xfe) : RGB(0xff, 0xff, 0xff));
                FillRect(mem, &br, fill);
                DeleteObject(fill);
                HBRUSH frame = CreateSolidBrush(lock ? RGB(0xc8, 0xc8, 0xc8)
                                                     : (hov || prs) ? RGB(0x1a, 0x73, 0xe8) : RGB(0xad, 0xad, 0xad));
                FrameRect(mem, &br, frame);
                DeleteObject(frame);
                SelectObject(mem, g_fontMenu);
                SetTextColor(mem, lock ? RGB(0xa6, 0xa6, 0xa6) : RGB(0x1a, 0x1a, 0x1a));
                DrawTextW(mem, it.name.c_str(), -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);   // 双缓冲回贴到窗口
        SelectObject(mem, oldb);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int hit = LeftLocked() ? -1 : LeftHitTest(h, (short)HIWORD(l));
        if (hit != g_leftHover) {
            g_leftHover = hit;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (!g_leftTracking) {
            TRACKMOUSEEVENT te = { sizeof(te), TME_LEAVE, h, 0 };
            TrackMouseEvent(&te);
            g_leftTracking = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_leftTracking = false;
        if (g_leftHover != -1) { g_leftHover = -1; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONDOWN:
        if (LeftLocked()) return 0;   // 流程未配置完：点击无效
        g_leftPressed = LeftHitTest(h, (short)HIWORD(l));
        if (g_leftPressed != -1) {
            SetCapture(h);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        int hit = LeftHitTest(h, (short)HIWORD(l));
        int pr = g_leftPressed;
        g_leftPressed = -1;
        InvalidateRect(h, nullptr, FALSE);
        if (hit != -1 && hit == pr)
            SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(g_leftItems[hit].id, 0), 0);
        return 0;
    }
    case WM_VSCROLL: {
        int line = (int)(22 * g_scale);
        int pos = g_leftScroll;
        switch (LOWORD(w)) {
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = g_leftTotalH; break;
            case SB_LINEUP: pos -= line; break;
            case SB_LINEDOWN: pos += line; break;
            case SB_PAGEUP: pos -= (int)(120 * g_scale); break;
            case SB_PAGEDOWN: pos += (int)(120 * g_scale); break;
            case SB_THUMBTRACK: pos = HIWORD(w); break;
        }
        LeftScrollTo(h, pos);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int pos = g_leftScroll - (int)GET_WHEEL_DELTA_WPARAM(w) * (int)(22 * g_scale) / WHEEL_DELTA;
        LeftScrollTo(h, pos);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
static HWND g_topStatics[2];
static void BuildControls() {
    // 顶栏（两个 STATIC 已在 WinMain 创建，这里只建输入框与按钮）
    g_editDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                0, 0, 0, 0, g_main, (HMENU)IDC_DIR, g_hInst, nullptr);
    SendMessageW(g_editDir, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    g_dirEditProc = (WNDPROC)SetWindowLongPtrW(g_editDir, GWLP_WNDPROC, (LONG_PTR)DirEditSubProc);
    MkBtn(g_main, L"打开", IDC_PICKDIR, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"清空输出", IDC_CLEAROUT, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"克隆仓库", IDM_CLONE, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"ℹ 关于", IDC_ABOUT, 0, 0, 0, 0, g_fontUI);   // 关于行（标题栏与操作目录行之间）
    MkBtn(g_main, L"代理配置", IDC_PROXYCFG, 0, 0, 0, 0, g_fontUI);   // github 代理设置（在"关于"后面）
    // 状态按钮行
    MkBtn(g_main, L"⟳ 刷新状态", IDC_REFRESH, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"⚙ 修改全局配置", IDC_GLOBALCFG, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"✚ 初始化仓库", IDC_INIT, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"⤓ 一键配置ignore", IDC_IGNORE, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L">_ 打开 .gitignore", IDC_OPENIGN, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"清空Git重来", IDC_RESET, 0, 0, 0, 0, g_fontUI);
    // 状态卡（RichEdit）+ 输出区（自绘黑底日志）
    g_stat = NewRich(g_main, (HMENU)IDC_STAT, RGB(0xff, 0xff, 0xff));
    g_out  = CreateWindowExW(0, L"GIT_OUT", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                             0, 0, 0, 0, g_main, (HMENU)IDC_OUT, g_hInst, nullptr);
    // 命令输入行
    g_cmdEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                0, 0, 0, 0, g_main, (HMENU)IDC_CMDEDIT, g_hInst, nullptr);
    SendMessageW(g_cmdEdit, WM_SETFONT, (WPARAM)g_fontMono, TRUE);
    g_cmdEditProc = (WNDPROC)SetWindowLongPtrW(g_cmdEdit, GWLP_WNDPROC, (LONG_PTR)CmdEditSubProc);
    MkBtn(g_main, L"运行", IDC_CMDRUN, 0, 0, 0, 0, g_fontUI);
    // 左侧面板（单窗口自绘，菜单项为数据而非子窗口）
    g_leftPanel = CreateWindowExW(0, L"GIT_LEFT", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                  0, 0, 0, 0, g_main, (HMENU)IDC_LEFT, g_hInst, nullptr);
    g_leftItems.clear();
    int y = (int)(4 * g_scale);
    int catH = (int)(22 * g_scale), btnH = (int)(22 * g_scale), gap = (int)(1 * g_scale);
    for (auto& cat : MENU) {
        g_leftItems.push_back({ true, cat.title, 0, y, catH });
        y += catH + gap;
        for (auto& item : cat.items) {
            g_leftItems.push_back({ false, item.name, item.id, y, btnH });
            y += btnH + gap;
        }
    }
    g_leftTotalH = y + (int)(8 * g_scale);
    UpdateLeftScroll(g_leftPanel);
}
// 主窗口布局（按 DPI 缩放）
static void Layout() {
    if (!g_stat || !g_out || !g_leftPanel || !g_editDir || !g_cmdEdit) return;   // 控件未建完
    RECT rc; GetClientRect(g_main, &rc);
    int W = rc.right, H = rc.bottom;
    int m = (int)(6 * g_scale);
    int rowH = (int)(30 * g_scale);
    int statH = g_statLineCnt * (int)(22 * g_scale) + (int)(16 * g_scale);   // 按行数自适应，免得撑出滚动条
    int cmdH = (int)(28 * g_scale);
    int leftW = (int)(200 * g_scale);
    // 顶栏：标签宽度按文字实测（DPI 缩放下写死宽度会截断「操作目录:」）；输入框右缘贴「打开」按钮左缘
    int lblX = m + (int)(2 * g_scale), lblW = (int)(64 * g_scale), lblH = 20;
    {
        HDC dc0 = GetDC(g_main);
        HFONT of0 = (HFONT)SelectObject(dc0, g_fontUI);
        SIZE sz0;
        GetTextExtentPoint32W(dc0, L"操作目录:", 5, &sz0);
        SelectObject(dc0, of0);
        ReleaseDC(g_main, dc0);
        lblW = sz0.cx + (int)(8 * g_scale);
        lblH = sz0.cy + 6;
    }
    // 关于行：标题栏与"操作目录"行之间，仅一个【ℹ 关于】按钮
    int row1Y = m + (int)(28 * g_scale) + (int)(4 * g_scale);
    MoveWindow(GetDlgItem(g_main, IDC_ABOUT), lblX, m, (int)(90 * g_scale), (int)(24 * g_scale), TRUE);
    MoveWindow(GetDlgItem(g_main, IDC_PROXYCFG), lblX + (int)(94 * g_scale), m, (int)(90 * g_scale), (int)(24 * g_scale), TRUE);
    MoveWindow(g_topStatics[0], lblX, row1Y + 5, lblW, lblH, TRUE);
    int entryX = lblX + lblW + (int)(4 * g_scale);
    int entryR = W - (int)(308 * g_scale) - (int)(8 * g_scale);   /* 「打开」按钮左缘留 8px */
    if (entryR < entryX + (int)(120 * g_scale)) entryR = entryX + (int)(120 * g_scale);
    MoveWindow(g_editDir, entryX, row1Y, entryR - entryX, (int)(24 * g_scale), TRUE);
    // 打开 / 清空输出 / 克隆仓库 放右端
    HWND open = GetDlgItem(g_main, IDC_PICKDIR), clear = GetDlgItem(g_main, IDC_CLEAROUT), clone = GetDlgItem(g_main, IDM_CLONE);
    MoveWindow(open, W - (int)(308 * g_scale), row1Y, (int)(64 * g_scale), (int)(24 * g_scale), TRUE);
    MoveWindow(clear, W - (int)(236 * g_scale), row1Y, (int)(90 * g_scale), (int)(24 * g_scale), TRUE);
    MoveWindow(clone, W - (int)(138 * g_scale), row1Y, (int)(86 * g_scale), (int)(24 * g_scale), TRUE);
    // 左栏
    // 左栏与状态按钮行同一起点（都在"操作目录"行下方）
    int bodyY = row1Y + (int)(24 * g_scale) + (int)(8 * g_scale);
    MoveWindow(g_leftPanel, 0, bodyY, leftW, H - bodyY, TRUE);
    UpdateLeftScroll(g_leftPanel);
    {   // 窗口变高后钳制滚动位置，滑块与内容保持一致
        RECT lrc; GetClientRect(g_leftPanel, &lrc);
        int maxScroll = g_leftTotalH - (lrc.bottom - lrc.top);
        if (maxScroll < 0) maxScroll = 0;
        if (g_leftScroll > maxScroll) { g_leftScroll = maxScroll; InvalidateRect(g_leftPanel, nullptr, FALSE); }
    }
    int rightX = leftW + m, rightW = W - rightX - m;
    // 状态按钮行：起点=标题条下方（行1 高度 24 缩放像素 + 8 间距），宽度按文字实测分配，避免小字被截断
    int y = bodyY;
    int idsSbar[6] = { IDC_REFRESH, IDC_GLOBALCFG, IDC_INIT, IDC_IGNORE, IDC_OPENIGN, IDC_RESET };
    int wtxt[6]; int total = 0;
    HDC dc = GetDC(g_main);
    HFONT of = (HFONT)SelectObject(dc, g_fontUI);
    SIZE sz;
    for (int i = 0; i < 6; ++i) {
        wchar_t t[64]; GetDlgItemTextW(g_main, idsSbar[i], t, 64);
        GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
        wtxt[i] = sz.cx + (int)(22 * g_scale);
        total += wtxt[i];
    }
    SelectObject(dc, of);
    ReleaseDC(g_main, dc);
    total += 5 * (int)(4 * g_scale);
    int avail = rightW - (int)(8 * g_scale);
    double fit = total > avail ? (double)avail / total : 1.0;
    int x2 = rightX;
    for (int i = 0; i < 6; ++i) {
        int w2 = (int)(wtxt[i] * fit) - (int)(4 * g_scale);
        MoveWindow(GetDlgItem(g_main, idsSbar[i]), x2, y, w2, rowH, TRUE);
        x2 += w2 + (int)(4 * g_scale);
    }
    // 状态卡
    y += rowH + (int)(4 * g_scale);
    MoveWindow(g_stat, rightX, y, rightW, statH, TRUE);
    // 命令输入行（提示符高度随 DPI 缩放，写死 20px 会被大字体裁成残角）
    y += statH + (int)(4 * g_scale);
    MoveWindow(g_topStatics[1], rightX, y + (int)(2 * g_scale), (int)(34 * g_scale), (int)(24 * g_scale), TRUE);
    MoveWindow(g_cmdEdit, rightX + (int)(40 * g_scale), y, rightW - (int)(110 * g_scale), (int)(24 * g_scale), TRUE);
    MoveWindow(GetDlgItem(g_main, IDC_CMDRUN), rightX + rightW - (int)(64 * g_scale), y, (int)(64 * g_scale), (int)(24 * g_scale), TRUE);
    // 输出区
    y += cmdH + (int)(6 * g_scale);
    MoveWindow(g_out, rightX, y, rightW, H - y - m, TRUE);
    UpdateOutScroll(g_out);
    {   // 窗口变高后钳制滚动位置
        RECT orc; GetClientRect(g_out, &orc);
        int maxScroll = g_logTotalH - (orc.bottom - orc.top);
        if (maxScroll < 0) maxScroll = 0;
        if (g_logScroll > maxScroll) { g_logScroll = maxScroll; InvalidateRect(g_out, nullptr, FALSE); }
    }
}
// 状态卡渲染
static void RenderStatus(std::vector<std::vector<Seg>>* lines) {
    g_statLineCnt = (int)lines->size();
    std::wstring all;                                    // 纯文本比对：内容没变就不重建（每 4 秒刷新完全静止）
    for (auto& ln : *lines) for (auto& s : ln) all += s.text;
    if (all == g_statLastText) { delete lines; return; }
    g_statLastText = all;
    int needH = g_statLineCnt * (int)(22 * g_scale) + (int)(16 * g_scale);
    static int s_lastNeed = -1;
    bool resize = needH != s_lastNeed;                   // 与上次需要的卡高比较（客户区含边框，不能直接比）
    s_lastNeed = needH;
    SendMessageW(g_stat, WM_SETREDRAW, FALSE, 0);        // 关重绘：清空+逐行追加过程不闪
    SetWindowTextW(g_stat, L"");
    for (auto& ln : *lines) RichAppendLine(g_stat, ln);
    SendMessageW(g_stat, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_stat, nullptr, FALSE);
    delete lines;
    if (resize) Layout();   // 行数变化才重排布局
}

// ============================ 主窗口过程 ====================================
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE:
        Layout();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        (void)code;
        if (id == IDC_PICKDIR) { PickDir(); return 0; }
        if (id == IDC_CLEAROUT) {
            g_log.clear(); g_logScroll = 0; g_logMaxW = 0; g_logScrollX = 0; g_anchorL = g_selCurL = -1; UpdateOutScroll(g_out); InvalidateRect(g_out, nullptr, FALSE);
            OutTxt(L"提示：输出区可拖选文字，Ctrl+C 复制选中（未选中=全部），Ctrl+A 全选，Ctrl+V 贴到命令输入行。");
            return 0;
        }
        if (id == IDC_CMDRUN) { RunCmdLine(); return 0; }
        if (id == IDC_REFRESH) { RefreshStatusAsync(true); return 0; }   // 手动刷新：同步刷新全部信息（含远端标签）
        if (id == IDC_GLOBALCFG) { OpGlobalCfg(); return 0; }
        if (id == IDC_INIT) { OpInitRepo(); return 0; }
        if (id == IDC_IGNORE) { OpIgnoreCfg(); return 0; }
        if (id == IDC_OPENIGN) { OpOpenIgnore(); return 0; }
        if (id == IDC_RESET) { OpResetGit(); return 0; }
        if (id == IDM_CLONE) { OpClone(); return 0; }
        if (id == IDC_ABOUT) { ShowAboutDialog(nullptr); return 0; }
        if (id == IDC_PROXYCFG) { OpProxyCfg(); return 0; }
        if (id >= IDM_BASE && id < IDM_BASE + 100) {
            for (auto& cat : MENU)
                for (auto& item : cat.items)
                    if (item.id == id) {
                        item.fn();
                        return 0;
                    }
        }
        return 0;
    }
    case WM_NOTIFY:
        return 0;
    case WM_MOUSEWHEEL: {
        // 光标在左栏 → 左栏滚动；否则输出区滚动
        POINT pt; GetCursorPos(&pt);
        RECT lr; GetWindowRect(g_leftPanel, &lr);
        if (PtInRect(&lr, pt)) SendMessageW(g_leftPanel, WM_MOUSEWHEEL, w, l);
        else { SetFocus(g_out); SendMessageW(g_out, WM_MOUSEWHEEL, w, l); }
        return 0;
    }
    case WM_APP_JOBMSG: {
        JobMsg* jm = (JobMsg*)w;
        if (jm->kind == 0) OutCmd(jm->text);
        else if (jm->kind == 1) OutTxt(jm->text);
        else OutErr(jm->text);
        delete jm;
        return 0;
    }
    case WM_APP_JOBDONE:
        g_busy = false;
        RefreshStatusAsync();
        // 事件解锁：配置远程弹窗因"远端操作进行中"处于只读态 → 原地解锁并重建远端行（拿最新列表，无闪烁）
        if (g_remoteDlgLocked && g_hRemoteDlg && IsWindow(g_hRemoteDlg))
            RemoteDlgUnlock(g_hRemoteDlg);
        return 0;
    case WM_APP_STATUS:
        g_statusRunning = false;
        RenderStatus((std::vector<std::vector<Seg>>*)w);
        InvalidateRect(g_leftPanel, nullptr, FALSE);   // g_flowBlock 可能变化，左栏灰/亮态同步重绘
        return 0;
    case WM_APP_UPDATE: {
        // 检查更新结果：弹窗开着→结果集成进弹窗（新版本号+升级内容+下载按钮，高度自适应）；
        // 否则自动模式发现新版→弹"关于"（每会话一次），其余静默
        UpdateResult* r = (UpdateResult*)w;
        bool autoMode = l != 0;
        if (g_hAboutDlg && IsWindow(g_hAboutDlg)) {
            if (autoMode && r->kind == 0) g_updateNagged = true;   // 弹窗开着时自动发现新版也算已提示过
            AboutApplyResult(g_hAboutDlg, *r, autoMode);           // 自动结果不写结果区，手动检查才显示
        } else if (autoMode && r->kind == 0 && !g_updateNagged) {
            g_updateNagged = true;   // 本次会话不再自动弹，手动检查仍可用
            ShowAboutDialog(r);
        }
        delete r;
        return 0;
    }
    case WM_APP_DLPCT:
        // 下载进度：写回关于弹窗的下载按钮（弹窗已关则丢弃）
        if (g_hAboutDlg && IsWindow(g_hAboutDlg))
            SetWindowTextW(GetDlgItem(g_hAboutDlg, IDC_ABOUT_DL), (std::to_wstring((int)w) + L"%").c_str());
        return 0;
    case WM_APP_UPGRADE: {
        // 升级结果：ok 时 msg=新 exe 临时路径 → 换身 + 自动重启；失败恢复按钮并弹提示框（弹窗开着挂它，否则挂主窗）
        UpgradeResult* ur = (UpgradeResult*)w;
        std::wstring reason = ur->ok ? DoUpgradeSwap(ur->msg) : ur->msg;
        if (ur->ok && reason.empty()) {
            ExitProcess(0);   // 新 exe 已启动。不能依赖 WM_CLOSE/PostQuitMessage：模态循环会吞掉唯一一条 WM_QUIT，外层消息循环收不到、进程退不掉
        }
        std::wstring t = L"升级失败：" + reason + L"\n可到 github 发布页手动下载安装。";
        if (g_hAboutDlg && IsWindow(g_hAboutDlg)) {
            HWND hBtn = GetDlgItem(g_hAboutDlg, IDC_ABOUT_CHECK);
            EnableWindow(hBtn, TRUE); SetWindowTextW(hBtn, L"检查更新");
            hBtn = GetDlgItem(g_hAboutDlg, IDC_ABOUT_DL);
            if (hBtn) { EnableWindow(hBtn, TRUE); SetWindowTextW(hBtn, L"下载"); }
            g_aboutState.downloading = false;
            MessageBoxW(g_hAboutDlg, t.c_str(), L"检查更新", MB_OK | MB_ICONWARNING);
        } else {
            MessageBoxW(g_main, t.c_str(), L"检查更新", MB_OK | MB_ICONWARNING);
        }
        delete ur;
        return 0;
    }
    case WM_TIMER:
        // 定时自动刷新状态卡：感知工具外部的改动（他处提交/推送/切分支），空闲时才刷新
        if (w == 1 && !g_busy && !g_statusRunning) RefreshStatusAsync();
        // 每 10 分钟自动检查更新：失败静默，发现新版每会话只弹一次
        if (w == 2 && !g_busy) CheckUpdateAsync(true);
        return 0;
    case WM_DESTROY:
        KillTimer(g_main, 1);
        KillTimer(g_main, 2);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================ 入口 ==========================================
static void MakeDpiAware() {
    HMODULE sh = LoadLibraryW(L"shcore.dll");
    if (sh) {
        typedef HRESULT(WINAPI* Fn)(int);
        Fn f = (Fn)GetProcAddress(sh, "SetProcessDpiAwareness");
        if (f) f(2);   // PER_MONITOR_DPI_AWARE
    }
}
static HFONT MkFont(int pt, bool bold, const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return CreateFontW(-MulDiv(pt, dpiY, 72), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}
/**
  ******************************************************************************
  * @brief    启动清理：删除上次升级遗留的 GitMenuTool_old.exe
  * @param    None
  * @retval   None
  * @note     升级时只起了一个延迟 2 秒的 cmd 删除，杀软短暂锁文件会失败遗留；
  *           本函数在新 exe 启动时兜底重试，此时旧进程已退出、锁必释放
  ******************************************************************************
  */
static void CleanupOldExe() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path dir = fs::path(exePath).parent_path();
    std::error_code ec;
    fs::path oldf = fs::path(exePath);
    oldf.replace_extension(L".old");
    for (int i = 0; i < 5 && fs::exists(oldf, ec); ++i) {   // 上次升级的 .old 兜底删除
        fs::remove(oldf, ec);
        if (fs::exists(oldf, ec)) Sleep(300);
    }
    fs::remove(dir / L"GitMenuTool.ver", ec);               // 清理已废弃的升级标记（历史遗留）
}
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInst;
    MakeDpiAware();
    CleanupOldExe();   // 上次升级遗留的 .old 兜底删除
    InitCommonControls();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);   // Shell 对话框需要 COM
    LoadLibraryW(L"Riched20.dll");   // RichEdit 控件库（RICHEDIT_CLASS）
    g_git = FindExe(L"git");
    if (g_git.empty()) { MessageBoxW(nullptr, L"未检测到 Git，请先安装 Git for Windows。", APP_TITLE, MB_ICONERROR); return 1; }
    g_scale = GetDeviceCaps(GetDC(nullptr), LOGPIXELSX) / 96.0;
    g_fontUI = MkFont(10, false, L"Microsoft YaHei UI");
    g_fontUIBold = MkFont(10, true, L"Microsoft YaHei UI");
    g_fontUIBig = MkFont(13, false, L"Microsoft YaHei UI");
    g_fontUISmall = MkFont(9, false, L"Microsoft YaHei UI");
    g_fontMono = MkFont(11, false, L"Consolas");
    g_fontMenu = MkFont(8, false, L"Microsoft YaHei UI");
    g_fontMenuBold = MkFont(8, true, L"Microsoft YaHei UI");
    g_fontSym = MkFont(10, false, L"Segoe UI Symbol");   // ✕/🖊 字形仅在 Segoe UI Symbol，YaHei 画不出（GDI 不回退）
    // 注册左侧面板类
    WNDCLASSW wc = {};
    wc.lpfnWndProc = LeftPanelProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"GIT_LEFT";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);
    // 注册输出区自绘类
    WNDCLASSW wco = {};
    wco.lpfnWndProc = OutProc;
    wco.hInstance = hInst;
    wco.lpszClassName = L"GIT_OUT";
    wco.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wco.hbrBackground = nullptr;
    RegisterClassW(&wco);
    // 主窗口
    WNDCLASSW wcm = {};
    wcm.lpfnWndProc = WndProc;
    wcm.hInstance = hInst;
    wcm.lpszClassName = L"GIT_MENU_MAIN";
    wcm.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcm.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wcm);
    int W = (int)(1080 * g_scale), H = (int)(640 * g_scale);
    int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - W) / 2);
    int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - H) / 2);
    g_main = CreateWindowExW(0, L"GIT_MENU_MAIN",
                             APP_TITLE,   // 标题栏不显示版本，版本在关于弹窗里
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,   // 主窗不擦子控件区，否则白底盖住 RichEdit 黑底
                             x, y, W, H, nullptr, nullptr, hInst, nullptr);
    // 顶栏两个 STATIC（Layout 引用 g_topStatics）
    g_topStatics[0] = MkStatic(g_main, L"操作目录:", 0, 0, 0, 0, 0, g_fontUI);
    g_topStatics[1] = MkStatic(g_main, L">_", 0, 0, 0, 0, 0, g_fontMono);
    BuildControls();
    SetTimer(g_main, 1, 4000, nullptr);          // 每 4 秒自动刷新状态卡
    SetTimer(g_main, 2, 600000, nullptr);        // 每 10 分钟自动检查更新（后台静默）
    CheckUpdateAsync(true);                      // 启动即查一次更新
    ShowWindow(g_main, nCmdShow);
    UpdateWindow(g_main);
    // 记住上次目录
    std::wstring last = LoadLastDir();
    if (!last.empty() && fs::is_directory(fs::path(last))) {
        SetWindowTextW(g_editDir, last.c_str());
        OpenDirFromEdit();
    } else {
        RefreshStatusAsync();
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
