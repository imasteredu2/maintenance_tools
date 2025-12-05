#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <map>
#include <windows.h>
#include <wincrypt.h>
#include <mutex>
#include <unordered_map>

// Logging subsystem
static std::mutex gLogMutex;
static const char* LOG_FILE = "maintenance.log";
static bool gProgressFlag = false; // global progress flag
static const char* MANIFEST_DIR = "C:\\Maintenance\\Manifests";

void logLine(const std::string& action, const std::string& detail, const std::string& status) {
    std::lock_guard<std::mutex> lk(gLogMutex);
    std::ofstream out(LOG_FILE, std::ios::app);
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm; localtime_s(&tm, &t);
    char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    out << buf << " | " << action << " | " << detail << " | " << status << "\n";
}

// Simple maintenance tool
// Config file: config.txt in same directory.
// Format:
// [job:JobName]
// sources=path1|path2|...
// destinations=dest1|dest2|...
// override=0|1

struct Job {
    std::string name;
    std::vector<std::string> sources;
    std::vector<std::string> destinations;
    bool overrideFlag = false;
    bool incrementalFlag = false; // new incremental support flag
    std::vector<std::string> shortcuts; // program paths to launch
    bool scheduleEnabled = false;
    std::string scheduleInterval = "daily"; // daily, weekly, monthly, hours, mins, once
    int scheduleValue = 1; // numeric value for interval (e.g., 7 for weekly, 2 for every 2 hours)
    std::string scheduleTime = "02:00"; // HH:MM format for when to run
    std::string startDate = ""; // optional: YYYY-MM-DD HH:MM format for first backup start date/time
    std::time_t lastRun = 0; // timestamp of last backup run
    std::string scheduleDest = ""; // optional: specific destination for scheduled backups (overrides destinations if set)
    bool scheduleOneTime = false; // if true, disable after first run
    std::vector<std::string> additionalSchedules; // format: interval|value|time|dest|onetime e.g., "daily|1|02:00|D:/Backup|0"
};

static const char* CONFIG_FILE = "config.txt";

// Helper to convert file_time to time_t (C++17 compatibility)
std::time_t fileTimeToTimeT(std::filesystem::file_time_type ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return std::chrono::system_clock::to_time_t(sctp);
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::vector<Job> loadConfig() {
    std::vector<Job> jobs;
    std::ifstream in(CONFIG_FILE);
    if (!in.is_open()) return jobs;
    std::string line;
    Job current;
    bool inJob = false;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line.rfind("[job:", 0) == 0 && line.back() == ']') {
            if (inJob) {
                jobs.push_back(current);
                current = Job();
            }
            std::string nm = line.substr(5, line.size() - 6); // between [job: and ]
            current.name = trim(nm);
            inJob = true;
        } else if (inJob) {
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = trim(line.substr(0, pos));
            std::string val = trim(line.substr(pos + 1));
            if (key == "sources") current.sources = split(val, '|');
            else if (key == "destinations") current.destinations = split(val, '|');
            else if (key == "override") current.overrideFlag = (val == "1" || val == "true" || val == "TRUE" || val == "True");
            else if (key == "incremental") current.incrementalFlag = (val == "1" || val == "true" || val == "TRUE" || val == "True");
            else if (key == "shortcuts") current.shortcuts = split(val, '|');
            else if (key == "schedule_enabled") current.scheduleEnabled = (val == "1" || val == "true" || val == "TRUE" || val == "True");
            else if (key == "schedule_interval") current.scheduleInterval = val;
            else if (key == "schedule_value") current.scheduleValue = std::stoi(val);
            else if (key == "schedule_time") current.scheduleTime = val;
            else if (key == "start_date") current.startDate = val;
            else if (key == "last_run") current.lastRun = std::stoll(val);
            else if (key == "schedule_dest") current.scheduleDest = val;
            else if (key == "schedule_onetime") current.scheduleOneTime = (val == "1" || val == "true" || val == "TRUE" || val == "True");
            else if (key == "additional_schedules") current.additionalSchedules = split(val, '|');
        }
    }
    if (inJob) jobs.push_back(current);
    return jobs;
}

void saveConfig(const std::vector<Job>& jobs) {
    std::ofstream out(CONFIG_FILE, std::ios::trunc);
    for (const auto& j : jobs) {
        out << "[job:" << j.name << "]\n";
        auto join = [](const std::vector<std::string>& v) {
            std::ostringstream oss;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) oss << '|';
                oss << v[i];
            }
            return oss.str();
        };
        out << "sources=" << join(j.sources) << "\n";
        out << "destinations=" << join(j.destinations) << "\n";
        out << "override=" << (j.overrideFlag ? '1' : '0') << "\n";
        out << "incremental=" << (j.incrementalFlag ? '1' : '0') << "\n";
        out << "shortcuts=" << join(j.shortcuts) << "\n";
        out << "schedule_enabled=" << (j.scheduleEnabled ? '1' : '0') << "\n";
        out << "schedule_interval=" << j.scheduleInterval << "\n";
        out << "schedule_value=" << j.scheduleValue << "\n";
        out << "schedule_time=" << j.scheduleTime << "\n";
        out << "start_date=" << j.startDate << "\n";
        out << "last_run=" << j.lastRun << "\n";
        out << "schedule_dest=" << j.scheduleDest << "\n";
        out << "schedule_onetime=" << (j.scheduleOneTime ? '1' : '0') << "\n";
        out << "additional_schedules=" << join(j.additionalSchedules) << "\n\n";
    }
}

Job* findJob(std::vector<Job>& jobs, const std::string& name) {
    for (auto& j : jobs) if (_stricmp(j.name.c_str(), name.c_str()) == 0) return &j;
    return nullptr;
}

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

void copyFileTo(const std::filesystem::path& src, const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) std::cerr << "WARN: Failed copy " << src << " -> " << dest << " : " << ec.message() << "\n";
}

void copyDirectoryMerge(const std::filesystem::path& src, const std::filesystem::path& dest) {
    std::error_code ec;
    for (auto& entry : std::filesystem::recursive_directory_iterator(src, ec)) {
        if (ec) break;
        auto rel = std::filesystem::relative(entry.path(), src, ec);
        auto target = dest / rel;
        if (entry.is_directory()) {
            std::filesystem::create_directories(target, ec);
        } else if (entry.is_regular_file()) {
            copyFileTo(entry.path(), target);
        }
    }
    if (ec) std::cerr << "WARN: Directory iterator issue: " << ec.message() << "\n";
}

void clearDirectory(const std::filesystem::path& p) {
    if (!std::filesystem::exists(p)) return;
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(p, ec)) {
        if (entry.is_directory()) std::filesystem::remove_all(entry.path(), ec);
        else std::filesystem::remove(entry.path(), ec);
    }
}

void performBackup(const Job& job, bool dry) {
    if (job.sources.empty() || job.destinations.empty()) {
        std::cout << "Job has empty sources/destinations.\n";
        logLine("backup", job.name, "fail-empty");
        return;
    }
    std::string ts = timestamp();

    // Progress computation (aggregate size first if needed)
    uintmax_t totalBytes = 0; uintmax_t processedBytes = 0; 
    auto sizeOfPath = [](const std::filesystem::path& p){
        std::error_code ec; if(!std::filesystem::exists(p,ec)) return (uintmax_t)0; 
        if(std::filesystem::is_regular_file(p,ec)) return std::filesystem::file_size(p,ec); 
        uintmax_t sum=0; for(auto& e: std::filesystem::recursive_directory_iterator(p,ec)){ if(ec) break; if(e.is_regular_file()){ std::error_code ec2; sum+= std::filesystem::file_size(e.path(),ec2); }} return sum; };
    if (gProgressFlag && !dry) {
        for (auto& s : job.sources) totalBytes += sizeOfPath(s);
    }
    auto showProgress = [&](uintmax_t added){
        if (!gProgressFlag || dry || totalBytes==0) return; processedBytes += added; double pct = (double)processedBytes * 100.0 / (double)totalBytes; std::cout << "Progress: " << std::fixed << std::setprecision(1) << pct << "% (" << processedBytes << "/" << totalBytes << " bytes)\r"; };

    for (const auto& destBase : job.destinations) {
        std::filesystem::path destRoot(destBase);
        std::error_code ec;
        std::filesystem::create_directories(destRoot, ec);
        if (ec) {
            std::cerr << "ERROR: Cannot create destination base " << destBase << " : " << ec.message() << "\n";
            logLine("backup", job.name + " dest=" + destBase, "fail-create-dest");
            continue;
        }
        std::filesystem::path targetRoot;
        if (job.overrideFlag) {
            targetRoot = destRoot / job.name;
            if (!dry) {
                std::filesystem::create_directories(targetRoot, ec);
                // Only clear directory if NOT incremental - incremental mode will selectively update files
                if (!job.incrementalFlag) {
                    clearDirectory(targetRoot);
                }
            }
        } else {
            targetRoot = destRoot / (job.name + "_" + ts);
            if (!dry) std::filesystem::create_directories(targetRoot, ec);
        }
        if (dry) std::cout << "[DRY] Destination root: " << targetRoot.string() << "\n";

        // Load previous manifest for incremental skip logic from centralized location
        std::unordered_map<std::string, std::tuple<uintmax_t, std::time_t, std::string>> previousManifest;
        if (job.incrementalFlag) {
            std::filesystem::path manifestDir(MANIFEST_DIR);
            std::error_code ec2;
            std::filesystem::create_directories(manifestDir, ec2);
            
            // Manifest filename: JobName_DestHash.txt (hash of destination to handle multiple destinations)
            std::string destHash = std::to_string(std::hash<std::string>{}(destBase));
            std::filesystem::path manifestPath = manifestDir / (job.name + "_" + destHash + ".txt");
            
            std::cout << "Incremental mode enabled. Checking manifest: " << manifestPath.string() << "\n";
            
            if (std::filesystem::exists(manifestPath)) {
                std::ifstream mfin(manifestPath.string()); std::string mline;
                while (std::getline(mfin, mline)) {
                    auto parts = split(mline, '|');
                    if (parts.size()>=4) {
                        uintmax_t sz = std::strtoull(parts[1].c_str(), nullptr, 10);
                        std::time_t mt = std::strtoll(parts[2].c_str(), nullptr, 10);
                        previousManifest[parts[0]] = std::make_tuple(sz, mt, parts[3]); // file path -> (size, mtime, hash)
                    }
                }
                std::cout << "Loaded " << previousManifest.size() << " entries from previous manifest.\n";
            } else {
                std::cout << "No previous manifest found. This will be a full backup.\n";
            }
        }
        std::vector<std::string> manifestLines;
        std::unordered_map<std::string, std::string> manifestUpdates; // Track files we've processed (key -> manifest line)
        for (const auto& src : job.sources) {
            std::filesystem::path sp(src);
            if (!std::filesystem::exists(sp)) {
                std::cout << "WARN missing: " << src << "\n"; logLine("backup-missing", src, "warn"); continue;
            }
            std::string baseName = sp.filename().string(); auto destPath = targetRoot / baseName;
            if (dry) { std::cout << "[DRY] Copy " << sp.string() << " -> " << destPath.string() << "\n"; continue; }
            if (std::filesystem::is_regular_file(sp)) {
                std::error_code ec2; auto fsize = std::filesystem::file_size(sp, ec2); auto ftime = std::filesystem::last_write_time(sp, ec2); auto sctp = fileTimeToTimeT(ftime);
                
                bool doCopy = true;
                std::string hashHex;
                
                std::cout << "Checking file: " << baseName << " (size=" << fsize << ", mtime=" << sctp << ")\n";
                
                // Check if file exists in previous manifest
                if (job.incrementalFlag && !previousManifest.empty()) {
                    auto itprev = previousManifest.find(baseName);
                    if (itprev != previousManifest.end()) {
                        auto& [prevSize, prevMtime, prevHash] = itprev->second;
                        std::cout << "  Found in manifest: size=" << prevSize << ", mtime=" << prevMtime << "\n";
                        // If size and modification time match, assume unchanged and reuse hash
                        if (prevSize == fsize && prevMtime == sctp) {
                            doCopy = false;
                            hashHex = prevHash; // Reuse previous hash
                            std::cout << "Skipped unchanged file (mtime match): " << sp.string() << "\n";
                        } else {
                            std::cout << "  Size or mtime changed, will compute hash\n";
                        }
                    } else {
                        std::cout << "  Not found in manifest, will copy\n";
                    }
                }
                
                // Compute hash if we need to copy or if we don't have it yet
                if (hashHex.empty()) {
                    HCRYPTPROV hProv=0; HCRYPTHASH hHash=0; BYTE hv[32]; DWORD hvLen=32;
                    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
                        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                            std::ifstream fin(sp, std::ios::binary); char buf[4096]; while(fin.read(buf,sizeof(buf))) CryptHashData(hHash,(BYTE*)buf,(DWORD)fin.gcount(),0); if(fin.gcount()>0) CryptHashData(hHash,(BYTE*)buf,(DWORD)fin.gcount(),0);
                            if (CryptGetHashParam(hHash, HP_HASHVAL, hv, &hvLen, 0)) { std::ostringstream oss; for(DWORD i=0;i<hvLen;++i) oss<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)hv[i]; hashHex=oss.str(); }
                        }
                    }
                    if (hHash) CryptDestroyHash(hHash); if (hProv) CryptReleaseContext(hProv,0);
                    
                    // If mtime changed but hash is the same, still skip copy
                    if (job.incrementalFlag && !previousManifest.empty() && doCopy) {
                        auto itprev = previousManifest.find(baseName);
                        if (itprev != previousManifest.end()) {
                            auto& [prevSize, prevMtime, prevHash] = itprev->second;
                            if (prevHash == hashHex) {
                                doCopy = false;
                                std::cout << "Skipped unchanged file (hash match): " << sp.string() << "\n";
                            }
                        }
                    }
                }
                
                if (doCopy) { 
                    copyFileTo(sp, destPath); 
                    std::cout << "Copied: " << sp.string() << "\n";
                    std::error_code ec3; showProgress(std::filesystem::file_size(sp, ec3)); 
                } 
                
                // Only update manifest entry if file changed or is new
                if (doCopy || previousManifest.find(baseName) == previousManifest.end()) {
                    manifestUpdates[baseName] = baseName + "|" + std::to_string(fsize) + "|" + std::to_string(sctp) + "|" + hashHex;
                }
            } else if (std::filesystem::is_directory(sp)) {
                std::error_code ec2; std::filesystem::create_directories(destPath, ec2);
                for (auto& entry : std::filesystem::recursive_directory_iterator(sp)) {
                    if (!entry.is_regular_file()) continue; 
                    auto rel = entry.path().lexically_relative(sp).string(); 
                    std::error_code ec3; 
                    auto fsize = std::filesystem::file_size(entry.path(), ec3); 
                    auto ftime = std::filesystem::last_write_time(entry.path(), ec3); 
                    auto sctp = fileTimeToTimeT(ftime);
                    
                    bool doCopy = true;
                    std::string hashHex;
                    std::string fileKey = baseName + "/" + rel;
                    
                    // Check if file exists in previous manifest
                    if (job.incrementalFlag && !previousManifest.empty()) {
                        auto itprev = previousManifest.find(fileKey);
                        if (itprev != previousManifest.end()) {
                            auto& [prevSize, prevMtime, prevHash] = itprev->second;
                            // If size and modification time match, assume unchanged and reuse hash
                            if (prevSize == fsize && prevMtime == sctp) {
                                doCopy = false;
                                hashHex = prevHash; // Reuse previous hash
                                std::cout << "Skipped unchanged file (mtime match): " << entry.path().string() << "\n";
                            }
                        }
                    }
                    
                    // Compute hash if we need to copy or if we don't have it yet
                    if (hashHex.empty()) {
                        HCRYPTPROV hProv=0; HCRYPTHASH hHash=0; BYTE hv[32]; DWORD hvLen=32;
                        if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
                            if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                                std::ifstream fin(entry.path(), std::ios::binary); char buf[4096]; while(fin.read(buf,sizeof(buf))) CryptHashData(hHash,(BYTE*)buf,(DWORD)fin.gcount(),0); if(fin.gcount()>0) CryptHashData(hHash,(BYTE*)buf,(DWORD)fin.gcount(),0);
                                if (CryptGetHashParam(hHash, HP_HASHVAL, hv, &hvLen, 0)) { std::ostringstream oss; for(DWORD i=0;i<hvLen;++i) oss<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)hv[i]; hashHex=oss.str(); }
                            }
                        }
                        if (hHash) CryptDestroyHash(hHash); if (hProv) CryptReleaseContext(hProv,0);
                        
                        // If mtime changed but hash is the same, still skip copy
                        if (job.incrementalFlag && !previousManifest.empty() && doCopy) {
                            auto itprev = previousManifest.find(fileKey);
                            if (itprev != previousManifest.end()) {
                                auto& [prevSize, prevMtime, prevHash] = itprev->second;
                                if (prevHash == hashHex) {
                                    doCopy = false;
                                    std::cout << "Skipped unchanged file (hash match): " << entry.path().string() << "\n";
                                }
                            }
                        }
                    }
                    
                    if (doCopy) {
                        auto destFile = destPath / rel;
                        std::filesystem::create_directories(destFile.parent_path(), ec3);
                        copyFileTo(entry.path(), destFile);
                        std::cout << "Copied: " << entry.path().string() << "\n";
                    }
                    
                    // Only update manifest entry if file changed or is new
                    if (doCopy || previousManifest.find(fileKey) == previousManifest.end()) {
                        manifestUpdates[fileKey] = fileKey + "|" + std::to_string(fsize) + "|" + std::to_string(sctp) + "|" + hashHex;
                    }
                    showProgress(fsize);
                }
            }
        }
        if (!dry) { 
            // Save manifest to centralized location
            std::filesystem::path manifestDir(MANIFEST_DIR);
            std::error_code ec4;
            std::filesystem::create_directories(manifestDir, ec4);
            std::string destHash = std::to_string(std::hash<std::string>{}(destBase));
            std::filesystem::path manifestPath = manifestDir / (job.name + "_" + destHash + ".txt");
            std::ofstream mout(manifestPath.string(), std::ios::trunc);
            
            // Write updated/new entries from manifestUpdates
            for (auto& [key, line] : manifestUpdates) {
                mout << line << '\n';
            }
            
            // Write unchanged entries from previous manifest
            for (auto& [key, data] : previousManifest) {
                if (manifestUpdates.find(key) == manifestUpdates.end()) {
                    auto& [sz, mt, hash] = data;
                    mout << key << "|" << sz << "|" << mt << "|" << hash << '\n';
                }
            }
        }
        std::cout << "Backup to " << destBase << " completed (override=" << (job.overrideFlag?"true":"false") << ", incremental=" << (job.incrementalFlag?"true":"false") << ", dry=" << (dry?"true":"false") << ").\n"; logLine("backup", job.name + " dest=" + destBase, "success"); if(gProgressFlag && !dry) std::cout << "\n";
    }
}

bool confirm(const std::string& action, bool force) {
    if (force) return true;
    std::cout << action << "? Type YES to confirm: ";
    std::string resp; std::getline(std::cin, resp);
    return resp == "YES";
}

void systemShutdown(bool force) {
    if (!confirm("Shutdown", force)) { std::cout << "Cancelled.\n"; return; }
    system("shutdown /s /t 0");
    logLine("shutdown", "initiated", "sent");
}

void systemRestart(bool force) {
    if (!confirm("Restart", force)) { std::cout << "Cancelled.\n"; return; }
    system("shutdown /r /t 0");
    logLine("restart", "initiated", "sent");
}

void systemLogoff(bool force) {
    if (!confirm("Logoff", force)) { std::cout << "Cancelled.\n"; return; }
    system("shutdown /l");
    logLine("logoff", "initiated", "sent");
}

void systemUpdateRestart(bool force) {
    if (!confirm("Update and Restart (may require admin)", force)) { std::cout << "Cancelled.\n"; return; }
    system("UsoClient StartScan");
    system("UsoClient StartDownload");
    system("UsoClient StartInstall");
    system("UsoClient RestartDevice");
    logLine("update-restart", "uso-sequence", "sent");
}

void addJobInteractive() {
    auto jobs = loadConfig();
    std::string name;
    std::cout << "Enter job name: ";
    std::getline(std::cin, name);
    name = trim(name);
    if (name.empty()) { std::cout << "Job name required.\n"; return; }
    if (findJob(jobs, name)) { std::cout << "Job already exists.\n"; return; }
    std::vector<std::string> sources;
    std::cout << "Enter source paths (blank to finish):\n";
    while (true) {
        std::string line; std::cout << "Source> "; std::getline(std::cin, line); line = trim(line); if (line.empty()) break; sources.push_back(line);
    }
    std::vector<std::string> destinations;
    std::cout << "Enter destination folder paths (blank to finish):\n";
    while (true) {
        std::string line; std::cout << "Dest> "; std::getline(std::cin, line); line = trim(line); if (line.empty()) break; destinations.push_back(line);
    }
    std::string ov;
    std::cout << "Override previous backups? (y/N): ";
    std::getline(std::cin, ov);
    bool overrideFlag = (!ov.empty() && (ov[0] == 'y' || ov[0] == 'Y'));
    std::string inc;
    std::cout << "Enable incremental backups? (y/N): ";
    std::getline(std::cin, inc);
    bool incrementalFlag = (!inc.empty() && (inc[0] == 'y' || inc[0] == 'Y'));
    std::vector<std::string> shortcuts;
    std::cout << "Enter program shortcuts (full paths to .exe files, blank to finish):\n";
    while (true) {
        std::string line; std::cout << "Shortcut> "; std::getline(std::cin, line); line = trim(line); if (line.empty()) break; shortcuts.push_back(line);
    }
    bool scheduleEnabled = false;
    std::string scheduleInterval = "daily";
    int scheduleValue = 1;
    std::string scheduleTime = "02:00";
    std::string schedEnable;
    std::cout << "Enable automatic scheduled backups? (y/N): ";
    std::getline(std::cin, schedEnable);
    if (!schedEnable.empty() && (schedEnable[0] == 'y' || schedEnable[0] == 'Y')) {
        scheduleEnabled = true;
        std::cout << "Schedule interval (mins/hours/daily/weekly/monthly/once): ";
        std::getline(std::cin, scheduleInterval);
        scheduleInterval = trim(scheduleInterval);
        if (scheduleInterval.empty()) scheduleInterval = "daily";
        std::cout << "Interval value (e.g., 2 for every 2 hours, 7 for every 7 days): ";
        std::string valStr; std::getline(std::cin, valStr);
        scheduleValue = valStr.empty() ? 1 : std::stoi(valStr);
        std::cout << "Time to run (HH:MM format, e.g., 02:00 for 2 AM): ";
        std::getline(std::cin, scheduleTime);
        scheduleTime = trim(scheduleTime);
        if (scheduleTime.empty()) scheduleTime = "02:00";
        std::cout << "Schedule-specific destination (blank to use job destinations): ";
        std::string schedDest; std::getline(std::cin, schedDest);
        schedDest = trim(schedDest);
        std::cout << "One-time schedule (disable after first run)? (y/N): ";
        std::string oneTimeStr; std::getline(std::cin, oneTimeStr);
        bool scheduleOneTime = (!oneTimeStr.empty() && (oneTimeStr[0] == 'y' || oneTimeStr[0] == 'Y'));
        
        // Ask for additional schedules
        std::vector<std::string> additionalSchedules;
        std::cout << "Add additional schedules for this job? (y/N): ";
        std::string addMoreStr; std::getline(std::cin, addMoreStr);
        while (!addMoreStr.empty() && (addMoreStr[0] == 'y' || addMoreStr[0] == 'Y')) {
            std::string addInterval, addTime, addDest;
            int addValue = 1;
            std::cout << "  Interval (mins/hours/daily/weekly/monthly/once): ";
            std::getline(std::cin, addInterval); addInterval = trim(addInterval);
            std::cout << "  Interval value: ";
            std::string addValStr; std::getline(std::cin, addValStr);
            addValue = addValStr.empty() ? 1 : std::stoi(addValStr);
            std::cout << "  Time (HH:MM): ";
            std::getline(std::cin, addTime); addTime = trim(addTime);
            std::cout << "  Destination path: ";
            std::getline(std::cin, addDest); addDest = trim(addDest);
            std::cout << "  One-time? (y/N): ";
            std::string addOneStr; std::getline(std::cin, addOneStr);
            std::string addOne = (!addOneStr.empty() && (addOneStr[0] == 'y' || addOneStr[0] == 'Y')) ? "1" : "0";
            std::string schedEntry = addInterval + "|" + std::to_string(addValue) + "|" + addTime + "|" + addDest + "|" + addOne + "|0"; // last 0 is lastRun
            additionalSchedules.push_back(schedEntry);
            std::cout << "Add another schedule? (y/N): ";
            std::getline(std::cin, addMoreStr);
        }
        
        Job j; j.name = name; j.sources = sources; j.destinations = destinations; j.overrideFlag = overrideFlag; j.incrementalFlag = incrementalFlag; j.shortcuts = shortcuts;
        j.scheduleEnabled = scheduleEnabled; j.scheduleInterval = scheduleInterval; j.scheduleValue = scheduleValue; j.scheduleTime = scheduleTime; j.lastRun = 0;
        j.scheduleDest = schedDest; j.scheduleOneTime = scheduleOneTime; j.additionalSchedules = additionalSchedules;
        jobs.push_back(j);
    } else {
        Job j; j.name = name; j.sources = sources; j.destinations = destinations; j.overrideFlag = overrideFlag; j.incrementalFlag = incrementalFlag; j.shortcuts = shortcuts;
        j.scheduleEnabled = false; j.scheduleInterval = "daily"; j.scheduleValue = 1; j.scheduleTime = "02:00"; j.lastRun = 0;
        j.scheduleDest = ""; j.scheduleOneTime = false; j.additionalSchedules = {};
        jobs.push_back(j);
    }
    saveConfig(jobs);
    std::cout << "Job saved: " << name << "\n";
    logLine("add-job", name, "success");
}

void listJobs() {
    auto jobs = loadConfig();
    if (jobs.empty()) { std::cout << "No jobs defined.\n"; return; }
    for (const auto& j : jobs) {
        std::cout << "- " << j.name << "\n";
        std::cout << "    Sources (" << j.sources.size() << "):\n";
        for (auto& s : j.sources) std::cout << "      " << s << "\n";
        std::cout << "    Destinations (" << j.destinations.size() << "):\n";
        for (auto& d : j.destinations) std::cout << "      " << d << "\n";
        std::cout << "    Override: " << (j.overrideFlag?"true":"false") << "\n";
        std::cout << "    Incremental: " << (j.incrementalFlag?"true":"false") << "\n";
        std::cout << "    Shortcuts (" << j.shortcuts.size() << "):\n";
        for (auto& s : j.shortcuts) std::cout << "      " << s << "\n";
        std::cout << "    Schedule: " << (j.scheduleEnabled?"enabled":"disabled");
        if (j.scheduleEnabled) {
            std::cout << " - every " << j.scheduleValue << " " << j.scheduleInterval << " at " << j.scheduleTime;
            if (!j.scheduleDest.empty()) std::cout << " -> " << j.scheduleDest;
            if (j.scheduleOneTime) std::cout << " (one-time)";
            if (j.lastRun > 0) {
                std::time_t lr = j.lastRun; std::tm tm; localtime_s(&tm, &lr); char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
                std::cout << " (last: " << buf << ")";
            }
        }
        std::cout << "\n";
        if (!j.additionalSchedules.empty()) {
            std::cout << "    Additional Schedules (" << j.additionalSchedules.size() << "):\n";
            for (auto& sched : j.additionalSchedules) {
                auto parts = split(sched, '|');
                if (parts.size() >= 5) {
                    std::cout << "      - every " << parts[1] << " " << parts[0] << " at " << parts[2];
                    if (!parts[3].empty()) std::cout << " -> " << parts[3];
                    if (parts[4] == "1") std::cout << " (one-time)";
                    if (parts.size() >= 6 && std::stoll(parts[5]) > 0) {
                        std::time_t lr = std::stoll(parts[5]); std::tm tm; localtime_s(&tm, &lr); char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
                        std::cout << " (last: " << buf << ")";
                    }
                    std::cout << "\n";
                }
            }
        }
    }
}

// List backups for job across destinations
void listBackups(const Job& job) {
    for (auto& destBase : job.destinations) {
        std::filesystem::path destRoot(destBase);
        if (!std::filesystem::exists(destRoot)) continue;
        std::cout << "Destination: " << destBase << "\n";
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(destRoot, ec)) {
            if (!e.is_directory()) continue;
            auto fname = e.path().filename().string();
            if (job.overrideFlag && fname == job.name) {
                std::cout << "  [override] " << fname << "\n";
            } else if (fname.rfind(job.name + "_", 0) == 0) {
                std::cout << "  " << fname << "\n";
            }
        }
    }
}

bool zipFolder(const std::filesystem::path& folder) {
    auto zipPath = folder.string() + ".zip";
    std::string cmd = "powershell -Command \"Compress-Archive -Path '" + folder.string() + "' -DestinationPath '" + zipPath + "' -Force\"";
    int rc = system(cmd.c_str());
    logLine("zip", folder.filename().string(), rc==0?"success":"fail");
    return rc==0;
}

void restoreJob(const Job& job, const std::string& version, const std::string& sourceDest, bool force) {
    std::filesystem::path chosenDestBase;
    if (!sourceDest.empty()) chosenDestBase = sourceDest; else if (!job.destinations.empty()) chosenDestBase = job.destinations.front();
    if (chosenDestBase.empty()) { std::cout << "No destination available for restore.\n"; logLine("restore", job.name, "fail-no-dest"); return; }
    std::filesystem::path destRoot(chosenDestBase);
    if (!std::filesystem::exists(destRoot)) { std::cout << "Destination root missing.\n"; logLine("restore", job.name, "fail-dest-missing"); return; }
    std::filesystem::path backupFolder;
    if (job.overrideFlag) {
        backupFolder = destRoot / job.name;
    } else {
        if (!version.empty()) {
            backupFolder = destRoot / (job.name + "_" + version);
        } else {
            std::filesystem::path latest;
            std::error_code ec;
            for (auto& e : std::filesystem::directory_iterator(destRoot, ec)) {
                if (!e.is_directory()) continue;
                auto fname = e.path().filename().string();
                if (fname.rfind(job.name + "_", 0) == 0) {
                    if (latest.empty() || fname > latest.filename().string()) latest = e.path();
                }
            }
            backupFolder = latest;
        }
    }
    if (backupFolder.empty() || !std::filesystem::exists(backupFolder)) { std::cout << "Backup folder not found.\n"; logLine("restore", job.name, "fail-folder-not-found"); return; }
    if (!confirm("Restore from " + backupFolder.string(), force)) { std::cout << "Cancelled.\n"; return; }
    for (auto& srcOrig : job.sources) {
        std::filesystem::path srcPath(srcOrig);
        std::string baseName = srcPath.filename().string();
        std::filesystem::path backupItem = backupFolder / baseName;
        if (!std::filesystem::exists(backupItem)) {
            std::cout << "Missing in backup: " << backupItem.string() << "\n";
            logLine("restore-miss", backupItem.string(), "warn");
            continue;
        }
        if (std::filesystem::is_regular_file(backupItem)) {
            copyFileTo(backupItem, srcPath);
        } else if (std::filesystem::is_directory(backupItem)) {
            std::error_code ec;
            std::filesystem::create_directories(srcPath, ec);
            copyDirectoryMerge(backupItem, srcPath);
        }
        std::cout << "Restored: " << srcOrig << "\n";
        logLine("restore-item", srcOrig, "ok");
    }
    std::cout << "Restore completed from " << backupFolder.string() << "\n";
    logLine("restore", job.name, "success");
}

void launchShortcuts(const Job& job) {
    if (job.shortcuts.empty()) {
        std::cout << "No shortcuts defined for job " << job.name << ".\n";
        logLine("launch-shortcuts", job.name, "no-shortcuts");
        return;
    }
    for (const auto& shortcut : job.shortcuts) {
        std::filesystem::path progPath(shortcut);
        if (!std::filesystem::exists(progPath)) {
            std::cout << "WARN: Program not found: " << shortcut << "\n";
            logLine("launch-shortcut", shortcut, "not-found");
            continue;
        }
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        std::string cmdLine = '"' + shortcut + '"';
        if (CreateProcessA(NULL, cmdLine.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            std::cout << "Launched: " << shortcut << "\n";
            logLine("launch-shortcut", shortcut, "success");
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            std::cout << "Failed to launch: " << shortcut << "\n";
            logLine("launch-shortcut", shortcut, "failed");
        }
    }
}

void selfTest() {
    std::cout << "Running self-test...\n";
    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    std::filesystem::path srcDir = std::filesystem::path(tmpPath) / ("mnt_src_" + timestamp());
    std::filesystem::create_directories(srcDir);
    std::filesystem::path srcFile = srcDir / "sample.txt";
    {
        std::ofstream f(srcFile); f << "sample data"; }
    std::filesystem::path destDir = std::filesystem::path(tmpPath) / ("mnt_dest_" + timestamp());
    std::filesystem::create_directories(destDir);
    Job j; j.name = "TEST"; j.sources = { srcDir.string(), srcFile.string() }; j.destinations = { destDir.string() }; j.overrideFlag = false;
    performBackup(j, false);
    bool ok = std::filesystem::exists(destDir);
    std::cout << (ok?"Self-test completed." : "Self-test failed.") << "\n";
}

// Check if a scheduled job should run based on interval/time
bool shouldRunScheduled(Job& job) {
    if (!job.scheduleEnabled) return false;
    if (job.scheduleOneTime && job.lastRun > 0) return false; // one-time already ran
    
    auto now = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    std::tm nowTm; localtime_s(&nowTm, &nowT);
    
    // Check start date if specified (format: YYYY-MM-DD HH:MM)
    if (!job.startDate.empty()) {
        // Simple comparison: if we haven't reached the start date/time yet, don't run
        // For now, we'll do basic parsing - a full implementation would parse the date properly
        // This is a placeholder - proper date parsing should be added
        // TODO: Implement proper date/time parsing for startDate
    }
    
    // Parse schedule time HH:MM
    int schedHour = 0, schedMin = 0;
    if (job.scheduleTime.find(':') != std::string::npos) {
        auto parts = split(job.scheduleTime, ':');
        if (parts.size() == 2) {
            schedHour = std::stoi(parts[0]);
            schedMin = std::stoi(parts[1]);
        }
    }
    
    // For "once" interval - run if never run before or if scheduled time has passed
    if (job.scheduleInterval == "once") {
        if (job.lastRun > 0) return false; // already ran
        int currentMinutes = nowTm.tm_hour * 60 + nowTm.tm_min;
        int scheduledMinutes = schedHour * 60 + schedMin;
        return currentMinutes >= scheduledMinutes; // run once time arrives
    }
    
    // For daily/weekly/monthly - use day-based calculation with time-of-day check
    if (job.scheduleInterval == "daily" || job.scheduleInterval == "weekly" || job.scheduleInterval == "monthly") {
        // If never run, run it now if we're past the scheduled time today
        if (job.lastRun == 0) {
            int currentMinutes = nowTm.tm_hour * 60 + nowTm.tm_min;
            int scheduledMinutes = schedHour * 60 + schedMin;
            return currentMinutes >= scheduledMinutes;
        }
        
        // Get last run date (strip time component)
        std::tm lastRunTm; localtime_s(&lastRunTm, &job.lastRun);
        
        // Calculate days since last run
        int daysSinceRun = 0;
        if (job.scheduleInterval == "daily") {
            daysSinceRun = (nowTm.tm_year - lastRunTm.tm_year) * 365 + (nowTm.tm_yday - lastRunTm.tm_yday);
        } else if (job.scheduleInterval == "weekly") {
            daysSinceRun = (nowTm.tm_year - lastRunTm.tm_year) * 365 + (nowTm.tm_yday - lastRunTm.tm_yday);
        } else if (job.scheduleInterval == "monthly") {
            // Approximate month calculation
            int monthsDiff = (nowTm.tm_year - lastRunTm.tm_year) * 12 + (nowTm.tm_mon - lastRunTm.tm_mon);
            daysSinceRun = monthsDiff * 30; // rough estimate
        }
        
        // Check if enough days have passed
        int requiredDays = job.scheduleValue;
        if (job.scheduleInterval == "weekly") requiredDays *= 7;
        if (job.scheduleInterval == "monthly") requiredDays *= 30;
        
        if (daysSinceRun < requiredDays) return false;
        
        // Enough days passed - now check if we're at or past the scheduled time today
        int currentMinutes = nowTm.tm_hour * 60 + nowTm.tm_min;
        int scheduledMinutes = schedHour * 60 + schedMin;
        return currentMinutes >= scheduledMinutes;
    }
    
    // For mins/hours - simple time-based calculation
    if (job.scheduleInterval == "mins" || job.scheduleInterval == "hours") {
        // If never run, run immediately (don't check schedule time for interval-based schedules)
        if (job.lastRun == 0) {
            return true;
        }
        
        // Calculate next run based on interval
        std::time_t nextRun = job.lastRun;
        if (job.scheduleInterval == "mins") {
            nextRun += job.scheduleValue * 60;
        } else {
            nextRun += job.scheduleValue * 3600;
        }
        
        return nowT >= nextRun;
    }
    
    return false; // unknown interval type
}

// Run scheduler - check all jobs and run due backups
void runScheduler() {
    auto jobs = loadConfig();
    bool ranAny = false;
    for (auto& job : jobs) {
        // Check primary schedule
        if (shouldRunScheduled(job)) {
            std::cout << "Running scheduled backup for job: " << job.name << "\n";
            logLine("scheduler", job.name, "triggered");
            
            // Temporarily override destinations if schedule-specific dest is set
            auto origDests = job.destinations;
            if (!job.scheduleDest.empty()) {
                job.destinations = {job.scheduleDest};
            }
            
            performBackup(job, false);
            job.destinations = origDests; // restore
            
            job.lastRun = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            if (job.scheduleOneTime) {
                job.scheduleEnabled = false; // disable after one-time run
                std::cout << "One-time schedule completed, disabled for job: " << job.name << "\n";
            }
            ranAny = true;
        }
        
        // Check additional schedules
        for (size_t i = 0; i < job.additionalSchedules.size(); ++i) {
            auto& sched = job.additionalSchedules[i];
            auto parts = split(sched, '|');
            if (parts.size() < 6) continue;
            
            std::string interval = parts[0];
            if (interval == "disabled") continue; // skip disabled schedules
            
            int value = std::stoi(parts[1]);
            std::string timeStr = parts[2];
            std::string dest = parts[3];
            bool oneTime = (parts[4] == "1");
            std::time_t lastRun = std::stoll(parts[5]);
            
            // Create temporary job for schedule check
            Job tempJob = job;
            tempJob.scheduleInterval = interval;
            tempJob.scheduleValue = value;
            tempJob.scheduleTime = timeStr;
            tempJob.lastRun = lastRun;
            tempJob.scheduleOneTime = oneTime;
            tempJob.scheduleEnabled = true;
            
            if (shouldRunScheduled(tempJob)) {
                std::cout << "Running additional schedule " << (i+1) << " for job: " << job.name << "\n";
                logLine("scheduler-additional", job.name + " schedule=" + std::to_string(i+1), "triggered");
                
                auto origDests = job.destinations;
                if (!dest.empty()) job.destinations = {dest};
                
                performBackup(job, false);
                job.destinations = origDests;
                
                // Update lastRun in additional schedule entry
                parts[5] = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
                if (oneTime) {
                    // Mark as disabled by setting interval to empty or remove
                    parts[0] = "disabled";
                    std::cout << "One-time additional schedule completed for job: " << job.name << "\n";
                }
                job.additionalSchedules[i] = parts[0] + "|" + parts[1] + "|" + parts[2] + "|" + parts[3] + "|" + parts[4] + "|" + parts[5];
                ranAny = true;
            }
        }
    }
    if (ranAny) {
        saveConfig(jobs);
        std::cout << "Scheduler run completed.\n";
    } else {
        std::cout << "No scheduled backups due at this time.\n";
    }
}

void combineNonOverride(const Job& job) {
    std::cout << "Combining non-override backups for job: " << job.name << "\n";
    
    for (const auto& destBase : job.destinations) {
        std::filesystem::path destRoot(destBase);
        
        // Find all timestamped backup folders for this job
        std::map<std::string, std::filesystem::path> backupFolders; // timestamp -> path
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(destRoot, ec)) {
            if (!e.is_directory()) continue;
            auto fname = e.path().filename().string();
            if (fname.rfind(job.name + "_", 0) == 0) {
                std::string ts = fname.substr(job.name.length() + 1);
                backupFolders[ts] = e.path();
            }
        }
        
        if (backupFolders.empty()) {
            std::cout << "No non-override backups found in " << destBase << "\n";
            continue;
        }
        
        std::cout << "Found " << backupFolders.size() << " backup folders to combine.\n";
        
        // Create combined folder
        std::filesystem::path combinedPath = destRoot / (job.name + "_combined");
        std::filesystem::create_directories(combinedPath, ec);
        
        // Process folders in chronological order (oldest first, newest last)
        for (const auto& [ts, folder] : backupFolders) {
            std::cout << "Processing: " << folder.filename().string() << "\n";
            
            // Copy all files from this backup folder to combined folder
            for (auto& entry : std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file()) continue;
                
                auto rel = entry.path().lexically_relative(folder);
                auto destFile = combinedPath / rel;
                
                // Create parent directories if needed
                std::filesystem::create_directories(destFile.parent_path(), ec);
                
                // Copy file (will overwrite if exists - newer versions overwrite older)
                std::error_code ec2;
                std::filesystem::copy_file(entry.path(), destFile, std::filesystem::copy_options::overwrite_existing, ec2);
                if (ec2) {
                    std::cerr << "Error copying " << entry.path() << ": " << ec2.message() << "\n";
                }
            }
        }
        
        std::cout << "Combined backup created at: " << combinedPath << "\n";
        logLine("combine", job.name + " dest=" + destBase, "success");
    }
}

void printHelp() {
    std::cout << "Business Maintenance Tool (C++)\n";
    std::cout << "Usage: maintenance_tool.exe [options]\n\n";
    std::cout << "--add-job              Add a backup job interactively\n";
    std::cout << "--list-jobs            List defined jobs\n";
    std::cout << "--backup JOB           Perform backup for JOB\n";
    std::cout << "--dry-run              Simulate backup without copying\n";
    std::cout << "--zip                  Zip the created backup folder (not dry)\n";
    std::cout << "--list-backups JOB     List backup folders for JOB\n";
    std::cout << "--restore JOB          Restore latest (or specified --version) for JOB\n";
    std::cout << "--combine JOB          Combine all non-override backups into one folder\n";
    std::cout << "--version TS           Timestamp for restore (YYYYMMDD_HHMMSS)\n";
    std::cout << "--source-dest PATH     Destination base to use for restore\n";
    std::cout << "--verify JOB           Verify integrity of latest (or --version) backup\n";
    std::cout << "--launch JOB           Launch all program shortcuts for JOB\n";
    std::cout << "--run-scheduler        Check and run any scheduled backups that are due\n";
    std::cout << "--daemon MINS          Run as background daemon, checking every MINS minutes\n";
    std::cout << "--progress             Show progress during backup\n";
    std::cout << "--shutdown             Shutdown computer (asks confirm)\n";
    std::cout << "--restart              Restart computer (asks confirm)\n";
    std::cout << "--logoff               Log off current user (asks confirm)\n";
    std::cout << "--update-restart       Run Windows Update then restart\n";
    std::cout << "--force                Skip confirmation prompts\n";
    std::cout << "--self-test            Run internal backup self-test\n";
    std::cout << "--help                 Show this help\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    bool force = std::find(args.begin(), args.end(), "--force") != args.end();
    if (args.empty()) { printHelp(); return 0; }
    auto has = [&](const std::string& a){ return std::find(args.begin(), args.end(), a) != args.end(); };
    if (has("--help")) { printHelp(); return 0; }
    if (has("--add-job")) { addJobInteractive(); }
    if (has("--list-jobs")) { listJobs(); }
    if (has("--progress")) gProgressFlag = true;
    auto itBackup = std::find(args.begin(), args.end(), "--backup");
    if (itBackup != args.end()) {
        if (itBackup + 1 == args.end()) {
            std::cout << "--backup requires job name.\n";
        } else {
            std::string jobName = *(itBackup + 1);
            auto jobs = loadConfig();
            Job* j = findJob(jobs, jobName);
            if (!j) {
                std::cout << "Job not found: " << jobName << "\n";
            } else {
                bool dry = has("--dry-run");
                performBackup(*j, dry);
                if (!dry && has("--zip")) {
                    std::filesystem::path targetFolder;
                    if (j->overrideFlag) {
                        targetFolder = std::filesystem::path(j->destinations.front()) / j->name;
                    } else {
                        std::filesystem::path destRoot(j->destinations.front());
                        std::error_code ec;
                        std::filesystem::path latest;
                        for (auto& e : std::filesystem::directory_iterator(destRoot, ec)) {
                            if (!e.is_directory()) continue;
                            auto fname = e.path().filename().string();
                            if (fname.rfind(j->name + "_", 0) == 0) {
                                if (latest.empty() || fname > latest.filename().string()) latest = e.path();
                            }
                        }
                        targetFolder = latest;
                    }
                    if (!targetFolder.empty() && std::filesystem::exists(targetFolder)) {
                        zipFolder(targetFolder);
                    } else {
                        std::cout << "Zip skipped: backup folder not found.\n";
                        logLine("zip", j->name, "fail-folder-not-found");
                    }
                }
            }
        }
    }
    auto itLB = std::find(args.begin(), args.end(), "--list-backups");
    if (itLB != args.end()) {
        if (itLB + 1 == args.end()) std::cout << "--list-backups requires job name.\n"; else {
            std::string jobName = *(itLB + 1);
            auto jobs = loadConfig();
            Job* j = findJob(jobs, jobName);
            if (!j) std::cout << "Job not found: " << jobName << "\n"; else listBackups(*j);
        }
    }
    auto itLaunch = std::find(args.begin(), args.end(), "--launch");
    if (itLaunch != args.end()) {
        if (itLaunch + 1 == args.end()) std::cout << "--launch requires job name.\n"; else {
            std::string jobName = *(itLaunch + 1);
            auto jobs = loadConfig();
            Job* j = findJob(jobs, jobName);
            if (!j) std::cout << "Job not found: " << jobName << "\n"; else launchShortcuts(*j);
        }
    }
    auto itCombine = std::find(args.begin(), args.end(), "--combine");
    if (itCombine != args.end()) {
        if (itCombine + 1 == args.end()) {
            std::cout << "--combine requires job name.\n";
        } else {
            std::string jobName = *(itCombine + 1);
            auto jobs = loadConfig();
            Job* j = findJob(jobs, jobName);
            if (!j) {
                std::cout << "Job not found: " << jobName << "\n";
            } else {
                combineNonOverride(*j);
            }
        }
    }
    auto itRestore = std::find(args.begin(), args.end(), "--restore");
    if (itRestore != args.end()) {
        if (itRestore + 1 == args.end()) std::cout << "--restore requires job name.\n"; else {
            std::string jobName = *(itRestore + 1);
            std::string version;
            std::string sourceDest;
            auto itVer = std::find(args.begin(), args.end(), "--version");
            if (itVer != args.end() && itVer + 1 != args.end()) version = *(itVer + 1);
            auto itSD = std::find(args.begin(), args.end(), "--source-dest");
            if (itSD != args.end() && itSD + 1 != args.end()) sourceDest = *(itSD + 1);
            auto jobs = loadConfig();
            Job* j = findJob(jobs, jobName);
            if (!j) std::cout << "Job not found: " << jobName << "\n"; else restoreJob(*j, version, sourceDest, force);
        }
    }
    // Verify
    auto itVerify = std::find(args.begin(), args.end(), "--verify");
    if (itVerify != args.end()) {
        if (itVerify + 1 == args.end()) std::cout << "--verify requires job name.\n"; else {
            std::string jobName = *(itVerify + 1);
            std::string version; auto itVer = std::find(args.begin(), args.end(), "--version"); if (itVer != args.end() && itVer + 1 != args.end()) version = *(itVer + 1);
            auto jobs = loadConfig(); Job* j = findJob(jobs, jobName);
            if (!j) std::cout << "Job not found: " << jobName << "\n"; else {
                // Determine backup folder similar to restore
                std::filesystem::path destRoot(j->destinations.empty()?"":j->destinations.front());
                if (destRoot.empty() || !std::filesystem::exists(destRoot)) { std::cout << "No destination root found for verify.\n"; }
                else {
                    std::filesystem::path backupFolder;
                    if (j->overrideFlag) backupFolder = destRoot / j->name; else {
                        if (!version.empty()) backupFolder = destRoot / (j->name + "_" + version); else {
                            std::filesystem::path latest; std::error_code ec;
                            for (auto& e : std::filesystem::directory_iterator(destRoot, ec)) { if (!e.is_directory()) continue; auto fname = e.path().filename().string(); if (fname.rfind(j->name + "_", 0) == 0) { if (latest.empty() || fname > latest.filename().string()) latest = e.path(); } }
                            backupFolder = latest;
                        }
                    }
                    if (backupFolder.empty() || !std::filesystem::exists(backupFolder)) { std::cout << "Backup folder not found for verify.\n"; }
                    else {
                        auto manifestPath = backupFolder / "manifest.txt";
                        if (!std::filesystem::exists(manifestPath)) { std::cout << "Manifest missing.\n"; }
                        else {
                            std::ifstream mfin(manifestPath.string()); std::string line; size_t total=0, okCount=0, mismatch=0, missing=0;
                            while (std::getline(mfin, line)) {
                                auto parts = split(line, '|'); if (parts.size()<3) continue; ++total; std::string rel = parts[0]; uintmax_t recordedSize = std::strtoull(parts[1].c_str(), nullptr, 10); std::string recordedHash = parts.size()>=4?parts[3]:""; std::filesystem::path filePath;
                                auto slashPos = rel.find('/'); if (slashPos == std::string::npos) filePath = backupFolder / rel; else filePath = backupFolder / rel.substr(0, slashPos) / rel.substr(slashPos+1);
                                if (!std::filesystem::exists(filePath)) { ++missing; continue; }
                                std::error_code ecSz; auto actualSize = std::filesystem::file_size(filePath, ecSz);
                                std::string hashHex; HCRYPTPROV hProv=0; HCRYPTHASH hHash=0; BYTE hv[32]; DWORD hvLen=32;
                                if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
                                    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                                        std::ifstream fin(filePath, std::ios::binary); char buf[4096]; while(fin.read(buf,sizeof(buf))) CryptHashData(hHash,(BYTE*)buf,(DWORD)fin.gcount(),0); if(fin.gcount()>0) CryptHashData(hHash,(BYTE*)buf,(DWORD)fin.gcount(),0);
                                        if (CryptGetHashParam(hHash, HP_HASHVAL, hv, &hvLen, 0)) { std::ostringstream oss; for(DWORD i=0;i<hvLen;++i) oss<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)hv[i]; hashHex=oss.str(); }
                                    }
                                }
                                if (hHash) CryptDestroyHash(hHash); if (hProv) CryptReleaseContext(hProv,0);
                                bool sizeOk = (recordedSize == actualSize); bool hashOk = (recordedHash.empty() || recordedHash == hashHex);
                                if (sizeOk && hashOk) ++okCount; else ++mismatch;
                            }
                            std::cout << "Verify results: entries=" << total << " ok=" << okCount << " missing=" << missing << " mismatch=" << mismatch << "\n";
                            logLine("verify", jobName, (mismatch==0 && missing==0)?"success":"issues");
                        }
                    }
                }
            }
        }
    }
    if (has("--shutdown")) systemShutdown(force);
    if (has("--restart")) systemRestart(force);
    if (has("--logoff")) systemLogoff(force);
    if (has("--update-restart")) systemUpdateRestart(force);
    if (has("--self-test")) selfTest();
    if (has("--run-scheduler")) runScheduler();
    auto itDaemon = std::find(args.begin(), args.end(), "--daemon");
    if (itDaemon != args.end()) {
        int intervalMins = 60; // default 60 minutes
        if (itDaemon + 1 != args.end()) {
            intervalMins = std::stoi(*(itDaemon + 1));
        }
        std::cout << "Starting scheduler daemon (checking every " << intervalMins << " minutes). Press Ctrl+C to stop.\n";
        logLine("daemon", "started interval=" + std::to_string(intervalMins), "running");
        while (true) {
            runScheduler();
            Sleep(intervalMins * 60 * 1000);
        }
    }
    return 0;
}
