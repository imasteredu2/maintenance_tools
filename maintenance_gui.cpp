#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wininet.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <ctime>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "wininet.lib")

struct Job { std::string name; }; // Simplified for GUI listing
static const char* CONFIG_FILE = "config.txt";
static const char* SHORTCUT_FILE = "shortcuts.txt"; // persistence for shortcuts
static const char* THEME_FILE = "theme.txt"; // theme preference
static const char* SETTINGS_FILE = "settings.txt"; // application settings

// Settings structure
struct AppSettings {
    bool autoUpdateAt24h = true; // Auto-update when 24hr check finds new version
    int updateCheckIntervalHours = 12; // Check for updates every N hours (default 12)
};

AppSettings appSettings;

// Forward declarations
std::string trim(const std::string& s);
void toggleDeleteMode();
void performDeleteSelected();
void toggleEditMode();
void performEdit(size_t idx);
void loadSettings();
void saveSettings();

int compareVersions(const std::string& a, const std::string& b){
    int am=0, an=0, ap=0; int bm=0, bn=0, bp=0;
    sscanf_s(a.c_str(), "%d.%d.%d", &am, &an, &ap);
    sscanf_s(b.c_str(), "%d.%d.%d", &bm, &bn, &bp);
    if(am!=bm) return (am<bm)?-1:1; if(an!=bn) return (an<bn)?-1:1; if(ap!=bp) return (ap<bp)?-1:1; return 0;
}

// Version and helper dialogs
std::string getCurrentVersion(){
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    auto dir = p.parent_path();
    std::string v = "0.0.0";
    std::ifstream in((dir/"version.txt").string());
    if(in.is_open()){
        std::string line; if(std::getline(in, line)) v = trim(line);
    }
    return v;
}

void showAbout(){
    std::string msg = std::string("Maintenance Tool GUI\n") +
                      "Version: " + getCurrentVersion() + "\n\n" +
                      "C++ Win32 GUI for system tasks and backups.\n" +
                      "Build date: " __DATE__ " " __TIME__ "\n";
    MessageBoxA(NULL, msg.c_str(), "About", MB_OK|MB_ICONINFORMATION);
}

void checkForUpdates(){
    // Placeholder latest version from local file built/latest_version.txt
    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(exePath).parent_path();
    std::string current = getCurrentVersion();
    std::string latest = current;
    std::ifstream in((dir/"built"/"latest_version.txt").string());
    if(in.is_open()){
        std::string line; if(std::getline(in, line)) latest = trim(line);
    }
    if(compareVersions(current, latest) >= 0){
        std::string msg = "You are up to date.\n\nCurrent: " + current + "\nLatest: " + latest;
        MessageBoxA(NULL, msg.c_str(), "Check for Updates", MB_OK|MB_ICONINFORMATION);
        return;
    }
    std::string newExe = (dir/"built"/("maintenance_tool_gui_v"+latest+".exe")).string();
    std::string updater = (dir/"updater.exe").string();
    std::string prompt = "New version available!\n\nCurrent: "+current+"\nLatest: "+latest+"\n\nInstall now?";
    if(MessageBoxA(NULL, prompt.c_str(), "Update Available", MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
    
    // Launch updater to replace this executable, then exit
    std::string cmd = '"' + updater + '"' + " --target " + '"' + (dir/"maintenance_tool_gui.exe").string() + '"' +
                      " --source " + '"' + newExe + '"' + " --relaunch";
    STARTUPINFOA si{sizeof(si)}; PROCESS_INFORMATION pi{}; std::string cpy=cmd;
    if(CreateProcessA(NULL, cpy.data(), NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)){
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        PostQuitMessage(0);
    } else {
        MessageBoxA(NULL, "Failed to launch updater.exe", "Update Error", MB_OK|MB_ICONERROR);
    }
}

std::string trim(const std::string& s){ size_t b=0,e=s.size(); while(b<e && isspace((unsigned char)s[b]))++b; while(e>b && isspace((unsigned char)s[e-1]))--e; return s.substr(b,e-b);} 

void loadSettings() {
    appSettings.autoUpdateAt24h = true;
    appSettings.updateCheckIntervalHours = 12;
    
    std::ifstream in(SETTINGS_FILE);
    if(!in.is_open()) return;
    
    std::string line;
    while(std::getline(in, line)) {
        line = trim(line);
        if(line.empty() || line[0] == '#') continue;
        
        size_t eqPos = line.find('=');
        if(eqPos == std::string::npos) continue;
        
        std::string key = trim(line.substr(0, eqPos));
        std::string value = trim(line.substr(eqPos + 1));
        
        if(key == "auto_update_at_24h") {
            appSettings.autoUpdateAt24h = (value == "1" || value == "true");
        } else if(key == "update_check_interval_hours") {
            try {
                int val = std::stoi(value);
                if(val >= 1 && val <= 24) appSettings.updateCheckIntervalHours = val;
            } catch(...) {}
        }
    }
    in.close();
}

void saveSettings() {
    std::ofstream out(SETTINGS_FILE);
    if(!out.is_open()) return;
    
    out << "# Application Settings\n";
    out << "auto_update_at_24h=" << (appSettings.autoUpdateAt24h ? "1" : "0") << "\n";
    out << "update_check_interval_hours=" << appSettings.updateCheckIntervalHours << "\n";
    
    out.close();
}

std::vector<Job> loadJobs(){ std::vector<Job> jobs; std::ifstream in(CONFIG_FILE); if(!in.is_open()) return jobs; std::string line; Job current; bool inJob=false; while(std::getline(in,line)){ line=trim(line); if(line.empty()) continue; if(line.rfind("[job:",0)==0 && line.back()==']'){ if(inJob){ jobs.push_back(current); current=Job(); } current.name=trim(line.substr(5,line.size()-6)); inJob=true; } } if(inJob) jobs.push_back(current); return jobs; }

void runCommand(const std::string& cmd){ STARTUPINFOA si{sizeof(si)}; PROCESS_INFORMATION pi{}; std::string cpy=cmd; if(CreateProcessA(NULL, cpy.data(), NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)){ CloseHandle(pi.hThread); WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hProcess);} }

// Execute maintenance_tool.exe with given args
void toolInvoke(const std::string& args){ char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH); std::filesystem::path p(exePath); auto dir=p.parent_path(); std::string full = '"' + (dir/"maintenance_tool.exe").string() + '"' + ' ' + args; runCommand(full); }

HWND hList; HWND hStatus; HWND hShortcutsPanel;
HWND hBtnShutdown, hBtnRestart, hBtnLogoff, hBtnUpdateRestart;
HWND hBtnAdd, hBtnSave, hBtnEdit, hBtnDelete; // Shortcut management buttons
HWND hBtnBackup, hBtnRestore, hBtnStatus; // Backup tab main buttons
HWND hBtnCreate, hBtnEditJob, hBtnDeleteJob, hBtnRefresh; // Backup tab job buttons
HWND hBtnCombine, hBtnCheckUpdate; // Backup tab utility buttons
HBRUSH hBrushRed, hBrushYellow, hBrushOrange, hBrushBlue, hBrushGreen;
std::vector<std::pair<std::string, HWND>> shortcuts; // path, hwnd
std::vector<HWND> shortcutChecks; // checkboxes for delete mode
std::vector<HICON> shortcutIcons; // icons for shortcuts
int nextShortcutId = 100;
int nextShortcutY = 10;
HWND hMainWindow = NULL;
HWND hTabControl;
HWND hHomeTab, hBackupTab;
bool deleteMode = false;
bool editMode = false;
bool darkTheme = false;
HBRUSH hBrushBg, hBrushBgDark, hBrushControlBg, hBrushControlBgDark;

void refreshJobs(){ auto jobs = loadJobs(); SendMessage(hList, LB_RESETCONTENT, 0, 0); for(auto& j: jobs){ SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)j.name.c_str()); } SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"Jobs refreshed."); }

// Reposition shortcut buttons/checkboxes within the shortcuts panel
void layoutShortcutsUI(){
    if(!hShortcutsPanel || !hMainWindow) return;
    RECT rcPanel; GetWindowRect(hShortcutsPanel, &rcPanel);
    POINT pt = {rcPanel.left, rcPanel.top};
    ScreenToClient(hMainWindow, &pt);
    
    // Get panel size to constrain buttons within
    int panelW = rcPanel.right - rcPanel.left;
    int panelH = rcPanel.bottom - rcPanel.top;
    int btnW = panelW - 40; // Leave margin on sides
    if(btnW < 200) btnW = 200;
    
    nextShortcutY = 10;
    for(size_t i=0;i<shortcuts.size();++i){
        int baseX = pt.x + 20;  // 20px margin from left edge of panel
        int baseY = pt.y + 10 + (int)i * 35;
        
        // Only show if within panel bounds
        if(baseY + 30 <= pt.y + panelH) {
            SetWindowPos(shortcuts[i].second, NULL, baseX, baseY, btnW, 30, SWP_NOZORDER|SWP_SHOWWINDOW);
        } else {
            ShowWindow(shortcuts[i].second, SW_HIDE);
        }
        
        if(i < shortcutChecks.size() && shortcutChecks[i]){
            int chkX = baseX - 20;  // Position checkbox to the left of button, within panel
            int chkY = baseY + 7;
            // Bring checkbox to front and ensure it's visible within bounds
            SetWindowPos(shortcutChecks[i], HWND_TOP, chkX, chkY, 16, 16, SWP_SHOWWINDOW);
            if(!deleteMode && !editMode) ShowWindow(shortcutChecks[i], SW_HIDE);
        }
        nextShortcutY += 35;
    }
}

// Re-layout controls on resize to anchor to edges
void layoutAllUI(int clientW, int clientH){
    if(!hMainWindow) return;
    const int margin = 20;
    const int statusH = 20;
    const int tabH = 30;
    int tabY = 10;
    int availableW = clientW - 2*margin;
    if(availableW < 200) availableW = 200;
    MoveWindow(hTabControl, margin, tabY, availableW, tabH, TRUE);
    int y = tabY + tabH + 10;
    // Row 1 system buttons
    int btnW = 85, btnH = 35;
    int gap = (availableW - 4*btnW) / 3; if(gap < 5) gap = 5;
    int x = margin;
    MoveWindow(hBtnLogoff, x, y, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(hBtnUpdateRestart, x, y, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(hBtnRestart, x, y, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(hBtnShutdown, x, y, btnW, btnH, TRUE);
    // Lock button
    y += btnH + 5;
    MoveWindow(GetDlgItem(hMainWindow, 16), margin, y, availableW, 30, TRUE);
    // Shortcut management row
    y += 30 + 10;
    btnW = 85; btnH = 30; gap = (availableW - 4*btnW) / 3; if(gap < 5) gap = 5; x = margin;
    MoveWindow(GetDlgItem(hMainWindow, 8), x, y, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 13), x, y, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 14), x, y, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 15), x, y, btnW, btnH, TRUE);
    // Shortcuts label and panel
    y += btnH + 10;
    HWND hLblShortcuts = GetDlgItem(hMainWindow, 17);
    if(hLblShortcuts) MoveWindow(hLblShortcuts, margin, y, availableW, 20, TRUE);
    y += 20;
    int panelH = clientH - y - statusH - margin; if(panelH < 120) panelH = 120;
    MoveWindow(hShortcutsPanel, margin, y, availableW, panelH, TRUE);
    // Backup tab controls mirror positions
    int backupY = tabY + tabH + 10;
    btnW = 110; btnH = 35; gap = (availableW - (btnW*3) - 125) / 2; if(gap < 5) gap = 5; x = margin;
    MoveWindow(GetDlgItem(hMainWindow, 2), x, backupY, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 3), x, backupY, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 11), x, backupY, 125, btnH, TRUE);
    backupY += btnH + 10;
    btnW = 85; btnH = 30; gap = (availableW - 4*btnW) / 3; if(gap < 5) gap = 5; x = margin;
    MoveWindow(GetDlgItem(hMainWindow, 9), x, backupY, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 10), x, backupY, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 12), x, backupY, btnW, btnH, TRUE); x += btnW + gap;
    MoveWindow(GetDlgItem(hMainWindow, 1), x, backupY, btnW, btnH, TRUE);
    backupY += btnH + 10;
    MoveWindow(GetDlgItem(hMainWindow, 18), margin, backupY, 160, 30, TRUE);
    MoveWindow(GetDlgItem(hMainWindow, 19), margin + 165, backupY, availableW - 165, 30, TRUE);
    backupY += 30 + 10;
    HWND hLblJobs = NULL; // created without id; skip move
    int listH = clientH - backupY - statusH - margin; if(listH < 120) listH = 120;
    MoveWindow(hList, margin, backupY, availableW, listH, TRUE);
    // Status bar
    MoveWindow(hStatus, 10, clientH - statusH - 10, clientW - 20, statusH, TRUE);
    // Re-layout shortcuts
    layoutShortcutsUI();
}

// Load theme preference
void loadTheme(){
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    auto dir = p.parent_path();
    std::string fullPath = (dir / THEME_FILE).string();
    
    std::ifstream in(fullPath);
    if(in.is_open()) {
        std::string line;
        if(std::getline(in, line)) {
            darkTheme = (trim(line) == "dark");
        }
    }
}

// Save theme preference
void saveTheme(){
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    auto dir = p.parent_path();
    std::string fullPath = (dir / THEME_FILE).string();
    
    std::ofstream out(fullPath, std::ios::trunc);
    if(out.is_open()) {
        out << (darkTheme ? "dark" : "light") << "\n";
    }
}

// Extract icon from executable
HICON extractIcon(const std::string& path) {
    HICON hIcon = NULL;
    ExtractIconExA(path.c_str(), 0, NULL, &hIcon, 1);
    if (!hIcon) {
        // Fallback to large icon
        ExtractIconExA(path.c_str(), 0, &hIcon, NULL, 1);
    }
    if (!hIcon) {
        // Last resort - get associated icon
        SHFILEINFOA sfi = {0};
        SHGetFileInfoA(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON);
        hIcon = sfi.hIcon;
    }
    return hIcon;
}

// Save current shortcut paths to file with nicknames
void saveShortcuts(){
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    auto dir = p.parent_path();
    std::string fullPath = (dir / SHORTCUT_FILE).string();
    
    std::ofstream out(fullPath, std::ios::trunc);
    if(!out.is_open()) {
        MessageBoxA(NULL, ("Failed to save shortcuts to: " + fullPath).c_str(), "Error", MB_OK|MB_ICONERROR);
        return;
    }
    for(auto &sc : shortcuts){
        // Get button text (nickname)
        char btnText[256];
        GetWindowTextA(sc.second, btnText, 256);
        // Save as: path|nickname
        out << sc.first << "|" << btnText << "\n";
    }
    out.flush();
    out.close();
}

// Load shortcuts from file and create buttons
void loadShortcuts(HWND hMain){
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    auto dir = p.parent_path();
    std::string fullPath = (dir / SHORTCUT_FILE).string();
    
    std::ifstream in(fullPath);
    if(!in.is_open()) return; // nothing persisted yet
    std::string line;
    while(std::getline(in, line)){
        line = trim(line);
        if(line.empty()) continue;
        
        // Parse format: path|nickname or just path (legacy)
        std::string path = line;
        std::string name;
        size_t pipePos = line.find('|');
        if(pipePos != std::string::npos) {
            path = line.substr(0, pipePos);
            name = line.substr(pipePos + 1);
        } else {
            // Legacy format - extract name from path
            std::filesystem::path p(line);
            name = p.stem().string();
        }
        // Compute absolute position based on shortcuts panel
        RECT rcPanel; GetWindowRect(hShortcutsPanel, &rcPanel);
        POINT pt = {rcPanel.left, rcPanel.top};
        ScreenToClient(hMainWindow ? hMainWindow : hMain, &pt);
        int baseX = pt.x + (355 - 240)/2;
        int baseY = pt.y + 10 + nextShortcutY; // 10px inset inside panel
        HWND btn = CreateWindow("BUTTON", name.c_str(), WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW,
                    baseX, baseY, 240, 30, hMain, (HMENU)(UINT_PTR)nextShortcutId, NULL, NULL);
        // Prepare a hidden checkbox for delete mode next to button
        int chkX = baseX - 22; // to the left of centered button
        int chkY = baseY + 7; // vertical center alignment
        HWND chk = CreateWindow("BUTTON", "", WS_CHILD|BS_AUTOCHECKBOX,
                chkX, chkY, 16, 16, hMain, NULL, NULL, NULL);
        ShowWindow(chk, SW_HIDE);
        SetWindowPos(btn, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
        
        // Extract and store icon
        HICON hIcon = extractIcon(path);
        shortcutIcons.push_back(hIcon);
        
        shortcuts.push_back({path, btn});
        shortcutChecks.push_back(chk);
        nextShortcutId++;
        nextShortcutY += 35;
    }
}

void switchTab(int index) {
    // Get main window handle
    HWND hwnd = GetParent(hTabControl);
    
    // Always hide checkboxes when switching tabs
    for(auto chk : shortcutChecks) {
        if(chk) ShowWindow(chk, SW_HIDE);
    }
    deleteMode = false;
    editMode = false;
    
    // Enumerate all child windows and show/hide based on tab
    EnumChildWindows(hwnd, [](HWND child, LPARAM lParam) -> BOOL {
        int tabIndex = (int)lParam;
        
        // Skip tab control and status bar
        if (child == hTabControl || child == hStatus) return TRUE;
        
        // Home tab controls: buttons 4-8, shortcuts panel, shortcut buttons/checkboxes (ids >=100), shortcut management 8, 13-17
        int id = GetDlgCtrlID(child);
        
        bool isShortcutChild = (id >= 100); // shortcut buttons use ids starting at 100
        
        bool isHomeControl = (id >= 4 && id <= 8) || (id >= 13 && id <= 17) || child == hShortcutsPanel || isShortcutChild;
        
        // Backup tab controls: buttons 1-3, 9-12, 18, list
        bool isBackupControl = (id >= 1 && id <= 3) || (id >= 9 && id <= 12) || id == 18 || child == hList;
        
        // Check if it's a label
        char className[256];
        GetClassNameA(child, className, sizeof(className));
        bool isStatic = (strcmp(className, "STATIC") == 0 && child != hShortcutsPanel && child != hStatus);
        
        if (tabIndex == 0) {
            // Show home, hide backup
            if (isHomeControl) ShowWindow(child, SW_SHOW);
            if (isBackupControl || id == 19) ShowWindow(child, SW_HIDE);
            if (isStatic) {
                char text[256];
                GetWindowTextA(child, text, sizeof(text));
                if (strstr(text, "Program") || strstr(text, "Shortcut")) ShowWindow(child, SW_SHOW);
                else if (strstr(text, "Backup") || strstr(text, "Job")) ShowWindow(child, SW_HIDE);
            }
        } else {
            // Show backup, hide home
            if (isHomeControl) ShowWindow(child, SW_HIDE);
            if (isBackupControl || id == 19) ShowWindow(child, SW_SHOW);
            if (isStatic) {
                char text[256];
                GetWindowTextA(child, text, sizeof(text));
                if (strstr(text, "Backup") || strstr(text, "Job")) ShowWindow(child, SW_SHOW);
                else if (strstr(text, "Program") || strstr(text, "Shortcut")) ShowWindow(child, SW_HIDE);
            }
        }
        
        return TRUE;
    }, (LPARAM)index);
}

std::string getSelectedJob(){ int idx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0); if(idx == LB_ERR) return ""; char buf[256]; SendMessageA(hList, LB_GETTEXT, idx, (LPARAM)buf); return buf; }

// Structure for individual additional schedule
struct AdditionalSchedule {
    std::string interval;
    std::string value;
    std::string time;
    std::string dest;
    bool onetime;
    std::string lastRun;
};

// Dialog data structure for individual schedule editing
struct ScheduleDialogData {
    std::string interval;
    std::string value;
    std::string time;
    std::string dest;
    bool onetime;
    bool accepted;
};

// Forward declaration
void RunScheduleDialog(HWND parent, ScheduleDialogData* data, const char* title);

// Dialog data structure
struct JobDialogData {
    std::string jobName;
    std::string sources;
    std::string destinations;
    bool override;
    bool incremental;
    bool scheduleEnabled;
    std::string scheduleInterval;
    std::string scheduleValue;
    std::string scheduleTime;
    std::string startDate;     // NEW: first backup start date/time
    std::string scheduleDest;  // NEW: schedule-specific destination
    bool scheduleOneTime;      // NEW: disable after first run
    std::vector<AdditionalSchedule> additionalSchedules;  // NEW: unlimited schedules
    bool accepted;
};

// Window procedure for job dialog
LRESULT CALLBACK JobDialogWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static JobDialogData* data = nullptr;
    
    switch(msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            data = (JobDialogData*)cs->lpCreateParams;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)data);
            return 0;
        }
        
        case WM_COMMAND: {
            data = (JobDialogData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
            if(!data) break;
            
            if(LOWORD(wParam) == 201) { // Browse source
                char folderPath[MAX_PATH] = "";
                BROWSEINFOA bi = {0};
                bi.hwndOwner = hDlg;
                bi.lpszTitle = "Select Source Folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
                if(pidl) {
                    SHGetPathFromIDListA(pidl, folderPath);
                    CoTaskMemFree(pidl);
                    char buf[1024];
                    GetDlgItemTextA(hDlg, 102, buf, sizeof(buf));
                    std::string current(buf);
                    if(!current.empty() && current.back() != '|') current += "|";
                    current += folderPath;
                    SetDlgItemTextA(hDlg, 102, current.c_str());
                }
                return 0;
            }
            else if(LOWORD(wParam) == 202) { // Browse destination
                char folderPath[MAX_PATH] = "";
                BROWSEINFOA bi = {0};
                bi.hwndOwner = hDlg;
                bi.lpszTitle = "Select Destination Folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
                if(pidl) {
                    SHGetPathFromIDListA(pidl, folderPath);
                    CoTaskMemFree(pidl);
                    char buf[1024];
                    GetDlgItemTextA(hDlg, 103, buf, sizeof(buf));
                    std::string current(buf);
                    if(!current.empty() && current.back() != '|') current += "|";
                    current += folderPath;
                    SetDlgItemTextA(hDlg, 103, current.c_str());
                }
                return 0;
            }
            else if(LOWORD(wParam) == IDOK) {
                char buf[1024];
                GetDlgItemTextA(hDlg, 101, buf, sizeof(buf));
                data->jobName = buf;
                GetDlgItemTextA(hDlg, 102, buf, sizeof(buf));
                data->sources = buf;
                GetDlgItemTextA(hDlg, 103, buf, sizeof(buf));
                data->destinations = buf;
                data->override = IsDlgButtonChecked(hDlg, 104) == BST_CHECKED;
                data->incremental = IsDlgButtonChecked(hDlg, 105) == BST_CHECKED;
                data->scheduleEnabled = IsDlgButtonChecked(hDlg, 106) == BST_CHECKED;
                // Get interval from combobox
                HWND hIntervalCombo = GetDlgItem(hDlg, 107);
                int intervalIdx = (int)SendMessageA(hIntervalCombo, CB_GETCURSEL, 0, 0);
                const char* intervals[] = {"mins", "hours", "daily", "weekly", "monthly", "once"};
                data->scheduleInterval = (intervalIdx >= 0 && intervalIdx < 6) ? intervals[intervalIdx] : "daily";
                GetDlgItemTextA(hDlg, 108, buf, sizeof(buf));
                data->scheduleValue = buf;
                GetDlgItemTextA(hDlg, 109, buf, sizeof(buf));
                data->scheduleTime = buf;
                GetDlgItemTextA(hDlg, 112, buf, sizeof(buf));
                data->startDate = buf;
                GetDlgItemTextA(hDlg, 110, buf, sizeof(buf));
                data->scheduleDest = buf;
                data->scheduleOneTime = IsDlgButtonChecked(hDlg, 111) == BST_CHECKED;
                data->accepted = true;
                DestroyWindow(hDlg);
                PostQuitMessage(0);
                return 0;
            }
            else if(LOWORD(wParam) == IDCANCEL) {
                data->accepted = false;
                DestroyWindow(hDlg);
                PostQuitMessage(0);
                return 0;
            }
            else if(LOWORD(wParam) == 114) { // Add Schedule
                ScheduleDialogData schedData;
                schedData.interval = "daily";
                schedData.value = "1";
                schedData.time = "00:00";
                schedData.dest = "";
                schedData.onetime = false;
                schedData.accepted = false;
                
                RunScheduleDialog(hDlg, &schedData, "Add Additional Schedule");
                
                if(schedData.accepted) {
                    AdditionalSchedule newSched;
                    newSched.interval = schedData.interval;
                    newSched.value = schedData.value;
                    newSched.time = schedData.time;
                    newSched.dest = schedData.dest;
                    newSched.onetime = schedData.onetime;
                    newSched.lastRun = "0";
                    
                    data->additionalSchedules.push_back(newSched);
                    
                    // Update list
                    HWND hList = GetDlgItem(hDlg, 113);
                    std::string display = schedData.interval + " (" + schedData.value + ") at " + schedData.time;
                    if(!schedData.dest.empty()) display += " -> " + schedData.dest;
                    if(schedData.onetime) display += " [one-time]";
                    SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
                }
                return 0;
            }
            else if(LOWORD(wParam) == 115) { // Edit Schedule
                HWND hList = GetDlgItem(hDlg, 113);
                int idx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if(idx == LB_ERR || idx >= (int)data->additionalSchedules.size()) {
                    MessageBoxA(hDlg, "Please select a schedule to edit.", "Edit Schedule", MB_OK|MB_ICONINFORMATION);
                    return 0;
                }
                
                AdditionalSchedule& sched = data->additionalSchedules[idx];
                ScheduleDialogData schedData;
                schedData.interval = sched.interval;
                schedData.value = sched.value;
                schedData.time = sched.time;
                schedData.dest = sched.dest;
                schedData.onetime = sched.onetime;
                schedData.accepted = false;
                
                RunScheduleDialog(hDlg, &schedData, "Edit Additional Schedule");
                
                if(schedData.accepted) {
                    sched.interval = schedData.interval;
                    sched.value = schedData.value;
                    sched.time = schedData.time;
                    sched.dest = schedData.dest;
                    sched.onetime = schedData.onetime;
                    
                    // Update list display
                    SendMessage(hList, LB_DELETESTRING, idx, 0);
                    std::string display = schedData.interval + " (" + schedData.value + ") at " + schedData.time;
                    if(!schedData.dest.empty()) display += " -> " + schedData.dest;
                    if(schedData.onetime) display += " [one-time]";
                    SendMessage(hList, LB_INSERTSTRING, idx, (LPARAM)display.c_str());
                    SendMessage(hList, LB_SETCURSEL, idx, 0);
                }
                return 0;
            }
            else if(LOWORD(wParam) == 116) { // Delete Schedule
                HWND hList = GetDlgItem(hDlg, 113);
                int idx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if(idx == LB_ERR || idx >= (int)data->additionalSchedules.size()) {
                    MessageBoxA(hDlg, "Please select a schedule to delete.", "Delete Schedule", MB_OK|MB_ICONINFORMATION);
                    return 0;
                }
                
                data->additionalSchedules.erase(data->additionalSchedules.begin() + idx);
                SendMessage(hList, LB_DELETESTRING, idx, 0);
                return 0;
            }
            break;
        }
        
        case WM_CLOSE:
            data = (JobDialogData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
            if(data) data->accepted = false;
            DestroyWindow(hDlg);
            return 0;
    }
    return DefWindowProcA(hDlg, msg, wParam, lParam);
}

// Job creation/edit dialog procedure
INT_PTR CALLBACK JobDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static JobDialogData* data = nullptr;
    
    switch(msg) {
        case WM_INITDIALOG: {
            data = (JobDialogData*)lParam;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            
            // Set initial values
            SetDlgItemTextA(hDlg, 101, data->jobName.c_str());
            SetDlgItemTextA(hDlg, 102, data->sources.c_str());
            SetDlgItemTextA(hDlg, 103, data->destinations.c_str());
            CheckDlgButton(hDlg, 104, data->override ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, 105, data->incremental ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, 106, data->scheduleEnabled ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, 111, data->scheduleOneTime ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemTextA(hDlg, 107, data->scheduleInterval.c_str());
            SetDlgItemTextA(hDlg, 108, data->scheduleValue.c_str());
            SetDlgItemTextA(hDlg, 109, data->scheduleTime.c_str());
            SetDlgItemTextA(hDlg, 112, data->startDate.c_str());
            SetDlgItemTextA(hDlg, 110, data->scheduleDest.c_str());
            
            return TRUE;
        }
        
        case WM_COMMAND:
            if(LOWORD(wParam) == IDOK) {
                data = (JobDialogData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
                
                char buf[1024];
                GetDlgItemTextA(hDlg, 101, buf, sizeof(buf));
                data->jobName = buf;
                GetDlgItemTextA(hDlg, 102, buf, sizeof(buf));
                data->sources = buf;
                GetDlgItemTextA(hDlg, 103, buf, sizeof(buf));
                data->destinations = buf;
                data->override = IsDlgButtonChecked(hDlg, 104) == BST_CHECKED;
                data->incremental = IsDlgButtonChecked(hDlg, 105) == BST_CHECKED;
                data->scheduleEnabled = IsDlgButtonChecked(hDlg, 106) == BST_CHECKED;
                GetDlgItemTextA(hDlg, 107, buf, sizeof(buf));
                data->scheduleInterval = buf;
                GetDlgItemTextA(hDlg, 108, buf, sizeof(buf));
                data->scheduleValue = buf;
                GetDlgItemTextA(hDlg, 109, buf, sizeof(buf));
                data->scheduleTime = buf;
                data->accepted = true;
                
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            else if(LOWORD(wParam) == IDCANCEL) {
                data = (JobDialogData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
                data->accepted = false;
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// Dialog procedure for individual schedule editing
LRESULT CALLBACK ScheduleDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static ScheduleDialogData* data = nullptr;
    
    switch(msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            data = (ScheduleDialogData*)cs->lpCreateParams;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)data);
            return 0;
        }
        case WM_COMMAND: {
            data = (ScheduleDialogData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
            if(!data) break;
            
            if(LOWORD(wParam) == IDOK) {
                char buf[256];
                // Get interval from combobox
                HWND hIntervalCombo = GetDlgItem(hDlg, 201);
                int intervalIdx = (int)SendMessageA(hIntervalCombo, CB_GETCURSEL, 0, 0);
                const char* intervals[] = {"mins", "hours", "daily", "weekly", "monthly", "once"};
                data->interval = (intervalIdx >= 0 && intervalIdx < 6) ? intervals[intervalIdx] : "daily";
                
                GetDlgItemTextA(hDlg, 202, buf, sizeof(buf));
                data->value = buf;
                GetDlgItemTextA(hDlg, 203, buf, sizeof(buf));
                data->time = buf;
                GetDlgItemTextA(hDlg, 204, buf, sizeof(buf));
                data->dest = buf;
                data->onetime = IsDlgButtonChecked(hDlg, 205) == BST_CHECKED;
                data->accepted = true;
                DestroyWindow(hDlg);
                PostQuitMessage(0);
                return 0;
            }
            else if(LOWORD(wParam) == IDCANCEL) {
                data->accepted = false;
                DestroyWindow(hDlg);
                PostQuitMessage(0);
                return 0;
            }
            break;
        }
    }
    return DefWindowProc(hDlg, msg, wParam, lParam);
}

HWND CreateScheduleDialog(HWND parent, ScheduleDialogData* data, const char* title) {
    static bool registered = false;
    if(!registered) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = ScheduleDialogProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "ScheduleDialogClass";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        registered = true;
    }
    
    HWND hDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "ScheduleDialogClass",
        title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 350,
        parent, NULL, GetModuleHandle(NULL), data
    );
    
    int y = 20;
    
    // Interval
    CreateWindowA("STATIC", "Interval:", WS_CHILD|WS_VISIBLE, 20, y, 400, 20, hDlg, NULL, NULL, NULL);
    y += 24;
    HWND hIntervalCombo = CreateWindowA("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
        20, y, 400, 150, hDlg, (HMENU)201, NULL, NULL);
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"mins");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"hours");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"daily");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"weekly");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"monthly");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"once");
    // Set current value
    int idx = 2; // default daily
    if(data->interval == "mins") idx = 0;
    else if(data->interval == "hours") idx = 1;
    else if(data->interval == "daily") idx = 2;
    else if(data->interval == "weekly") idx = 3;
    else if(data->interval == "monthly") idx = 4;
    else if(data->interval == "once") idx = 5;
    SendMessageA(hIntervalCombo, CB_SETCURSEL, idx, 0);
    y += 35;
    
    // Value
    CreateWindowA("STATIC", "Value (number):", WS_CHILD|WS_VISIBLE, 20, y, 400, 20, hDlg, NULL, NULL, NULL);
    y += 24;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->value.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
        20, y, 400, 24, hDlg, (HMENU)202, NULL, NULL);
    y += 35;
    
    // Time
    CreateWindowA("STATIC", "Time (HH:MM):", WS_CHILD|WS_VISIBLE, 20, y, 400, 20, hDlg, NULL, NULL, NULL);
    y += 24;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->time.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
        20, y, 400, 24, hDlg, (HMENU)203, NULL, NULL);
    y += 35;
    
    // Destination
    CreateWindowA("STATIC", "Destination (optional):", WS_CHILD|WS_VISIBLE, 20, y, 400, 20, hDlg, NULL, NULL, NULL);
    y += 24;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->dest.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
        20, y, 400, 24, hDlg, (HMENU)204, NULL, NULL);
    y += 35;
    
    // One-time checkbox
    CreateWindowA("BUTTON", "One-time (disable after first run)", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
        20, y, 400, 24, hDlg, (HMENU)205, NULL, NULL);
    CheckDlgButton(hDlg, 205, data->onetime ? BST_CHECKED : BST_UNCHECKED);
    y += 35;
    
    // Buttons
    CreateWindowA("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 240, y, 80, 30, hDlg, (HMENU)IDOK, NULL, NULL);
    CreateWindowA("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 330, y, 80, 30, hDlg, (HMENU)IDCANCEL, NULL, NULL);
    
    return hDlg;
}

void RunScheduleDialog(HWND parent, ScheduleDialogData* data, const char* title) {
    HWND hDlg = CreateScheduleDialog(parent, data, title);
    if(!hDlg) return;
    
    EnableWindow(parent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        if(!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

// Create dialog template dynamically
HWND CreateJobDialog(HWND parent, JobDialogData* data, const char* title) {
    // Register dialog window class
    static bool registered = false;
    if(!registered) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = JobDialogWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "JobDialogClass";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        registered = true;
    }
    
    // Create a modal dialog window
    HWND hDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "JobDialogClass",
        title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 1300,
        parent, NULL, GetModuleHandle(NULL), data
    );
    
    int y = 25;
    
    // Job Name
    CreateWindowA("STATIC", "Job Name:", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    y += 28;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->jobName.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, y, 590, 30, hDlg, (HMENU)101, NULL, NULL);
    y += 45;
    
    // Sources with browse button
    CreateWindowA("STATIC", "Source Folders:", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    y += 28;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->sources.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, y, 500, 30, hDlg, (HMENU)102, NULL, NULL);
    CreateWindowA("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 525, y, 85, 30, hDlg, (HMENU)201, NULL, NULL);
    y += 45;
    
    // Destinations with browse button
    CreateWindowA("STATIC", "Destination Folders:", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    y += 28;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->destinations.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, y, 500, 30, hDlg, (HMENU)103, NULL, NULL);
    CreateWindowA("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 525, y, 85, 30, hDlg, (HMENU)202, NULL, NULL);
    y += 50;
    
    // Override checkbox
    CreateWindowA("BUTTON", "Override (replace existing backups)", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 20, y, 450, 26, hDlg, (HMENU)104, NULL, NULL);
    CheckDlgButton(hDlg, 104, data->override ? BST_CHECKED : BST_UNCHECKED);
    y += 45;
    
    // Incremental checkbox
    CreateWindowA("BUTTON", "Incremental (only changed files)", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 20, y, 450, 26, hDlg, (HMENU)105, NULL, NULL);
    CheckDlgButton(hDlg, 105, data->incremental ? BST_CHECKED : BST_UNCHECKED);
    y += 55;
    
    // Schedule enabled
    CreateWindowA("BUTTON", "Enable Scheduling", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 20, y, 450, 26, hDlg, (HMENU)106, NULL, NULL);
    CheckDlgButton(hDlg, 106, data->scheduleEnabled ? BST_CHECKED : BST_UNCHECKED);
    y += 55;
    
    // Schedule Interval
    CreateWindowA("STATIC", "Interval:", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    y += 28;
    HWND hIntervalCombo = CreateWindowA("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL, 20, y, 590, 200, hDlg, (HMENU)107, NULL, NULL);
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"mins");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"hours");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"daily");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"weekly");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"monthly");
    SendMessageA(hIntervalCombo, CB_ADDSTRING, 0, (LPARAM)"once");
    // Set current value
    int intervalIdx = 2; // default to daily
    if(data->scheduleInterval == "mins") intervalIdx = 0;
    else if(data->scheduleInterval == "hours") intervalIdx = 1;
    else if(data->scheduleInterval == "daily") intervalIdx = 2;
    else if(data->scheduleInterval == "weekly") intervalIdx = 3;
    else if(data->scheduleInterval == "monthly") intervalIdx = 4;
    else if(data->scheduleInterval == "once") intervalIdx = 5;
    SendMessageA(hIntervalCombo, CB_SETCURSEL, intervalIdx, 0);
    y += 45;
    
    // Schedule Value
    CreateWindowA("STATIC", "Value:", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    CreateWindowA("STATIC", "(number: e.g., 30 for mins, 1 for daily)", WS_CHILD|WS_VISIBLE|SS_LEFT, 20, y+24, 590, 22, hDlg, NULL, NULL, NULL);
    y += 50;
    HWND hValueCombo = CreateWindowA("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWN|WS_VSCROLL, 20, y, 200, 200, hDlg, (HMENU)108, NULL, NULL);
    // Add common values
    SendMessageA(hValueCombo, CB_ADDSTRING, 0, (LPARAM)"1");
    SendMessageA(hValueCombo, CB_ADDSTRING, 0, (LPARAM)"5");
    SendMessageA(hValueCombo, CB_ADDSTRING, 0, (LPARAM)"10");
    SendMessageA(hValueCombo, CB_ADDSTRING, 0, (LPARAM)"15");
    SendMessageA(hValueCombo, CB_ADDSTRING, 0, (LPARAM)"30");
    SendMessageA(hValueCombo, CB_ADDSTRING, 0, (LPARAM)"60");
    // Set current value or allow custom
    SetWindowTextA(hValueCombo, data->scheduleValue.c_str());
    y += 45;
    
    // Schedule Time
    CreateWindowA("STATIC", "Time:", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    CreateWindowA("STATIC", "(HH:MM, for daily/weekly/monthly)", WS_CHILD|WS_VISIBLE|SS_LEFT, 20, y+24, 590, 22, hDlg, NULL, NULL, NULL);
    y += 50;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->scheduleTime.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, y, 590, 30, hDlg, (HMENU)109, NULL, NULL);
    y += 45;
    
    // Start Date/Time
    CreateWindowA("STATIC", "Start Date (optional):", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    CreateWindowA("STATIC", "(YYYY-MM-DD HH:MM - leave blank for immediate start)", WS_CHILD|WS_VISIBLE|SS_LEFT, 20, y+24, 590, 22, hDlg, NULL, NULL, NULL);
    y += 50;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->startDate.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, y, 590, 30, hDlg, (HMENU)112, NULL, NULL);
    y += 45;
    
    // Schedule-specific destination
    CreateWindowA("STATIC", "Schedule Destination (optional override):", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    CreateWindowA("STATIC", "(leave blank to use job destinations)", WS_CHILD|WS_VISIBLE|SS_LEFT, 20, y+24, 590, 22, hDlg, NULL, NULL, NULL);
    y += 50;
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", data->scheduleDest.c_str(), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, y, 590, 30, hDlg, (HMENU)110, NULL, NULL);
    y += 45;
    
    // One-time schedule checkbox
    CreateWindowA("BUTTON", "One-time schedule (disable after first run)", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 20, y, 500, 26, hDlg, (HMENU)111, NULL, NULL);
    CheckDlgButton(hDlg, 111, data->scheduleOneTime ? BST_CHECKED : BST_UNCHECKED);
    y += 50;
    
    // Additional Schedules section
    CreateWindowA("STATIC", "Additional Schedules (unlimited):", WS_CHILD|WS_VISIBLE, 20, y, 590, 24, hDlg, NULL, NULL, NULL);
    y += 28;
    
    // List box for additional schedules (ID 113)
    HWND hScheduleList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY, 20, y, 590, 100, hDlg, (HMENU)113, NULL, NULL);
    
    // Populate list with existing additional schedules
    for(const auto& sched : data->additionalSchedules) {
        std::string display = sched.interval + " (" + sched.value + ") at " + sched.time;
        if(!sched.dest.empty()) display += " -> " + sched.dest;
        if(sched.onetime) display += " [one-time]";
        SendMessageA(hScheduleList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
    }
    y += 110;
    
    // Buttons for managing additional schedules
    CreateWindowA("BUTTON", "Add Schedule", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 20, y, 120, 30, hDlg, (HMENU)114, NULL, NULL);
    CreateWindowA("BUTTON", "Edit Schedule", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 150, y, 120, 30, hDlg, (HMENU)115, NULL, NULL);
    CreateWindowA("BUTTON", "Delete Schedule", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 280, y, 120, 30, hDlg, (HMENU)116, NULL, NULL);
    y += 50;
    
    // Buttons
    CreateWindowA("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 400, y, 105, 40, hDlg, (HMENU)IDOK, NULL, NULL);
    CreateWindowA("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 515, y, 105, 40, hDlg, (HMENU)IDCANCEL, NULL, NULL);
    
    return hDlg;
}

void RunJobDialog(HWND parent, JobDialogData* data, const char* title) {
    HWND hDlg = CreateJobDialog(parent, data, title);
    if(!hDlg) return;
    
    EnableWindow(parent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        if(!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

void createJob() {
    JobDialogData data;
    data.jobName = "NewBackupJob";
    data.sources = "C:/Users/Public/Documents";
    data.destinations = "D:/Backups";
    data.override = false;
    data.incremental = false;
    data.scheduleEnabled = false;
    data.scheduleInterval = "daily";
    data.scheduleValue = "1";
    data.scheduleTime = "02:00";
    data.scheduleDest = "";
    data.scheduleOneTime = false;
    data.accepted = false;
    
    RunJobDialog(GetParent(hTabControl), &data, "Create New Backup Job");
    
    if(!data.accepted) return;
    
    std::ofstream out(CONFIG_FILE, std::ios::app);
    out << "\n[job:" << data.jobName << "]\n";
    out << "sources=" << data.sources << "\n";
    out << "destinations=" << data.destinations << "\n";
    out << "override=" << (data.override ? "1" : "0") << "\n";
    out << "incremental=" << (data.incremental ? "1" : "0") << "\n";
    out << "shortcuts=\n";
    out << "schedule_enabled=" << (data.scheduleEnabled ? "1" : "0") << "\n";
    out << "schedule_interval=" << data.scheduleInterval << "\n";
    out << "schedule_value=" << data.scheduleValue << "\n";
    out << "schedule_time=" << data.scheduleTime << "\n";
    out << "start_date=" << data.startDate << "\n";
    out << "last_run=0\n";
    out << "schedule_dest=" << data.scheduleDest << "\n";
    out << "schedule_onetime=" << (data.scheduleOneTime ? "1" : "0") << "\n";
    
    // Serialize additional schedules: interval|value|time|dest|onetime|lastrun
    out << "additional_schedules=";
    for(size_t i = 0; i < data.additionalSchedules.size(); ++i) {
        if(i > 0) out << "|";
        const auto& s = data.additionalSchedules[i];
        out << s.interval << "|" << s.value << "|" << s.time << "|" 
            << s.dest << "|" << (s.onetime ? "1" : "0") << "|" << s.lastRun;
    }
    out << "\n";
    
    out.close();
    
    refreshJobs();
    MessageBoxA(NULL, ("Job '" + data.jobName + "' created successfully!").c_str(), "Success", MB_OK|MB_ICONINFORMATION);
    SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)("Job created: " + data.jobName).c_str());
}

void editJob() {
    auto job = getSelectedJob();
    if(job.empty()){ 
        MessageBoxA(NULL, "Please select a job from the list first.", "No Job Selected", MB_OK|MB_ICONWARNING);
        SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Select a job to edit."); 
        return;
    }
    
    // Load job data from config
    JobDialogData data;
    data.jobName = job;
    data.sources = "";
    data.destinations = "";
    data.override = false;
    data.incremental = false;
    data.scheduleEnabled = false;
    data.scheduleInterval = "daily";
    data.scheduleValue = "1";
    data.scheduleTime = "02:00";
    data.startDate = "";
    data.scheduleDest = "";
    data.scheduleOneTime = false;
    data.accepted = false;
    
    std::ifstream in(CONFIG_FILE);
    std::string line;
    bool inTargetJob = false;
    
    while(std::getline(in, line)) {
        line = trim(line);
        if(line.rfind("[job:",0)==0 && line.back()==']') {
            std::string nm = trim(line.substr(5, line.size()-6));
            inTargetJob = (nm == job);
        }
        else if(inTargetJob && !line.empty() && line.find('=') != std::string::npos) {
            size_t pos = line.find('=');
            std::string key = trim(line.substr(0, pos));
            std::string val = trim(line.substr(pos+1));
            
            if(key == "sources") data.sources = val;
            else if(key == "destinations") data.destinations = val;
            else if(key == "override") data.override = (val == "1");
            else if(key == "incremental") data.incremental = (val == "1");
            else if(key == "schedule_enabled") data.scheduleEnabled = (val == "1");
            else if(key == "schedule_interval") data.scheduleInterval = val;
            else if(key == "schedule_value") data.scheduleValue = val;
            else if(key == "schedule_time") data.scheduleTime = val;
            else if(key == "start_date") data.startDate = val;
            else if(key == "schedule_dest") data.scheduleDest = val;
            else if(key == "schedule_onetime") data.scheduleOneTime = (val == "1");
            else if(key == "additional_schedules") {
                // Parse: interval|value|time|dest|onetime|lastrun|...
                if(!val.empty()) {
                    std::stringstream ss(val);
                    std::string token;
                    std::vector<std::string> parts;
                    while(std::getline(ss, token, '|')) {
                        parts.push_back(token);
                    }
                    // Each schedule has 6 fields
                    for(size_t i = 0; i + 5 < parts.size(); i += 6) {
                        AdditionalSchedule sched;
                        sched.interval = parts[i];
                        sched.value = parts[i+1];
                        sched.time = parts[i+2];
                        sched.dest = parts[i+3];
                        sched.onetime = (parts[i+4] == "1");
                        sched.lastRun = parts[i+5];
                        data.additionalSchedules.push_back(sched);
                    }
                }
            }
        }
    }
    in.close();
    
    RunJobDialog(GetParent(hTabControl), &data, ("Edit Job: " + job).c_str());
    
    if(!data.accepted) return;
    
    // Read entire config, update the job section, write back
    in.open(CONFIG_FILE);
    std::stringstream buffer;
    inTargetJob = false;
    bool jobWritten = false;
    
    while(std::getline(in, line)) {
        std::string trimmed = trim(line);
        if(trimmed.rfind("[job:",0)==0 && trimmed.back()==']') {
            std::string nm = trim(trimmed.substr(5, trimmed.size()-6));
            if(nm == job) {
                // Write updated job
                buffer << "[job:" << data.jobName << "]\n";
                buffer << "sources=" << data.sources << "\n";
                buffer << "destinations=" << data.destinations << "\n";
                buffer << "override=" << (data.override ? "1" : "0") << "\n";
                buffer << "incremental=" << (data.incremental ? "1" : "0") << "\n";
                buffer << "shortcuts=\n";
                buffer << "schedule_enabled=" << (data.scheduleEnabled ? "1" : "0") << "\n";
                buffer << "schedule_interval=" << data.scheduleInterval << "\n";
                buffer << "schedule_value=" << data.scheduleValue << "\n";
                buffer << "schedule_time=" << data.scheduleTime << "\n";
                buffer << "start_date=" << data.startDate << "\n";
                buffer << "last_run=0\n";
                buffer << "schedule_dest=" << data.scheduleDest << "\n";
                buffer << "schedule_onetime=" << (data.scheduleOneTime ? "1" : "0") << "\n";
                
                // Serialize additional schedules
                buffer << "additional_schedules=";
                for(size_t i = 0; i < data.additionalSchedules.size(); ++i) {
                    if(i > 0) buffer << "|";
                    const auto& s = data.additionalSchedules[i];
                    buffer << s.interval << "|" << s.value << "|" << s.time << "|" 
                           << s.dest << "|" << (s.onetime ? "1" : "0") << "|" << s.lastRun;
                }
                buffer << "\n";
                
                inTargetJob = true;
                jobWritten = true;
                continue;
            } else {
                inTargetJob = false;
            }
        }
        
        if(!inTargetJob) buffer << line << "\n";
    }
    in.close();
    
    std::ofstream out(CONFIG_FILE, std::ios::trunc);
    out << buffer.str();
    out.close();
    
    refreshJobs();
    MessageBoxA(NULL, ("Job '" + data.jobName + "' updated successfully!").c_str(), "Success", MB_OK|MB_ICONINFORMATION);
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)("Job updated: " + data.jobName).c_str());
}

void deleteJob() {
    auto job = getSelectedJob();
    if(job.empty()){ SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Select a job to delete."); return;}
    std::string msg = "Delete job '" + job + "'?";
    if(MessageBoxA(NULL, msg.c_str(), "Confirm Delete", MB_YESNO|MB_ICONWARNING)==IDYES){
        // Read config, filter out the job, write back
        auto jobs = loadJobs();
        std::ifstream in(CONFIG_FILE);
        std::stringstream buffer;
        std::string line;
        bool inTargetJob = false;
        bool skipSection = false;
        
        while(std::getline(in, line)){
            std::string trimmed = trim(line);
            if(trimmed.rfind("[job:",0)==0 && trimmed.back()==']'){
                std::string nm = trim(trimmed.substr(5, trimmed.size()-6));
                if(nm == job){
                    skipSection = true;
                    continue;
                } else {
                    skipSection = false;
                }
            }
            if(!skipSection) buffer << line << "\n";
        }
        in.close();
        
        std::ofstream out(CONFIG_FILE, std::ios::trunc);
        out << buffer.str();
        out.close();
        
        refreshJobs();
        SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)("Job deleted: " + job).c_str());
    }
}

void checkJobStatus() {
    // Parse maintenance.log for job status
    std::ifstream log("maintenance.log");
    if(!log.is_open()){ SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"No log file found."); return; }
    
    std::string result = "Recent Job Status:\n";
    std::string line;
    std::vector<std::string> lastLines;
    
    while(std::getline(log, line)){
        if(line.find("backup") != std::string::npos || line.find("scheduler") != std::string::npos){
            lastLines.push_back(line);
            if(lastLines.size() > 10) lastLines.erase(lastLines.begin());
        }
    }
    
    for(auto& l : lastLines) result += l + "\n";
    
    MessageBoxA(NULL, result.c_str(), "Job Status", MB_OK|MB_ICONINFORMATION);
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Status checked.");
}

// deleteShortcut replaced by checkbox-driven multi-delete
void deleteShortcut() { toggleDeleteMode(); }

void toggleEditMode(){
    editMode = !editMode;
    for(size_t i=0;i<shortcutChecks.size();++i){
        if(shortcutChecks[i]){
            ShowWindow(shortcutChecks[i], editMode ? SW_SHOW : SW_HIDE);
        }
        // Redraw shortcut button to show color change
        if(i < shortcuts.size()) InvalidateRect(shortcuts[i].second, NULL, FALSE);
    }
    if(editMode){
        deleteMode = false; // Exit delete mode if entering edit mode
    }
    SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)(editMode?"Edit mode: select shortcut to edit":"Edit mode off"));
}

void performEdit(size_t idx){
    if(idx >= shortcuts.size()) return;
    
    // Get new file path
    char path[MAX_PATH] = "";
    strcpy_s(path, shortcuts[idx].first.c_str());
    OPENFILENAMEA ofn = {sizeof(ofn)};
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Select New File or Program for Shortcut";
    
    if (GetOpenFileNameA(&ofn)) {
        std::filesystem::path p(path);
        std::string name = p.stem().string();
        
        // Update the shortcut
        shortcuts[idx].first = std::string(path);
        SetWindowTextA(shortcuts[idx].second, name.c_str());
        
        saveShortcuts();
        SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)("Shortcut edited: " + name).c_str());
    }
    
    // Exit edit mode
    editMode = false;
    for(size_t i=0;i<shortcutChecks.size();++i){
        if(shortcutChecks[i]){
            ShowWindow(shortcutChecks[i], SW_HIDE);
        }
    }
    SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"Edit mode off");
}

void editShortcut() { toggleEditMode(); }

// Global for nickname dialog
static char g_nicknameBuffer[256] = "";
static bool g_nicknameOk = false;

// Subclassed window proc for our simple nickname dialog
LRESULT CALLBACK NicknameDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Nothing special here; controls already created by caller
        } return 0;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDOK) {
                // Read text from edit control
                GetDlgItemTextA(hDlg, 1001, g_nicknameBuffer, sizeof(g_nicknameBuffer));
                g_nicknameOk = true;
                PostMessageA(hDlg, WM_CLOSE, 0, 0);
                return 0;
            } else if (id == IDCANCEL) {
                g_nicknameOk = false;
                PostMessageA(hDlg, WM_CLOSE, 0, 0);
                return 0;
            }
        } break;
        case WM_CLOSE: {
            // Destroy the dialog window to break the loop in caller
            DestroyWindow(hDlg);
            return 0;
        }
    }
    return DefWindowProcA(hDlg, msg, wParam, lParam);
}

void addShortcut() {
    char path[MAX_PATH] = "";
    OPENFILENAMEA ofn = {sizeof(ofn)};
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Select File or Program to Add as Shortcut";
    
    if (GetOpenFileNameA(&ofn)) {
        std::filesystem::path p(path);
        std::string defaultName = p.stem().string();
        
        // Set default nickname
        strncpy_s(g_nicknameBuffer, defaultName.c_str(), _TRUNCATE);
        
        // Create a simple dialog dynamically
        HWND hDlg = CreateWindowExA(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            "#32770", // Dialog class
            "Shortcut Name",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, 350, 140,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );
        
        if (hDlg) {
            // Create label
            CreateWindowA("STATIC", "Enter shortcut name:",
                WS_CHILD | WS_VISIBLE,
                10, 10, 320, 20,
                hDlg, NULL, GetModuleHandle(NULL), NULL);
            
            // Create text box
            HWND hEdit = CreateWindowA("EDIT", g_nicknameBuffer,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                10, 35, 320, 22,
                hDlg, (HMENU)1001, GetModuleHandle(NULL), NULL);
            
            // Create OK button
            CreateWindowA("BUTTON", "OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                100, 70, 70, 25,
                hDlg, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
            
            // Create Cancel button
            CreateWindowA("BUTTON", "Cancel",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                180, 70, 70, 25,
                hDlg, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
            
            // Subclass the dialog window to our proc
            SetWindowLongPtrA(hDlg, GWLP_WNDPROC, (LONG_PTR)NicknameDialogProc);
            
            // Focus and select all in edit control
            SetFocus(hEdit);
            SendMessage(hEdit, EM_SETSEL, 0, -1);
            
            // Message loop for dialog
            MSG msg;
            bool dialogDone = false;
            g_nicknameOk = false;
            
            while (!dialogDone && GetMessage(&msg, NULL, 0, 0)) {
                if (msg.hwnd == hDlg || IsChild(hDlg, msg.hwnd)) {
                    if (msg.message == WM_COMMAND) {
                        if (LOWORD(msg.wParam) == IDOK || LOWORD(msg.wParam) == IDCANCEL) {
                            // NicknameDialogProc will set g_nicknameOk and post WM_CLOSE
                        }
                    } else if (msg.message == WM_CLOSE) {
                        dialogDone = true;
                    }
                }
                
                if (!IsDialogMessage(hDlg, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            
            if (!g_nicknameOk) return; // User cancelled
        }
        
        std::string name = g_nicknameBuffer;
        if (name.empty()) name = defaultName; // Fallback
        
        // Position within shortcuts panel but parented to main window so WM_DRAWITEM is handled
        HWND hMain = hMainWindow ? hMainWindow : GetParent(hShortcutsPanel);
        RECT rcPanel; GetWindowRect(hShortcutsPanel, &rcPanel);
        POINT pt = {rcPanel.left, rcPanel.top};
        ScreenToClient(hMain, &pt);
        
        int panelW = rcPanel.right - rcPanel.left;
        int btnW = panelW - 40;
        if(btnW < 200) btnW = 200;
        
        int baseX = pt.x + 20;
        int baseY = pt.y + 10 + nextShortcutY; // 10px inset inside panel
        
        HWND btn = CreateWindow("BUTTON", name.c_str(), WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW,
                baseX, baseY, btnW, 30, hMain, (HMENU)(UINT_PTR)nextShortcutId, NULL, NULL);
        int chkX = baseX - 20;
        int chkY = baseY + 7;
        HWND chk = CreateWindow("BUTTON", "", WS_CHILD|BS_AUTOCHECKBOX,
            chkX, chkY, 16, 16, hMain, NULL, NULL, NULL);
        ShowWindow(chk, SW_HIDE);
        
        // Extract and store icon
        HICON hIcon = extractIcon(std::string(path));
        shortcutIcons.push_back(hIcon);
        
        // Refresh panel
        InvalidateRect(hShortcutsPanel, NULL, TRUE);
        
        shortcuts.push_back({std::string(path), btn});
        shortcutChecks.push_back(chk);
        nextShortcutId++;
        nextShortcutY += 35;
        
        saveShortcuts();
        SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)("Shortcut added: " + name).c_str());
    }
}

void toggleDeleteMode(){
    deleteMode = !deleteMode;
    for(size_t i=0;i<shortcutChecks.size();++i){
        if(shortcutChecks[i]){
            ShowWindow(shortcutChecks[i], deleteMode ? SW_SHOW : SW_HIDE);
        }
        // Redraw shortcut button to show color change
        if(i < shortcuts.size()) InvalidateRect(shortcuts[i].second, NULL, FALSE);
    }
    if(deleteMode){
        editMode = false; // Exit edit mode if entering delete mode
    }
    SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)(deleteMode?"Delete mode: select and press Delete again":"Delete mode off"));
}

void performDeleteSelected(){
    // Collect indices to delete (checked)
    std::vector<size_t> toDelete;
    for(size_t i=0;i<shortcutChecks.size();++i){
        if(shortcutChecks[i]){
            LRESULT state = SendMessage(shortcutChecks[i], BM_GETCHECK, 0, 0);
            if(state == BST_CHECKED) toDelete.push_back(i);
        }
    }
    if(toDelete.empty()){
        MessageBoxA(NULL, "No shortcuts selected.", "Delete Shortcuts", MB_OK|MB_ICONINFORMATION);
        toggleDeleteMode(); // Exit delete mode
        return;
    }
    // Delete from end to start to keep indices valid
    for(int k=(int)toDelete.size()-1;k>=0;--k){
        size_t idx = toDelete[k];
        // Destroy associated window controls
        DestroyWindow(shortcuts[idx].second);
        if(shortcutChecks[idx]) DestroyWindow(shortcutChecks[idx]);
        // Destroy and remove icon if present
        if(idx < shortcutIcons.size() && shortcutIcons[idx]) {
            DestroyIcon(shortcutIcons[idx]);
            shortcutIcons.erase(shortcutIcons.begin() + idx);
        }
        shortcuts.erase(shortcuts.begin()+idx);
        shortcutChecks.erase(shortcutChecks.begin()+idx);
    }
    // Recompute layout positions using panel origin
    nextShortcutY = 10;
    HWND hMain = hMainWindow ? hMainWindow : GetParent(hShortcutsPanel);
    RECT rcPanel; GetWindowRect(hShortcutsPanel, &rcPanel);
    POINT pt = {rcPanel.left, rcPanel.top};
    ScreenToClient(hMain, &pt);
    
    int panelW = rcPanel.right - rcPanel.left;
    int btnW = panelW - 40;
    if(btnW < 200) btnW = 200;
    
    for(size_t i=0;i<shortcuts.size();++i){
        int baseX = pt.x + 20; // left margin within panel
        int baseY = pt.y + 10 + nextShortcutY;
        SetWindowPos(shortcuts[i].second, NULL, baseX, baseY, btnW, 30, SWP_NOZORDER);
        if(shortcutChecks[i]){
            int chkX = baseX - 20;
            int chkY = baseY + 7;
            SetWindowPos(shortcutChecks[i], HWND_TOP, chkX, chkY, 16, 16, SWP_SHOWWINDOW);
            ShowWindow(shortcutChecks[i], SW_HIDE);
        }
        nextShortcutY += 35;
    }
    saveShortcuts();
    toggleDeleteMode(); // Exit delete mode after successful delete
    SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"Shortcuts deleted and saved.");
}

void launchShortcut(int id) {
    for (auto& sc : shortcuts) {
        int btnId = (int)(UINT_PTR)GetDlgCtrlID(sc.second);
        if (btnId == id) {
            // Use ShellExecuteA to open any file type with its default application
            std::filesystem::path p(sc.first);
            std::string dir = p.has_parent_path() ? p.parent_path().string() : std::string();
            HINSTANCE result = ShellExecuteA(NULL, "open", sc.first.c_str(), NULL,
                                             dir.empty() ? NULL : dir.c_str(), SW_SHOWNORMAL);
            
            if ((INT_PTR)result > 32) {
                SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"Shortcut opened.");
            } else {
                SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"Failed to open shortcut.");
            }
            break;
        }
    }
}

void doBackup(){ 
    auto job = getSelectedJob(); 
    if(job.empty()){ 
        MessageBoxA(NULL, "Please select a backup job from the list first.", "No Job Selected", MB_OK|MB_ICONWARNING);
        SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Select a job first."); 
        return;
    } 
    
    if(MessageBoxA(NULL, ("Run backup for job '" + job + "'?").c_str(), "Confirm Backup", MB_YESNO|MB_ICONQUESTION) != IDYES) return;
    
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Running backup...");
    
    // Run backup in background
    std::thread([job]() {
        toolInvoke("--backup " + job);
    }).detach();
    
    MessageBoxA(NULL, ("Backup started for job '" + job + "'.\n\nCheck maintenance.log for results.").c_str(), "Backup Started", MB_OK|MB_ICONINFORMATION);
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Backup running...");
}
void doRestore(){ 
    auto job = getSelectedJob(); 
    if(job.empty()){ 
        MessageBoxA(NULL, "Please select a backup job from the list first.", "No Job Selected", MB_OK|MB_ICONWARNING);
        SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Select a job first."); 
        return;
    } 
    
    std::string msg = "Restore latest backup for job '" + job + "'?\n\nWARNING: This will overwrite existing files!";
    if(MessageBoxA(NULL, msg.c_str(), "Confirm Restore", MB_YESNO|MB_ICONWARNING) != IDYES) return;
    
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Restoring backup...");
    
    // Run restore in background
    std::thread([job]() {
        toolInvoke("--restore " + job + " --force");
    }).detach();
    
    MessageBoxA(NULL, ("Restore started for job '" + job + "'.\n\nCheck maintenance.log for results.").c_str(), "Restore Started", MB_OK|MB_ICONINFORMATION);
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Restore running...");
}
void doShutdown(){ if(MessageBoxA(NULL, "Shutdown the computer?", "Confirm", MB_YESNO|MB_ICONWARNING)==IDYES){ toolInvoke("--shutdown --force"); SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Shutdown initiated."); } }
void doRestart(){ if(MessageBoxA(NULL, "Restart the computer?", "Confirm", MB_YESNO|MB_ICONWARNING)==IDYES){ toolInvoke("--restart --force"); SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Restart initiated."); } }
void doUpdateRestart(){ if(MessageBoxA(NULL, "Run Windows Update and restart?", "Confirm", MB_YESNO|MB_ICONWARNING)==IDYES){ toolInvoke("--update-restart --force"); SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Update & restart initiated."); } }
void doLogoff(){ if(MessageBoxA(NULL, "Log off current user?", "Confirm", MB_YESNO|MB_ICONWARNING)==IDYES){ toolInvoke("--logoff --force"); SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Logoff initiated."); } }
void doLock(){ LockWorkStation(); SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Workstation locked."); }
void doCombineNonOverride(){
    auto job = getSelectedJob(); 
    if(job.empty()){ 
        MessageBoxA(NULL, "Please select a backup job from the list first.", "No Job Selected", MB_OK|MB_ICONWARNING);
        SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Select a job first."); 
        return;
    }
    
    std::string msg = "Combine all non-override backups for job '" + job + "'?\n\nThis will create a single folder with the newest version of each file.";
    if(MessageBoxA(NULL, msg.c_str(), "Confirm Combine", MB_YESNO|MB_ICONQUESTION) != IDYES) return;
    
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Combining backups...");
    
    // Run combine in background
    std::thread([job]() {
        toolInvoke("--combine " + job);
    }).detach();
    
    MessageBoxA(NULL, ("Combine started for job '" + job + "'.\n\nCheck maintenance.log for results.").c_str(), "Combine Started", MB_OK|MB_ICONINFORMATION);
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Combine running...");
}

void startBackupDaemon() {
    // Start the backup daemon in the background with 1-minute check interval
    std::thread([]() {
        toolInvoke("--daemon 1");
    }).detach();
}

void startUpdaterDaemon() {
    // Start the updater daemon in the background for 24-hour update checks
    std::thread([]() {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path p(exePath);
        auto dir = p.parent_path();
        std::string updaterPath = (dir / "updater.exe").string();
        std::string cmd = '"' + updaterPath + '"' + " --daemon";
        
        STARTUPINFOA si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::string cmdCopy = cmd;
        if(CreateProcessA(NULL, cmdCopy.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }).detach();
}

void checkForUpdatesOnStartup() {
    // Check if updates are available on startup
    std::thread([]() {
        // Small delay to let GUI fully initialize
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path p(exePath);
        auto dir = p.parent_path();
        
        // Read current version
        std::string currentVersion = "0.0.0";
        std::ifstream vf((dir / "version.txt").string());
        if(vf.is_open()) {
            std::getline(vf, currentVersion);
            vf.close();
        }
        
        // Read version from GitHub
        std::string newVersion = currentVersion;
        std::string versionUrl = "https://raw.githubusercontent.com/imasteredu2/maintenance_tools/tools/version.txt?t=" + 
                                 std::to_string(std::time(nullptr));
        
        // Simple HTTP get
        HINTERNET hInternet = InternetOpenA("Maintenance Tool", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if(hInternet) {
            HINTERNET hUrl = InternetOpenUrlA(hInternet, versionUrl.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
            if(hUrl) {
                char buffer[256];
                DWORD bytesRead = 0;
                if(InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead)) {
                    buffer[bytesRead] = '\0';
                    std::string versionData(buffer);
                    size_t pos = versionData.find_first_not_of(" \t\r\n");
                    if(pos != std::string::npos) {
                        size_t end = versionData.find_first_of(" \t\r\n", pos);
                        if(end == std::string::npos) end = versionData.length();
                        newVersion = versionData.substr(pos, end - pos);
                    }
                }
                InternetCloseHandle(hUrl);
            }
            InternetCloseHandle(hInternet);
        }
        
        // Compare versions
        if(newVersion != currentVersion) {
            // New version available - ask user
            std::string msg = "New version available!\n\nCurrent: " + currentVersion + 
                             "\nLatest: " + newVersion + "\n\nUpdate now?";
            if(MessageBoxA(NULL, msg.c_str(), "Update Available", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                // User wants to update
                std::string updaterPath = (dir / "updater.exe").string();
                std::string cmd = '"' + updaterPath + '"' + " --check-update";
                STARTUPINFOA si{sizeof(si)};
                PROCESS_INFORMATION pi{};
                std::string cmdCopy = cmd;
                if(CreateProcessA(NULL, cmdCopy.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            }
        }
    }).detach();
}

void doCheckForUpdates(){
    SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Checking for updates...");
    
    // Get current executable directory
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path p(exePath);
    auto dir = p.parent_path();
    std::string updaterPath = (dir / "updater.exe").string();
    
    // Launch updater with --check-update flag
    SHELLEXECUTEINFOA sei{sizeof(sei)};
    sei.lpVerb = "open";
    sei.lpFile = updaterPath.c_str();
    sei.lpParameters = "--check-update";
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    
    if(ShellExecuteExA(&sei)) {
        SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Update check complete.");
    } else {
        MessageBoxA(NULL, "Failed to launch updater.", "Error", MB_OK|MB_ICONERROR);
        SendMessageA(hStatus, WM_SETTEXT,0,(LPARAM)"Update check failed.");
    }
}

void showSettingsDialog() {
    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, "STATIC", "Settings", 
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 250, hMainWindow, NULL, GetModuleHandle(NULL), NULL);
    
    SetWindowLongA(hDlg, GWL_STYLE, GetWindowLongA(hDlg, GWL_STYLE) | WS_DLGFRAME);
    
    int y = 20;
    
    // Auto-update at 24hr checkbox
    HWND hChkAutoUpdate = CreateWindowA("BUTTON", "Auto-update when new version found (24hr check)", 
        WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 20, y, 350, 20, hDlg, (HMENU)301, NULL, NULL);
    SendMessageA(hChkAutoUpdate, BM_SETCHECK, appSettings.autoUpdateAt24h ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 30;
    
    // Update check interval label
    CreateWindowA("STATIC", "Update check interval (hours, 1-24):", WS_CHILD|WS_VISIBLE, 20, y, 350, 20, hDlg, NULL, NULL, NULL);
    y += 24;
    
    char intervalStr[10];
    sprintf_s(intervalStr, sizeof(intervalStr), "%d", appSettings.updateCheckIntervalHours);
    HWND hEditInterval = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", intervalStr, 
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_NUMBER, 20, y, 100, 24, hDlg, (HMENU)302, NULL, NULL);
    y += 40;
    
    // Buttons
    HWND hBtnOK = CreateWindowA("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 
        100, y, 80, 30, hDlg, (HMENU)IDOK, NULL, NULL);
    HWND hBtnCancel = CreateWindowA("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 
        200, y, 80, 30, hDlg, (HMENU)IDCANCEL, NULL, NULL);
    
    // Modal loop
    MSG msg;
    bool done = false;
    while(!done && GetMessageA(&msg, NULL, 0, 0)) {
        if(msg.hwnd == hDlg || IsChild(hDlg, msg.hwnd)) {
            if(msg.message == WM_COMMAND) {
                int id = LOWORD(msg.wParam);
                if(id == IDOK) {
                    // Save settings
                    appSettings.autoUpdateAt24h = (SendMessageA(hChkAutoUpdate, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    
                    char val[10];
                    GetWindowTextA(hEditInterval, val, sizeof(val));
                    try {
                        int interval = std::stoi(val);
                        if(interval >= 1 && interval <= 24) {
                            appSettings.updateCheckIntervalHours = interval;
                        }
                    } catch(...) {}
                    
                    saveSettings();
                    done = true;
                } else if(id == IDCANCEL) {
                    done = true;
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    
    DestroyWindow(hDlg);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){ switch(m){
    case WM_CREATE: {
        hMainWindow = h;
        // Load theme preference and settings
        loadTheme();
        loadSettings();
        
        // Enable dark mode/theme support for window
        BOOL useDarkMode = darkTheme ? TRUE : FALSE;
        DwmSetWindowAttribute(h, 20, &useDarkMode, sizeof(useDarkMode)); // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
        
        // Create theme brushes
        hBrushBg = CreateSolidBrush(RGB(240, 240, 240)); // Light background
        hBrushBgDark = CreateSolidBrush(RGB(32, 32, 32)); // Dark background
        hBrushControlBg = CreateSolidBrush(RGB(255, 255, 255)); // Light control background
        hBrushControlBgDark = CreateSolidBrush(RGB(45, 45, 45)); // Dark control background
        
        // Create menu bar (File, View, Settings, Help)
        HMENU hMenu = CreateMenu();
        HMENU hFile = CreatePopupMenu();
        AppendMenuA(hFile, MF_STRING, 1000, "Exit");
        HMENU hView = CreatePopupMenu();
        AppendMenuA(hView, MF_STRING, 1003, darkTheme ? "Switch to Light Theme" : "Switch to Dark Theme");
        HMENU hSettings = CreatePopupMenu();
        AppendMenuA(hSettings, MF_STRING, 1004, "Settings...");
        HMENU hHelp = CreatePopupMenu();
        {
            std::string verLabel = std::string("Version: ") + getCurrentVersion();
            AppendMenuA(hHelp, MF_STRING | MF_GRAYED, 0, verLabel.c_str());
            AppendMenuA(hHelp, MF_SEPARATOR, 0, NULL);
        }
        AppendMenuA(hHelp, MF_STRING, 1001, "Check for Updates...");
        AppendMenuA(hHelp, MF_STRING, 1002, "About...");
        AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "File");
        AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "View");
        AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSettings, "Settings");
        AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "Help");
        SetMenu(h, hMenu);
        DrawMenuBar(h);
        
        // Create colored brushes
        hBrushRed = CreateSolidBrush(RGB(220, 50, 50));
        hBrushYellow = CreateSolidBrush(RGB(240, 200, 50));
        hBrushOrange = CreateSolidBrush(RGB(255, 140, 50));
        hBrushBlue = CreateSolidBrush(RGB(50, 120, 220));
        hBrushGreen = CreateSolidBrush(RGB(50, 200, 100));
        
        // Create tab control at top
        hTabControl = CreateWindow(WC_TABCONTROL, "", WS_CHILD|WS_VISIBLE, 10,10,375,30,h,NULL,NULL,NULL);
        
        TCITEMA tie = {TCIF_TEXT};
        tie.pszText = (char*)"Home";
        TabCtrl_InsertItem(hTabControl, 0, &tie);
        tie.pszText = (char*)"Backup";
        TabCtrl_InsertItem(hTabControl, 1, &tie);
        
        // HOME TAB CONTROLS (start at y=50, below tab control)
        // System buttons on Home tab (all same size: 85x35)
        hBtnLogoff = CreateWindow("BUTTON","Logoff", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 20,50,85,35,h,(HMENU)7,NULL,NULL);
        hBtnUpdateRestart = CreateWindow("BUTTON","Update", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 110,50,85,35,h,(HMENU)6,NULL,NULL);
        hBtnRestart = CreateWindow("BUTTON","Restart", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 200,50,85,35,h,(HMENU)5,NULL,NULL);
        hBtnShutdown = CreateWindow("BUTTON","Shutdown", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 290,50,85,35,h,(HMENU)4,NULL,NULL);
        
        // Lock PC button spanning from Logoff to Shutdown
        HWND hBtnLock = CreateWindow("BUTTON","Lock PC", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 20,90,355,30,h,(HMENU)16,NULL,NULL);
        
        // Shortcut management buttons (row of 4) - owner-draw for dark mode
        hBtnAdd = CreateWindow("BUTTON","Add", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 20,130,85,30,h,(HMENU)8,NULL,NULL);
        hBtnSave = CreateWindow("BUTTON","Save", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 110,130,85,30,h,(HMENU)13,NULL,NULL);
        hBtnEdit = CreateWindow("BUTTON","Edit", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 200,130,85,30,h,(HMENU)14,NULL,NULL);
        hBtnDelete = CreateWindow("BUTTON","Delete", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW, 290,130,85,30,h,(HMENU)15,NULL,NULL);
        
        // Shortcuts panel on Home tab
        HWND hLblShortcuts = CreateWindow("STATIC","Program Shortcuts:", WS_CHILD|WS_VISIBLE|SS_LEFT, 20,170,150,20,h,(HMENU)17,NULL,NULL);
        hShortcutsPanel = CreateWindowEx(WS_EX_CLIENTEDGE, "STATIC", "", WS_CHILD|WS_VISIBLE, 20,190,355,240,h,NULL,NULL,NULL);
        // Load persisted shortcuts
        loadShortcuts(h);
        
        // BACKUP TAB CONTROLS (initially hidden) - owner-draw for dark mode
        // Row 1: Primary action buttons (larger for emphasis)
        hBtnBackup = CreateWindow("BUTTON","Run Backup", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 20,50,110,35,h,(HMENU)2,NULL,NULL);
        hBtnRestore = CreateWindow("BUTTON","Restore", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 135,50,110,35,h,(HMENU)3,NULL,NULL);
        hBtnStatus = CreateWindow("BUTTON","Check Status", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 250,50,125,35,h,(HMENU)11,NULL,NULL);
        
        // Row 2: Job management buttons
        hBtnCreate = CreateWindow("BUTTON","Create Job", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 20,95,85,30,h,(HMENU)9,NULL,NULL);
        hBtnEditJob = CreateWindow("BUTTON","Edit Job", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 110,95,85,30,h,(HMENU)10,NULL,NULL);
        hBtnDeleteJob = CreateWindow("BUTTON","Delete Job", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 200,95,85,30,h,(HMENU)12,NULL,NULL);
        hBtnRefresh = CreateWindow("BUTTON","Refresh", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 290,95,85,30,h,(HMENU)1,NULL,NULL);
        
        // Row 3: Combine and Update buttons
        hBtnCombine = CreateWindow("BUTTON","Combine Non-Override", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 20,130,160,30,h,(HMENU)18,NULL,NULL);
        hBtnCheckUpdate = CreateWindow("BUTTON","Check for Updates", WS_CHILD|BS_PUSHBUTTON|BS_OWNERDRAW, 185,130,190,30,h,(HMENU)19,NULL,NULL);
        
        // Jobs list on Backup tab
        HWND hLblJobs = CreateWindow("STATIC","Backup Jobs:", WS_CHILD|SS_LEFT, 20,170,150,20,h,NULL,NULL,NULL);
        hList = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", "", WS_CHILD|LBS_NOTIFY|WS_VSCROLL, 20,190,355,240,h,NULL,NULL,NULL);
        
        // Status bar at bottom
        hStatus = CreateWindow("STATIC","Ready", WS_CHILD|WS_VISIBLE|SS_LEFT, 10,440,375,20,h,NULL,NULL,NULL);
        
        refreshJobs();
        switchTab(0); // Show Home tab by default
        
        // Check for updates on startup
        checkForUpdatesOnStartup();
        
        // Start backup daemon in background
        startBackupDaemon();
        
        // Start updater daemon in background for 24-hour update checks
        startUpdaterDaemon();
        
        SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"Daemons started.");
    } break;
    case WM_NOTIFY: {
        NMHDR* pnmhdr = (NMHDR*)l;
        if (pnmhdr->hwndFrom == hTabControl && pnmhdr->code == TCN_SELCHANGE) {
            int index = TabCtrl_GetCurSel(hTabControl);
            switchTab(index);
        }
    } break;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* pDIS = (DRAWITEMSTRUCT*)l;
        if (pDIS->CtlType == ODT_BUTTON) {
            HBRUSH hBrush = NULL;
            COLORREF textColor = RGB(255, 255, 255);
            HWND hBtnLock = GetDlgItem(h, 16);
            bool isStandardButton = false;
            
            // System action buttons (colored)
            if (pDIS->hwndItem == hBtnShutdown) hBrush = hBrushRed;
            else if (pDIS->hwndItem == hBtnRestart) hBrush = hBrushYellow;
            else if (pDIS->hwndItem == hBtnLogoff) hBrush = hBrushOrange;
            else if (pDIS->hwndItem == hBtnUpdateRestart) hBrush = hBrushBlue;
            else if (pDIS->hwndItem == hBtnLock) hBrush = hBrushGreen;
            // Standard buttons (dark gray in dark mode, light gray in light mode)
            else if (pDIS->hwndItem == hBtnAdd || pDIS->hwndItem == hBtnSave || 
                     pDIS->hwndItem == hBtnEdit || pDIS->hwndItem == hBtnDelete ||
                     pDIS->hwndItem == hBtnBackup || pDIS->hwndItem == hBtnRestore ||
                     pDIS->hwndItem == hBtnStatus || pDIS->hwndItem == hBtnCreate ||
                     pDIS->hwndItem == hBtnEditJob || pDIS->hwndItem == hBtnDeleteJob ||
                     pDIS->hwndItem == hBtnRefresh || pDIS->hwndItem == hBtnCombine ||
                     pDIS->hwndItem == hBtnCheckUpdate) {
                isStandardButton = true;
                hBrush = CreateSolidBrush(darkTheme ? RGB(60, 60, 60) : RGB(225, 225, 225));
                textColor = darkTheme ? RGB(255, 255, 255) : RGB(0, 0, 0);
            }
            
            if (hBrush) {
                FillRect(pDIS->hDC, &pDIS->rcItem, hBrush);
                
                // Draw button text
                char text[64];
                GetWindowTextA(pDIS->hwndItem, text, sizeof(text));
                SetBkMode(pDIS->hDC, TRANSPARENT);
                SetTextColor(pDIS->hDC, textColor);
                DrawTextA(pDIS->hDC, text, -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                
                // Draw border if focused
                if (pDIS->itemState & ODS_FOCUS) {
                    RECT rc = pDIS->rcItem;
                    InflateRect(&rc, -2, -2);
                    DrawFocusRect(pDIS->hDC, &rc);
                }
                
                // Clean up temporary brush
                if (isStandardButton) DeleteObject(hBrush);
                return TRUE;
            }
            
            // Check if this is a shortcut button
            for (size_t i = 0; i < shortcuts.size(); ++i) {
                if (shortcuts[i].second == pDIS->hwndItem) {
                    // Draw shortcut button with icon and text
                    HBRUSH bgBrush = NULL;
                    bool needsCleanup = false;
                    
                    // Change background color based on edit/delete mode or dark theme
                    if (deleteMode) {
                        bgBrush = CreateSolidBrush(darkTheme ? RGB(120, 40, 40) : RGB(200, 100, 100)); // Dark red in dark mode
                        needsCleanup = true;
                    } else if (editMode) {
                        bgBrush = CreateSolidBrush(darkTheme ? RGB(40, 80, 120) : RGB(100, 150, 200)); // Dark blue in dark mode
                        needsCleanup = true;
                    } else {
                        bgBrush = CreateSolidBrush(darkTheme ? RGB(60, 60, 60) : RGB(225, 225, 225));
                        needsCleanup = true;
                    }
                    
                    FillRect(pDIS->hDC, &pDIS->rcItem, bgBrush);
                    
                    // Draw icon on the left
                    if (i < shortcutIcons.size() && shortcutIcons[i]) {
                        DrawIconEx(pDIS->hDC, pDIS->rcItem.left + 5, pDIS->rcItem.top + 7,
                                   shortcutIcons[i], 16, 16, 0, NULL, DI_NORMAL);
                    }
                    
                    // Draw text next to icon
                    char text[256];
                    GetWindowTextA(pDIS->hwndItem, text, sizeof(text));
                    RECT textRect = pDIS->rcItem;
                    textRect.left += 26; // Offset for icon
                    SetBkMode(pDIS->hDC, TRANSPARENT);
                    SetTextColor(pDIS->hDC, darkTheme ? RGB(255, 255, 255) : RGB(0, 0, 0));
                    DrawTextA(pDIS->hDC, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    
                    // Draw border if focused/pressed
                    if (pDIS->itemState & (ODS_FOCUS | ODS_SELECTED)) {
                        DrawEdge(pDIS->hDC, &pDIS->rcItem, 
                                (pDIS->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, 
                                BF_RECT);
                    }
                    
                    // Clean up the brush
                    if (needsCleanup) DeleteObject(bgBrush);
                    
                    return TRUE;
                }
            }
        }
    } break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        if (darkTheme) {
            HDC hdc = (HDC)w;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(32, 32, 32));
            return (LRESULT)hBrushBgDark;
        }
    } break;
    case WM_CTLCOLORLISTBOX: {
        if (darkTheme) {
            HDC hdc = (HDC)w;
            SetTextColor(hdc, RGB(255, 255, 255)); // White text
            SetBkColor(hdc, RGB(45, 45, 45)); // Dark background
            return (LRESULT)hBrushControlBgDark;
        }
    } break;
    case WM_ERASEBKGND: {
        if (darkTheme) {
            HDC hdc = (HDC)w;
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(hdc, &rc, hBrushBgDark);
            return 1;
        }
    } break;
    case WM_SIZE: {
        int clientW = LOWORD(l);
        int clientH = HIWORD(l);
        layoutAllUI(clientW, clientH);
    } break;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if(id==1000) { PostMessage(h, WM_CLOSE, 0, 0); }
        else if(id==1001) { doCheckForUpdates(); }
        else if(id==1002) { showAbout(); }
        else if(id==1003) { 
            // Toggle theme and refresh GUI
            darkTheme = !darkTheme;
            saveTheme();
            
            // Update dark mode attribute
            BOOL useDarkMode = darkTheme ? TRUE : FALSE;
            DwmSetWindowAttribute(h, 20, &useDarkMode, sizeof(useDarkMode));
            
            // Update menu text
            HMENU hMenu = GetMenu(h);
            HMENU hView = GetSubMenu(hMenu, 1); // View menu is second (index 1)
            ModifyMenuA(hView, 1003, MF_BYCOMMAND | MF_STRING, 1003, darkTheme ? "Switch to Light Theme" : "Switch to Dark Theme");
            DrawMenuBar(h);
            
            // Force complete redraw of all controls
            InvalidateRect(h, NULL, TRUE);
            UpdateWindow(h);
            
            // Redraw all child windows
            EnumChildWindows(h, [](HWND hwndChild, LPARAM) -> BOOL {
                InvalidateRect(hwndChild, NULL, TRUE);
                UpdateWindow(hwndChild);
                return TRUE;
            }, 0);
        }
        else if(id==1004) { showSettingsDialog(); }
        else if(id==1) refreshJobs();
        else if(id==2) doBackup();
        else if(id==3) doRestore();
        else if(id==4) doShutdown();
        else if(id==5) doRestart();
        else if(id==6) doUpdateRestart();
        else if(id==7) doLogoff();
        else if(id==16) doLock();
        else if(id==18) doCombineNonOverride();
        else if(id==19) doCheckForUpdates();
        else if(id==8) addShortcut();
        else if(id==9) createJob();
        else if(id==10) editJob();
        else if(id==11) checkJobStatus();
        else if(id==12) deleteJob();
        else if(id==13) { saveShortcuts(); SendMessageA(hStatus, WM_SETTEXT, 0, (LPARAM)"Shortcuts saved."); }
        else if(id==14) { if(!editMode) toggleEditMode(); else { editMode=false; for(auto c:shortcutChecks)ShowWindow(c,SW_HIDE); SendMessageA(hStatus,WM_SETTEXT,0,(LPARAM)"Edit cancelled."); } }
        else if(id==15) { if(!deleteMode) toggleDeleteMode(); else performDeleteSelected(); }
        else if(id >= 100) {
            int idx = id - 100;
            if(deleteMode){
                if(idx >=0 && idx < (int)shortcutChecks.size() && shortcutChecks[idx]){
                    LRESULT state = SendMessage(shortcutChecks[idx], BM_GETCHECK, 0, 0);
                    SendMessage(shortcutChecks[idx], BM_SETCHECK, state==BST_CHECKED?BST_UNCHECKED:BST_CHECKED, 0);
                }
            } else if(editMode){
                if(idx >=0 && idx < (int)shortcuts.size()){
                    performEdit(idx);
                }
            } else {
                launchShortcut(id);
            }
        }
    } break;
    case WM_DESTROY: 
            DeleteObject(hBrushRed);
            DeleteObject(hBrushYellow);
            DeleteObject(hBrushOrange);
            DeleteObject(hBrushBlue);
            DeleteObject(hBrushGreen);
            if(hBrushBg) DeleteObject(hBrushBg);
            if(hBrushBgDark) DeleteObject(hBrushBgDark);
            if(hBrushControlBg) DeleteObject(hBrushControlBg);
            if(hBrushControlBgDark) DeleteObject(hBrushControlBgDark);
            // Destroy any loaded shortcut icons
            for(auto ic : shortcutIcons) {
                if(ic) DestroyIcon(ic);
            }
            PostQuitMessage(0);
        break;
 }
 return DefWindowProc(h,m,w,l);
}

int APIENTRY WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int){ 
    // Initialize common controls for Windows visual styles
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Enable visual styles
    SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT);
    
    const char* cls="MaintToolGUI"; 
    WNDCLASSA wc{}; 
    wc.lpfnWndProc=WndProc; 
    wc.hInstance=hInst; 
    wc.lpszClassName=cls; 
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); 
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassA(&wc); 
    
    HWND hwnd=CreateWindowExA(WS_EX_COMPOSITED, cls,"Maintenance Tool GUI", WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT,CW_USEDEFAULT,410,500,NULL,NULL,hInst,NULL); 
    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create window", "Error", MB_ICONERROR);
        return 1;
    }
    // Set title bar to include version
    std::string title = std::string("Maintenance Tool GUI v") + getCurrentVersion();
    SetWindowTextA(hwnd, title.c_str());
    
    MSG msg; 
    while(GetMessage(&msg,NULL,0,0)){ 
        TranslateMessage(&msg); 
        DispatchMessage(&msg);
    } 
    return 0; 
}
