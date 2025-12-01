#include <windows.h>
#include <shellapi.h>
#include <wininet.h>
#include <string>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <thread>
#include <fstream>

#pragma comment(lib, "wininet.lib")

static void showMsg(const char* t, const std::string& m){ MessageBoxA(NULL, m.c_str(), t, MB_OK|MB_ICONINFORMATION); }
static void showError(const char* t, const std::string& m){ MessageBoxA(NULL, m.c_str(), t, MB_OK|MB_ICONERROR); }

// Download a file from URL to local path
bool downloadFile(const std::string& url, const std::string& localPath) {
    HINTERNET hInternet = InternetOpenA("Maintenance Tool Updater", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if(!hInternet) return false;
    
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if(!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }
    
    std::ofstream outFile(localPath, std::ios::binary);
    if(!outFile) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }
    
    char buffer[4096];
    DWORD bytesRead;
    while(InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        outFile.write(buffer, bytesRead);
    }
    
    outFile.close();
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
}

// Compare version strings (format: X.Y.Z)
// Returns: 1 if v1 > v2, -1 if v1 < v2, 0 if equal
int compareVersions(const std::string& v1, const std::string& v2) {
    int major1=0, minor1=0, patch1=0;
    int major2=0, minor2=0, patch2=0;
    
    sscanf_s(v1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf_s(v2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);
    
    if(major1 != major2) return major1 > major2 ? 1 : -1;
    if(minor1 != minor2) return minor1 > minor2 ? 1 : -1;
    if(patch1 != patch2) return patch1 > patch2 ? 1 : -1;
    return 0;
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

// Check for updates and prompt user
bool checkForUpdates(const std::string& currentVersion, std::string& newVersion) {
    const std::string versionUrl = "https://raw.githubusercontent.com/imasteredu2/maintenance_tools/tools/version.txt";
    const std::string tempVersionFile = "temp_version.txt";
    
    if(!downloadFile(versionUrl, tempVersionFile)) {
        return false;
    }
    
    std::ifstream vfile(tempVersionFile);
    if(!vfile) {
        std::filesystem::remove(tempVersionFile);
        return false;
    }
    
    std::getline(vfile, newVersion);
    newVersion = trim(newVersion);
    vfile.close();
    std::filesystem::remove(tempVersionFile);
    
    int cmp = compareVersions(newVersion, currentVersion);
    return cmp > 0; // true if new version is higher
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR cmd, int){
    // Check for --check-update flag
    int argc = __argc; char** argv = __argv;
    bool checkUpdate = false;
    
    for(int i=1; i<argc; i++) {
        if(strcmp(argv[i], "--check-update")==0) {
            checkUpdate = true;
            break;
        }
    }
    
    if(checkUpdate) {
        // Read current version
        std::string currentVersion = "0.0.0";
        std::ifstream vf("version.txt");
        if(vf) {
            std::getline(vf, currentVersion);
            currentVersion = trim(currentVersion);
            vf.close();
        }
        
        // Check for updates
        std::string newVersion;
        if(checkForUpdates(currentVersion, newVersion)) {
            char msg[512];
            sprintf_s(msg, "A new version is available!\n\nCurrent: %s\nNew: %s\n\nWould you like to download and install the update?\n\nThe program will close and update automatically.", 
                     currentVersion.c_str(), newVersion.c_str());
            
            int result = MessageBoxA(NULL, msg, "Update Available", MB_YESNO|MB_ICONINFORMATION);
            if(result == IDYES) {
                // Download new executables
                const std::string guiUrl = "https://github.com/imasteredu2/maintenance_tools/raw/tools/built/maintenance_tool_gui_v" + newVersion + ".exe";
                const std::string toolUrl = "https://github.com/imasteredu2/maintenance_tools/raw/tools/built/maintenance_tool_v" + newVersion + ".exe";
                
                std::string tempGui = "temp_gui.exe";
                std::string tempTool = "temp_tool.exe";
                
                showMsg("Updater", "Downloading update...");
                
                bool success = true;
                if(!downloadFile(guiUrl, tempGui)) {
                    showError("Update Failed", "Failed to download GUI update.");
                    success = false;
                }
                if(success && !downloadFile(toolUrl, tempTool)) {
                    showError("Update Failed", "Failed to download tool update.");
                    success = false;
                }
                
                if(success) {
                    // Find and close maintenance processes
                    system("taskkill /F /IM maintenance_tool_gui.exe >nul 2>&1");
                    system("taskkill /F /IM maintenance_tool.exe >nul 2>&1");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    
                    // Replace files
                    if(CopyFileA(tempGui.c_str(), "maintenance_tool_gui.exe", FALSE) &&
                       CopyFileA(tempTool.c_str(), "maintenance_tool.exe", FALSE)) {
                        
                        // Update version file
                        std::ofstream vout("version.txt");
                        if(vout) {
                            vout << newVersion;
                            vout.close();
                        }
                        
                        // Cleanup
                        std::filesystem::remove(tempGui);
                        std::filesystem::remove(tempTool);
                        
                        showMsg("Update Complete", "Update installed successfully!\n\nThe application will now restart.");
                        
                        // Relaunch GUI
                        SHELLEXECUTEINFOA sei{sizeof(sei)};
                        sei.lpVerb = "open";
                        sei.lpFile = "maintenance_tool_gui.exe";
                        sei.nShow = SW_SHOWNORMAL;
                        ShellExecuteExA(&sei);
                        
                        return 0;
                    } else {
                        showError("Update Failed", "Failed to replace executable files.");
                        std::filesystem::remove(tempGui);
                        std::filesystem::remove(tempTool);
                        return 2;
                    }
                }
                return 1;
            }
        } else {
            showMsg("No Updates", "You are running the latest version (" + currentVersion + ").");
        }
        return 0;
    }
    
    // Original updater functionality for manual updates
    std::string target, source; bool relaunch=false;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i], "--target")==0 && i+1<argc){ target=argv[++i]; }
        else if(strcmp(argv[i], "--source")==0 && i+1<argc){ source=argv[++i]; }
        else if(strcmp(argv[i], "--relaunch")==0){ relaunch=true; }
    }
    if(target.empty()||source.empty()){
        showMsg("Updater", "Usage: updater.exe --check-update\n   OR: updater.exe --target <exe> --source <new exe> [--relaunch]");
        return 1;
    }

    // Try replace with retries (in case process not released yet)
    const int maxRetries = 40; // ~20 seconds
    int attempt = 0;
    DWORD lastErr = 0;
    while(attempt < maxRetries){
        if(CopyFileA(source.c_str(), target.c_str(), FALSE)){
            lastErr = 0; break; // success
        }
        lastErr = GetLastError();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        attempt++;
    }

    if(lastErr!=0){
        char buf[512]; sprintf_s(buf, "Failed to update. WinErr=%lu", (unsigned long)lastErr);
        MessageBoxA(NULL, buf, "Updater", MB_OK|MB_ICONERROR);
        return 2;
    }

    if(relaunch){
        SHELLEXECUTEINFOA sei{sizeof(sei)};
        sei.lpVerb = "open";
        sei.lpFile = target.c_str();
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExA(&sei);
    }

    return 0;
}
