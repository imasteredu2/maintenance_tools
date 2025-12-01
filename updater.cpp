#include <windows.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <thread>

static void showMsg(const char* t, const std::string& m){ MessageBoxA(NULL, m.c_str(), t, MB_OK|MB_ICONINFORMATION); }

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR cmd, int){
    // Simple arg parser: --target "path" --source "path" [--relaunch]
    int argc = __argc; char** argv = __argv;
    std::string target, source; bool relaunch=false;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i], "--target")==0 && i+1<argc){ target=argv[++i]; }
        else if(strcmp(argv[i], "--source")==0 && i+1<argc){ source=argv[++i]; }
        else if(strcmp(argv[i], "--relaunch")==0){ relaunch=true; }
    }
    if(target.empty()||source.empty()){
        showMsg("Updater", "Usage: updater.exe --target <exe> --source <new exe> [--relaunch]");
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
