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
#include <string>
#include <vector>
#include <optional>
#include <atomic>
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
#define IDM_BASE      2000

// ============================ 全局 ==========================================
static const wchar_t APP_TITLE[] = L"Git 菜单工具 cpp v1";
static const wchar_t GITEA_HINT[] = L"https://github.com/仓库名.git";
static const wchar_t PRETTY[] = L"format:%C(yellow)%h%C(reset) %C(cyan)%ad%C(reset)%C(auto)%d%C(reset) %s";
static const wchar_t DATEFMT[] = L"format:%m-%d %H:%M";
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
*.sct
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
      L"# ===== Keil MDK 编译产物 =====\r\n*.o\r\n*.d\r\n*.crf\r\n*.axf\r\n*.bin\r\n*.hex\r\n*.elf\r\n*.map\r\n*.lst\r\n*.htm\r\n*.lnp\r\n*.dep\r\n*.sct\r\n*.iex\r\n*.scvd\r\n# 编译输出目录\r\n**/mdk/output/\r\n**/Objects/\r\n**/Listings/\r\n**/build/\r\n**/DebugConfig/\r\n**/RTE/_*/" },
    { L"Keil 个人配置/临时文件（uvprojx 工程文件保留）", true,
      L"# ===== Keil 用户态/临时（每人不同，uvprojx 保留）=====\r\n*.uvguix.*\r\n*.uvoptx\r\n*.bak\r\n*.bak_*" },
    { L"JLink 调试日志", true, L"JLinkLog.txt" },
    { L"AI 工具本地配置（.claude/ 等）", true, L"# ===== AI 工具本地配置 =====\r\n.claude/" },
    { L"系统/编辑器临时文件", true, L"# ===== 系统/编辑器 =====\r\n.DS_Store\r\nThumbs.db\r\n*.swp" },
    { L"Python 缓存/虚拟环境", false, L"# ===== Python 缓存 =====\r\n__pycache__/\r\n*.pyc\r\n.venv*/\r\nvenv/" },
    { L"前端/其它构建目录（dist、node_modules）", false, L"# ===== 其它常见构建目录 =====\r\ndist/\r\nnode_modules/" },
};

static HINSTANCE g_hInst = nullptr;
static HWND g_main = nullptr, g_editDir = nullptr, g_stat = nullptr, g_out = nullptr;
static HWND g_cmdEdit = nullptr, g_leftPanel = nullptr;
static HFONT g_fontUI = nullptr, g_fontUIBold = nullptr, g_fontMono = nullptr;
static HFONT g_fontMenu = nullptr, g_fontMenuBold = nullptr;   // 左侧菜单小两号
static WNDPROC g_dirEditProc = nullptr, g_cmdEditProc = nullptr;
static double g_scale = 1.0;

static std::wstring g_git, g_repo, g_remote, g_statUrl;
static std::atomic<bool> g_busy{ false };
static std::atomic<bool> g_statusRunning{ false };
static bool g_pushAfterRemoteChange = false;   // 更换远端后自动强制首推
static int g_flowBlock = -1;              // 流程卡卡点（0/1/2 任意未完成都锁定左侧按钮，③不可跳过）
static std::wstring g_lastSkip;           // 同一卡点只提示一次
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
// 同步执行一条命令（capture 输出合并、超时强杀、隐藏窗口）
static GitResult RunGit(const std::vector<std::wstring>& argv, const std::wstring& cwd, DWORD timeoutMs) {
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
    DWORD deadline = GetTickCount() + timeoutMs;
    std::string raw; char buf[8192]; DWORD n = 0;
    for (;;) {
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &n, nullptr)) break;
        if (n > 0) {
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
            if (GetTickCount() > deadline) {
                TerminateProcess(pi.hProcess, 1);
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
    if (g_repo.empty()) return L"";
    std::vector<std::wstring> argv = { g_git, L"-C", g_repo };
    for (auto& a : args) argv.push_back(a);
    GitResult r = RunGit(argv, L"", timeoutMs);
    return r.rc == 0 ? r.out : L"";
}
// 组装带编码前缀的 git 命令
static std::vector<std::wstring> G(std::initializer_list<std::wstring> args) {
    std::vector<std::wstring> argv = { g_git, L"-c", L"i18n.logoutputencoding=utf-8" };
    for (auto& a : args) argv.push_back(a);
    return argv;
}
// 默认远端：优先当前分支上游远端；否则第一个远端；无返回空
static std::wstring DetectRemote() {
    if (g_repo.empty()) return L"";
    std::vector<std::wstring> rems;
    for (auto& l : SplitLines(GitOut({ L"remote" }))) {
        std::wstring t = Trim(l);
        if (!t.empty()) rems.push_back(t);
    }
    if (rems.empty()) return L"";
    std::wstring up = Trim(GitOut({ L"rev-parse", L"--abbrev-ref", L"--symbolic-full-name", L"@{u}" }));
    if (!up.empty()) {
        size_t i = up.find(L'/');
        if (i != std::wstring::npos) {
            std::wstring rn = up.substr(0, i);
            for (auto& r : rems) if (r == rn) return rn;
        }
    }
    return rems[0];
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
static void LogAppend(const std::vector<Seg>& segs) {
    LogLine ln;
    for (auto& s : segs) ln.segs.push_back({ s.text, s.color });
    g_log.push_back(std::move(ln));
    if (g_out) {
        UpdateOutScroll(g_out);
        InvalidateRect(g_out, nullptr, FALSE);
    }
}
static void CopyAllLog() {
    // 整段日志复制到剪贴板（输出区聚焦时 Ctrl+C）
    std::wstring all;
    for (auto& ln : g_log)
        for (auto& s : ln.segs) all += s.text;
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
    LogAppend({ SegM(L"[已复制全部日志到剪贴板]", CLR_HINT) });
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
        for (int i = first; i < (int)g_log.size() && y < rc.bottom; ++i, y += lh) {
            int x = pad;
            for (auto& s : g_log[i].segs) {
                if (s.text.empty()) continue;
                SetTextColor(mem, s.color);
                SIZE sz;
                GetTextExtentPoint32W(mem, s.text.c_str(), (int)s.text.size(), &sz);
                TextOutW(mem, x, y, s.text.c_str(), (int)s.text.size());
                x += sz.cx;
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
            case SB_THUMBTRACK: pos = HIWORD(w); break;
        }
        OutScrollTo(h, pos);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int pos = g_logScroll - (int)GET_WHEEL_DELTA_WPARAM(w) * LogLineH() * 3 / WHEEL_DELTA;
        OutScrollTo(h, pos);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(h);
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'C') CopyAllLog();
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
// 单任务串行：逐条执行，非 optional 失败即中止；SPECIAL 命令见 HandleSpecial
static void HandleSpecial(const std::vector<std::wstring>& argv);
static void ExecAsync(std::vector<JobCmd> cmds) {
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) {
        { FILE* f = fopen("C:/Users/liuao/.codebuddy/work/git_tool_cpp/exec_debug.log", "a"); if (f) { fprintf(f, "[ExecAsync] BUSY-REJECT"); fputc(10, f); fclose(f); } }
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
            GitResult r = RunGit(c.argv, repo, 1800 * 1000);
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
        JobMsg* m = new JobMsg{ 1, L"初始化完成（未提交）。下一步按流程卡：② 一键配置ignore 生成忽略规则，"
                                   L"再【提交修改】完成首次提交，最后【配置远程+首次推送】推送到远端。" };
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
// 目录选择对话框
static std::optional<std::wstring> BrowseFolder(HWND parent, const wchar_t* title) {
    BROWSEINFOW bi = { parent, nullptr, nullptr, title, BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE };
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return std::nullopt;
    wchar_t path[MAX_PATH] = L"";
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (!ok || !path[0]) return std::nullopt;
    return std::wstring(path);
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
static void RefreshStatusAsync();
// 状态 worker：探测仓库状态，构建状态卡富文本（含流程卡/分支/上游/改动/远端）
static std::vector<std::vector<Seg>>* BuildStatusSegs() {
    auto* lines = new std::vector<std::vector<Seg>>;
    if (g_repo.empty()) {
        lines->push_back({ SegT(L"（未选定仓库目录，请先在顶部选定并打开）", STAT_RED) });
        g_flowBlock = -1;
        return lines;
    }
    bool inRepo = Trim(GitOut({ L"rev-parse", L"--is-inside-work-tree" })) == L"true";
    if (!inRepo) {
        g_flowBlock = 0;
        std::vector<Seg> flow = { SegT(L"流程：", STAT_TXT, true) };
        flow.push_back(SegT(L" ① 初始化仓库 ✖ ", STAT_RED, false, BG_RED));
        flow.push_back(SegT(L" ② 配置ignore · ", STAT_DIM, false, BG_GRY));
        flow.push_back(SegT(L" ③ 远端+首推 · ", STAT_DIM, false, BG_GRY));
        lines->push_back(flow);
        lines->push_back({ SegT(L"（该目录还不是 git 仓库，点红色卡片可一键初始化）", STAT_DIM) });
        return lines;
    }
    g_remote = DetectRemote();
    std::wstring branch = Trim(GitOut({ L"rev-parse", L"--abbrev-ref", L"HEAD" }));
    if (branch.empty()) branch = L"（空）";
    // 流程三环节状态
    bool s1 = true, s2 = false, s3 = false;
    std::error_code ec;
    fs::path gi = fs::path(g_repo) / L".gitignore";
    s2 = fs::exists(gi, ec) && fs::file_size(gi, ec) > 0;
    std::wstring upstream = Trim(GitOut({ L"rev-parse", L"--abbrev-ref", L"--symbolic-full-name", L"@{u}" }));
    s3 = !g_remote.empty() && !upstream.empty();
    g_flowBlock = !s2 ? 1 : (!s3 ? 2 : -1);   // ①②③ 任一未完成都锁定按钮（③ 强制，不可跳过）
    // 流程行
    std::vector<Seg> flow = { SegT(L"流程：", STAT_TXT, true) };
    struct PillS { const wchar_t* name; bool ok; };
    PillS pills[3] = { { L"① 初始化仓库", s1 }, { L"② 配置ignore", s2 }, { L"③ 远端+首推", s3 } };
    bool blockedMarked = false;
    for (auto& p : pills) {
        if (p.ok) flow.push_back(SegT(L" " + std::wstring(p.name) + L" ✔ ", STAT_GRN, false, BG_GRN));
        else if (!blockedMarked) { blockedMarked = true; flow.push_back(SegT(L" " + std::wstring(p.name) + L" ✖ ", STAT_RED, false, BG_RED)); }
        else flow.push_back(SegT(L" " + std::wstring(p.name) + L" · ", STAT_DIM, false, BG_GRY));
    }
    lines->push_back(flow);
    // 分支行
    lines->push_back({ SegT(L"分支：", STAT_TXT, true), SegT(L" " + branch + L" ", STAT_BLU, true, BG_BLU) });
    // 与上游行
    std::vector<Seg> up = { SegT(L"与上游：", STAT_TXT, true) };
    std::wstring ab = GitOut({ L"rev-list", L"--left-right", L"--count", L"HEAD...@{u}" });
    size_t tab = ab.find(L'\t');
    if (tab != std::wstring::npos) {
        std::wstring ahead = Trim(ab.substr(0, tab)), behind = Trim(ab.substr(tab + 1));
        up.push_back(SegT(L" 领先上游 ", STAT_TXT));
        up.push_back(SegT(L" " + ahead + L" ", STAT_BLU, true, BG_BLU));
        up.push_back(SegT(L" / 落后 ", STAT_TXT));
        up.push_back(SegT(L" " + behind + L" ", STAT_BLU, true, BG_BLU));
    } else {
        up.push_back(SegT(L"无上游跟踪", STAT_DIM));
    }
    lines->push_back(up);
    // 改动行
    std::wstring st = GitOut({ L"status", L"--porcelain" });
    int nTotal = 0, nUntracked = 0;
    for (auto& l : SplitLines(st)) if (!Trim(l).empty()) { ++nTotal; if (l.rfind(L"??", 0) == 0) ++nUntracked; }
    if (nTotal > 0) {
        std::wstring dtxt = L"未提交改动 " + std::to_wstring(nTotal) + L" 处";
        if (nUntracked) dtxt += L"（含未跟踪 " + std::to_wstring(nUntracked) + L"）";
        lines->push_back({ SegT(L"● ", STAT_RED), SegT(L" ⚠ " + dtxt + L" ", STAT_ORG, false, BG_YLW) });
    } else {
        lines->push_back({ SegT(L" ✔ 工作区干净（无未提交改动） ", STAT_GRN, false, BG_GRN) });
    }
    // 远端两行
    if (!g_remote.empty()) {
        lines->push_back({ SegT(L"远端名称(本地别名)：", STAT_TXT, true), SegT(L" " + g_remote + L" ", STAT_TXT, false, BG_GRY) });
        g_statUrl = Trim(GitOut({ L"config", L"--get", L"remote." + g_remote + L".url" }));
        // URL 不用 CFE_LINK 链接样式（下划线盖住 _ 字符、吸附范围异常）；
        // 手型光标与点击由 RichBaseSub 的字符级命中测试处理
        lines->push_back({ SegT(L"远端地址：", STAT_TXT, true), SegT(L"  ", STAT_TXT),
                           SegT(g_statUrl.empty() ? L"（未取到）" : g_statUrl, STAT_BLU),
                           SegT(L" ↗", STAT_BLU) });
    } else {
        g_statUrl.clear();
        lines->push_back({ SegT(L"远端名称(本地别名)：", STAT_TXT, true), SegT(L"（未设置）", STAT_DIM) });
        lines->push_back({ SegT(L"远端地址：", STAT_TXT, true), SegT(L"（未配置，可点左侧【配置远程+首次推送】）", STAT_DIM) });
    }
    return lines;
}
static void RefreshStatusAsync() {
    bool expected = false;
    if (!g_statusRunning.compare_exchange_strong(expected, true)) return;
    std::thread([]() {
        auto* segs = BuildStatusSegs();
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
        Warn(g_main, L"该仓库尚未配置远端。\n请先点【配置远程+首次推送】添加远端并完成首次推送，\n再使用推送/拉取等与远端相关的操作。");
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
static void OpPull()        { Exec({ G({ L"pull", L"--no-edit" }) }); }
static void OpTagList()     { Exec({ G({ L"tag", L"-l" }) }); }
static void OpTagPush()     { if (NeedRemote()) return; Exec({ G({ L"push", g_remote, L"--tags" }) }); }
static void OpRemoteInfo()  { if (NeedRemote()) return; Exec({ G({ L"remote", L"-v" }), G({ L"branch", L"-vv" }) }); }
static void OpRemoteLs()    { if (NeedRemote()) return; Exec({ G({ L"ls-remote", L"--heads", g_remote }) }); }
static void OpStash()       { Exec({ G({ L"stash" }) }); }
static void OpStashPop()    { Exec({ G({ L"stash", L"pop" }) }); }
static void OpBackMain()    { std::vector<JobCmd> v; v.push_back({ { L"SPECIAL", L"back_main" }, false }); ExecAsync(std::move(v)); }

static void OpCommit() {
    if (!EnsureRepo()) return;
    auto msg = AskText(g_main, L"提交修改", L"提交消息（一行，格式 类型(范围): 中文说明）:");
    if (!msg) return;
    if (msg->empty()) { Warn(g_main, L"消息不能为空。"); return; }
    Exec({ G({ L"add", L"-A" }), G({ L"commit", L"-m", *msg }) });
}
static void OpLogAll() {
    Exec({ G({ L"--no-pager", L"log", L"--all", L"--pretty=" + std::wstring(PRETTY), L"--date=" + std::wstring(DATEFMT) }) });
}
static void OpShow() {
    if (!EnsureRepo()) return;
    auto sha = AskText(g_main, L"查看某次提交", L"提交号（留空=看最近一次）:");
    if (!sha) return;
    if (sha->empty()) Exec({ G({ L"--no-pager", L"show" }) });
    else Exec({ G({ L"--no-pager", L"show", *sha }) });
}
// 推送核心序列（OpPush 与"更换远端后首推"共用）：无上游→fetch 后检查无关历史并询问强推
static void DoPushCore(bool withTags) {
    if (!EnsureRepo() || NeedRemote()) return;
    std::wstring up = Trim(GitOut({ L"rev-parse", L"--abbrev-ref", L"--symbolic-full-name", L"@{u}" }));
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
    } else if (up.empty()) {
        cmds.push_back(G({ L"push", L"-u", g_remote, br }));
    } else {
        cmds.push_back(G({ L"push" }));
    }
    if (withTags) cmds.push_back(G({ L"push", g_remote, L"--tags" }));
    ExecV(cmds);
}
static void OpPush() {
    if (!EnsureRepo() || NeedRemote()) return;
    std::wstring up = Trim(GitOut({ L"rev-parse", L"--abbrev-ref", L"--symbolic-full-name", L"@{u}" }));
    if (!up.empty()) { Exec({ G({ L"push" }) }); return; }
    DoPushCore(false);
}
static void OpInitRepo() {
    if (!EnsureRepo()) return;
    if (Trim(GitOut({ L"rev-parse", L"--is-inside-work-tree" })) == L"true") { Info(g_main, L"该目录已经是 git 仓库，无需初始化。"); return; }
    OutSep(); OutCmd(L"一键初始化: " + g_repo);
    std::vector<JobCmd> v; v.push_back({ { L"SPECIAL", L"init" }, false });
    ExecAsync(std::move(v));
}
static void OpRemoteAdd() {
    if (!EnsureRepo()) return;
    auto url = AskText(g_main, L"配置远程+首次推送", L"远程仓库地址:", GITEA_HINT);
    if (!url || url->empty()) return;
    std::wstring br = CurBranch();
    std::vector<std::vector<std::wstring>> cmds;
    if (!g_remote.empty()) {
        cmds.push_back(G({ L"remote", L"set-url", g_remote, *url }));
    } else {
        std::wstring rn = RemoteFromUrl(*url);
        cmds.push_back(G({ L"remote", L"add", rn, *url }));
        g_remote = rn;
    }
    cmds.push_back(G({ L"push", L"-u", g_remote, br }));
    cmds.push_back(G({ L"push", g_remote, L"--tags" }));
    ExecV(cmds);
}
static void OpRemoteUrl() {
    if (!EnsureRepo() || NeedRemote()) return;
    std::vector<std::wstring> rems;
    for (auto& l : SplitLines(GitOut({ L"remote" }))) { std::wstring t = Trim(l); if (!t.empty()) rems.push_back(t); }
    if (rems.empty()) return;
    std::wstring rn = g_remote;
    if (rems.size() > 1) {
        std::wstring hint;
        for (auto& r : rems) { if (!hint.empty()) hint += L", "; hint += r; }
        auto pick = AskText(g_main, L"更换远端地址", Fmt(L"要修改的远端名(%s):", hint.c_str()), g_remote);
        if (!pick || pick->empty()) return;
        bool found = false;
        for (auto& r : rems) if (r == *pick) { found = true; rn = *pick; }
        if (!found) { Warn(g_main, Fmt(L"远端 %s 不存在（现有: %s）", pick->c_str(), hint.c_str())); return; }
    }
    auto url = AskText(g_main, L"更换远端地址", L"新的仓库地址:");
    if (!url || url->empty()) return;
    g_pushAfterRemoteChange = true;   // 改完地址后自动强制首推（用户要求，不再询问）
    ExecV({ G({ L"remote", L"set-url", rn, *url }), G({ L"remote", L"-v" }) });
}
static void OpRemoteRename() {
    if (!EnsureRepo() || NeedRemote()) return;
    std::vector<std::wstring> rems;
    for (auto& l : SplitLines(GitOut({ L"remote" }))) { std::wstring t = Trim(l); if (!t.empty()) rems.push_back(t); }
    if (rems.empty()) return;
    std::wstring old = rems[0];
    if (rems.size() > 1) {
        std::wstring hint;
        for (auto& r : rems) { if (!hint.empty()) hint += L", "; hint += r; }
        auto pick = AskText(g_main, L"远端改名(本地别名)", Fmt(L"本仓库有多个远端(%s)，要改哪个:", hint.c_str()), g_remote);
        if (!pick || pick->empty()) return;
        old = *pick;
    }
    auto nw = AskText(g_main, L"远端改名(本地别名)", Fmt(L"现有远端名(本地别名): %s\n新远端名:", old.c_str()));
    if (!nw || nw->empty()) return;
    Exec({ G({ L"remote", L"rename", old, *nw }) });
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
    if (!Confirm(g_main, Fmt(L"确认删除本地标签 [%s]？", name->c_str()))) return;
    std::vector<std::vector<std::wstring>> cmds;
    cmds.push_back(G({ L"tag", L"-d", *name }));
    if (!g_remote.empty() && Confirm(g_main, Fmt(L"是否同时删除远端（%s）上的同名标签？", g_remote.c_str())))
        cmds.push_back(G({ L"push", g_remote, L":refs/tags/" + *name }));
    ExecV(cmds);
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
    auto name = AskText(g_main, L"修改全局配置", L"全局 user.name（提交署名，对所有仓库生效）:",
                        Trim(GitOut({ L"config", L"--global", L"user.name" })));
    if (!name) return;
    auto email = AskText(g_main, L"修改全局配置", L"全局 user.email（提交邮箱）:",
                         Trim(GitOut({ L"config", L"--global", L"user.email" })));
    if (!email) return;
    std::vector<std::vector<std::wstring>> cmds;
    if (!name->empty())   cmds.push_back(G({ L"config", L"--global", L"user.name", *name }));
    if (!email->empty())  cmds.push_back(G({ L"config", L"--global", L"user.email", *email }));
    if (cmds.empty()) return;
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
    GitResult r = RunGit(G({ L"clone", *url, target.wstring() }), *parentDir, 600000);
    for (auto& ln : SplitLines(r.out)) if (!Trim(ln).empty()) OutTxt(ln);
    if (r.rc != 0 || !fs::exists(target / L".git", ec)) {
        OutErr(L"克隆失败，请检查地址/账号/网络");
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
static void OpResetGit() {
    if (!EnsureRepo()) return;
    fs::path gitdir = fs::path(g_repo) / L".git";
    std::error_code ec;
    if (!fs::is_directory(gitdir, ec)) { Info(g_main, L"该目录还没有 Git 历史，无需清空。"); return; }
    if (g_busy) { Info(g_main, L"有操作正在运行，请等它完成后再清空。"); return; }
    std::wstring note = L"【危险】将删除该目录的 .git：\n"
                        L"· 全部提交历史消失（含未推送的提交，reflog 也救不回）\n"
                        L"· 远端配置、分支、标签全部消失\n"
                        L"· 目录里的代码/文档文件全部保留\n";
    if (!g_remote.empty()) note += L"· 该仓库曾推送过远端：清空后重推必须强制覆盖远端旧历史\n";
    note += L"\n确认清空请输入大写 Y（其他输入或留空=取消）：";
    auto y = AskText(g_main, L"清空Git重来（危险操作）", note.c_str());
    if (!y || *y != L"Y") return;
    fs::remove_all(gitdir, ec);
    if (ec) { Warn(g_main, std::wstring(L"删除 .git 失败：\n") + GbkToWide(ec.message())); return; }
    g_remote.clear();
    OutSep(); OutTxt(L"已清空 Git 历史（.git 已删除），所有文件保留未动。");
    OutTxt(L"需要重来可点【✚ 初始化仓库】。");
    RefreshStatusAsync();
}
// ============================ 左侧菜单表 ====================================
enum {
    IDM_REMOTE_ADD = IDM_BASE, IDM_REMOTE_URL, IDM_REMOTE_RENAME, IDM_REMOTE_LS,
    IDM_CLONE, IDM_STATUS, IDM_DIFF, IDM_COMMIT, IDM_PUSH, IDM_PULL, IDM_LOG_ALL, IDM_SHOW, IDM_REMOTE_INFO,
    IDM_TAG_LIST, IDM_TAG_NEW, IDM_TAG_PUSH, IDM_TAG_DEL,
    IDM_BRANCH_SW, IDM_BRANCH_NEW, IDM_BRANCH_MERGE, IDM_BRANCH_DEL, IDM_CHECKOUT_REF, IDM_BACK_MAIN, IDM_STASH, IDM_STASH_POP,
    IDM_RESTORE_STAGED, IDM_RESTORE_FILE, IDM_DISCARD_ALL, IDM_UNDO_LAST, IDM_AMEND, IDM_REVERT,
    IDM_CONFLICT_LIST, IDM_NOTEPAD, IDM_MARK_RESOLVED, IDM_TAKE_OURS, IDM_TAKE_THEIRS, IDM_ABORT_MERGE,
};
struct MenuItem { int id; const wchar_t* name; void (*fn)(); };
struct MenuCat  { const wchar_t* title; std::vector<MenuItem> items; };
static const MenuCat MENU[] = {
    { L"远端", {
        { IDM_REMOTE_ADD,    L"配置远程+首次推送", OpRemoteAdd },
        { IDM_REMOTE_URL,    L"更换远端地址",     OpRemoteUrl },
        { IDM_REMOTE_RENAME, L"远端改名(本地别名)", OpRemoteRename },
        { IDM_REMOTE_LS,     L"验证远端连通",     OpRemoteLs },
    } },
    { L"日常", {
        { IDM_CLONE,        L"克隆仓库",   OpClone },
        { IDM_STATUS,       L"查看状态",   OpStatus },
        { IDM_DIFF,         L"查看改动",   OpDiff },
        { IDM_COMMIT,       L"提交修改",   OpCommit },
        { IDM_PUSH,         L"推送(commit)", OpPush },
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

// ============================ 一键配置 ignore ===============================
static void OpIgnoreCfg() {
    if (!EnsureRepo()) return;
    int n = (int)(sizeof(GI_OPTIONS) / sizeof(GI_OPTIONS[0]));
    // TODO: 下轮补勾选弹窗——先按默认勾选直接生成（覆盖需确认），功能等价
    fs::path gi = fs::path(g_repo) / L".gitignore";
    std::error_code ec;
    if (fs::exists(gi, ec) && !Confirm(g_main, L"已存在 .gitignore，覆盖它吗？\n（覆盖前可先点【>_ 打开 .gitignore】查看现有内容）")) return;
    std::wstring text;
    for (int i = 0; i < n; ++i) {
        if (!GI_OPTIONS[i].dft) continue;
        if (!text.empty()) text += L"\r\n\r\n";
        text += GI_OPTIONS[i].rules;
    }
    std::ofstream f(gi, std::ios::binary | std::ios::trunc);
    std::string u = WideToUtf8(text);
    f.write(u.data(), (std::streamsize)u.size());
    f.close();
    OutTxt(L"已按默认勾选生成 .gitignore。");
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
// 流程①②③任一未完成时锁定破坏类按钮；以下三类豁免：
// ① 只读查看类（无害且便于诊断）② 导航/恢复类（防流程锁死：新建分支后必须能切回/能 stash）
// ③ 完成首推所需（新分支上提交+推送后 ③ 变绿，其余按钮自动解锁）
static bool FlowAllowed(int id) {
    switch (id) {
    // 只读查看类
    case IDM_STATUS: case IDM_DIFF: case IDM_LOG_ALL: case IDM_SHOW:
    case IDM_REMOTE_INFO: case IDM_TAG_LIST: case IDM_REMOTE_LS:
    // 导航与恢复类（防死锁）
    case IDM_BRANCH_SW: case IDM_BACK_MAIN: case IDM_CHECKOUT_REF:
    case IDM_STASH: case IDM_STASH_POP:
    // 完成首推所需
    case IDM_COMMIT: case IDM_PUSH: case IDM_PULL:
        return true;
    }
    return false;
}
static void FlowGate(void (*fn)(), const wchar_t* name, int id) {
    if (g_flowBlock != -1 && !FlowAllowed(id)) {   // 流程①②③任一未完成：锁定（只读除外）
        if (g_lastSkip != name) {
            g_lastSkip = name;
            OutTxt(L"流程未完成：先解决流程卡红色环节（点红卡可直接修复），【" + std::wstring(name) + L"】等后续操作暂被锁定。");
        }
        return;
    }
    fn();
}

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
                bool hov = (i == g_leftHover), prs = (i == g_leftPressed);
                RECT br = { 4, y, 4 + bw, y + it.h };
                HBRUSH fill = CreateSolidBrush(prs ? RGB(0xdc, 0xe9, 0xfd)
                                                   : hov ? RGB(0xe8, 0xf0, 0xfe) : RGB(0xff, 0xff, 0xff));
                FillRect(mem, &br, fill);
                DeleteObject(fill);
                HBRUSH frame = CreateSolidBrush((hov || prs) ? RGB(0x1a, 0x73, 0xe8) : RGB(0xad, 0xad, 0xad));
                FrameRect(mem, &br, frame);
                DeleteObject(frame);
                SelectObject(mem, g_fontMenu);
                SetTextColor(mem, RGB(0x1a, 0x1a, 0x1a));
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
        int hit = LeftHitTest(h, (short)HIWORD(l));
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
    // 状态按钮行
    MkBtn(g_main, L"⟳ 刷新状态", IDC_REFRESH, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"⚙ 修改全局配置", IDC_GLOBALCFG, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"✚ 初始化仓库", IDC_INIT, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"⤓ 一键配置ignore", IDC_IGNORE, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L">_ 打开 .gitignore", IDC_OPENIGN, 0, 0, 0, 0, g_fontUI);
    MkBtn(g_main, L"清空Git重来", IDC_RESET, 0, 0, 0, 0, g_fontUI);
    // 状态卡（RichEdit）+ 输出区（自绘黑底日志）
    g_stat = NewRich(g_main, (HMENU)IDC_STAT, RGB(0xff, 0xff, 0xff));
    g_out  = CreateWindowExW(0, L"GIT_OUT", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL,
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
    int topH = (int)(32 * g_scale), rowH = (int)(30 * g_scale);
    int statH = (int)(150 * g_scale), cmdH = (int)(28 * g_scale);
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
    MoveWindow(g_topStatics[0], lblX, m + 5, lblW, lblH, TRUE);
    int entryX = lblX + lblW + (int)(4 * g_scale);
    int entryR = W - (int)(230 * g_scale) - (int)(8 * g_scale);   /* 「打开」按钮左缘留 8px */
    if (entryR < entryX + (int)(120 * g_scale)) entryR = entryX + (int)(120 * g_scale);
    MoveWindow(g_editDir, entryX, m, entryR - entryX, (int)(24 * g_scale), TRUE);
    // 打开 / 清空输出 放右端
    HWND open = GetDlgItem(g_main, IDC_PICKDIR), clear = GetDlgItem(g_main, IDC_CLEAROUT);
    MoveWindow(open, W - (int)(230 * g_scale), m, (int)(64 * g_scale), (int)(24 * g_scale), TRUE);
    MoveWindow(clear, W - (int)(158 * g_scale), m, (int)(90 * g_scale), (int)(24 * g_scale), TRUE);
    // 左栏
    MoveWindow(g_leftPanel, 0, topH + m, leftW, H - topH - m, TRUE);
    UpdateLeftScroll(g_leftPanel);
    {   // 窗口变高后钳制滚动位置，滑块与内容保持一致
        RECT lrc; GetClientRect(g_leftPanel, &lrc);
        int maxScroll = g_leftTotalH - (lrc.bottom - lrc.top);
        if (maxScroll < 0) maxScroll = 0;
        if (g_leftScroll > maxScroll) { g_leftScroll = maxScroll; InvalidateRect(g_leftPanel, nullptr, FALSE); }
    }
    int rightX = leftW + m, rightW = W - rightX - m;
    // 状态按钮行：宽度按文字实测分配，避免小字被截断
    int y = topH + m;
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
    SetWindowTextW(g_stat, L"");
    RECT rc; GetClientRect(g_stat, &rc);
    (void)rc;
    for (auto& ln : *lines) RichAppendLine(g_stat, ln);
    delete lines;
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
        if (id == IDC_CLEAROUT) { g_log.clear(); g_logScroll = 0; UpdateOutScroll(g_out); InvalidateRect(g_out, nullptr, FALSE); return 0; }
        if (id == IDC_CMDRUN) { RunCmdLine(); return 0; }
        if (id == IDC_REFRESH) { RefreshStatusAsync(); return 0; }
        if (id == IDC_GLOBALCFG) { OpGlobalCfg(); return 0; }
        if (id == IDC_INIT) { OpInitRepo(); return 0; }
        if (id == IDC_IGNORE) { OpIgnoreCfg(); return 0; }
        if (id == IDC_OPENIGN) { OpOpenIgnore(); return 0; }
        if (id == IDC_RESET) { OpResetGit(); return 0; }
        if (id >= IDM_BASE && id < IDM_BASE + 100) {
            for (auto& cat : MENU)
                for (auto& item : cat.items)
                    if (item.id == id) {
                        FlowGate(item.fn, item.name, id);
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
        if (g_pushAfterRemoteChange) {
            g_pushAfterRemoteChange = false;
            OutTxt(L"已更换远端地址，自动推送当前分支和标签到新远端……");
            DoPushCore(true);
        }
        return 0;
    case WM_APP_STATUS:
        g_statusRunning = false;
        RenderStatus((std::vector<std::vector<Seg>>*)w);
        return 0;
    case WM_TIMER:
        // 定时自动刷新状态卡：感知工具外部的改动（他处提交/推送/切分支），空闲时才刷新
        if (w == 1 && !g_busy && !g_statusRunning) RefreshStatusAsync();
        return 0;
    case WM_DESTROY:
        KillTimer(g_main, 1);
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
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInst;
    MakeDpiAware();
    InitCommonControls();
    LoadLibraryW(L"Riched20.dll");   // RichEdit 控件库（RICHEDIT_CLASS）
    g_git = FindExe(L"git");
    if (g_git.empty()) { MessageBoxW(nullptr, L"未检测到 Git，请先安装 Git for Windows。", APP_TITLE, MB_ICONERROR); return 1; }
    g_scale = GetDeviceCaps(GetDC(nullptr), LOGPIXELSX) / 96.0;
    g_fontUI = MkFont(10, false, L"Microsoft YaHei UI");
    g_fontUIBold = MkFont(10, true, L"Microsoft YaHei UI");
    g_fontMono = MkFont(11, false, L"Consolas");
    g_fontMenu = MkFont(8, false, L"Microsoft YaHei UI");
    g_fontMenuBold = MkFont(8, true, L"Microsoft YaHei UI");
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
    g_main = CreateWindowExW(0, L"GIT_MENU_MAIN", APP_TITLE,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,   // 主窗不擦子控件区，否则白底盖住 RichEdit 黑底
                             x, y, W, H, nullptr, nullptr, hInst, nullptr);
    // 顶栏两个 STATIC（Layout 引用 g_topStatics）
    g_topStatics[0] = MkStatic(g_main, L"操作目录:", 0, 0, 0, 0, 0, g_fontUI);
    g_topStatics[1] = MkStatic(g_main, L">_", 0, 0, 0, 0, 0, g_fontMono);
    BuildControls();
    SetTimer(g_main, 1, 4000, nullptr);   // 每 4 秒自动刷新状态卡
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
