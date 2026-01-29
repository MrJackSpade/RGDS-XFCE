/*
 * wine-launcher - Unified Wine launcher for ARM devices
 *
 * Selects between Box86+Wine, Box64+Wine64, and Hangover Wine
 * based on executable type or user selection.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>

// PE Header structures
#pragma pack(push, 1)

struct DOSHeader {
    uint16_t e_magic;      // "MZ"
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;     // Offset to PE header
};

struct PESignature {
    uint32_t signature;    // "PE\0\0"
};

struct COFFHeader {
    uint16_t machine;
    uint16_t numberOfSections;
    uint32_t timeDateStamp;
    uint32_t pointerToSymbolTable;
    uint32_t numberOfSymbols;
    uint16_t sizeOfOptionalHeader;
    uint16_t characteristics;
};

struct OptionalHeader32 {
    uint16_t magic;        // 0x10b for PE32
    uint8_t  majorLinkerVersion;
    uint8_t  minorLinkerVersion;
    uint32_t sizeOfCode;
    uint32_t sizeOfInitializedData;
    uint32_t sizeOfUninitializedData;
    uint32_t addressOfEntryPoint;
    uint32_t baseOfCode;
    uint32_t baseOfData;
    uint32_t imageBase;
    uint32_t sectionAlignment;
    uint32_t fileAlignment;
    uint16_t majorOperatingSystemVersion;
    uint16_t minorOperatingSystemVersion;
    uint16_t majorImageVersion;
    uint16_t minorImageVersion;
    uint16_t majorSubsystemVersion;
    uint16_t minorSubsystemVersion;
    uint32_t win32VersionValue;
    uint32_t sizeOfImage;
    uint32_t sizeOfHeaders;
    uint32_t checkSum;
    uint16_t subsystem;
    uint16_t dllCharacteristics;
    uint32_t sizeOfStackReserve;
    uint32_t sizeOfStackCommit;
    uint32_t sizeOfHeapReserve;
    uint32_t sizeOfHeapCommit;
    uint32_t loaderFlags;
    uint32_t numberOfRvaAndSizes;
};

struct OptionalHeader64 {
    uint16_t magic;        // 0x20b for PE32+
    uint8_t  majorLinkerVersion;
    uint8_t  minorLinkerVersion;
    uint32_t sizeOfCode;
    uint32_t sizeOfInitializedData;
    uint32_t sizeOfUninitializedData;
    uint32_t addressOfEntryPoint;
    uint32_t baseOfCode;
    uint64_t imageBase;
    uint32_t sectionAlignment;
    uint32_t fileAlignment;
    uint16_t majorOperatingSystemVersion;
    uint16_t minorOperatingSystemVersion;
    uint16_t majorImageVersion;
    uint16_t minorImageVersion;
    uint16_t majorSubsystemVersion;
    uint16_t minorSubsystemVersion;
    uint32_t win32VersionValue;
    uint32_t sizeOfImage;
    uint32_t sizeOfHeaders;
    uint32_t checkSum;
    uint16_t subsystem;
    uint16_t dllCharacteristics;
    uint64_t sizeOfStackReserve;
    uint64_t sizeOfStackCommit;
    uint64_t sizeOfHeapReserve;
    uint64_t sizeOfHeapCommit;
    uint32_t loaderFlags;
    uint32_t numberOfRvaAndSizes;
};

struct DataDirectory {
    uint32_t virtualAddress;
    uint32_t size;
};

// LNK file structures
struct LnkHeader {
    uint32_t headerSize;
    uint8_t  clsid[16];
    uint32_t linkFlags;
    uint32_t fileAttributes;
    uint64_t creationTime;
    uint64_t accessTime;
    uint64_t writeTime;
    uint32_t fileSize;
    uint32_t iconIndex;
    uint32_t showCommand;
    uint16_t hotKey;
    uint16_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct LinkInfoHeader {
    uint32_t linkInfoSize;
    uint32_t linkInfoHeaderSize;
    uint32_t linkInfoFlags;
    uint32_t volumeIDOffset;
    uint32_t localBasePathOffset;
    uint32_t commonNetworkRelativeLinkOffset;
    uint32_t commonPathSuffixOffset;
};

#pragma pack(pop)

// LNK LinkFlags
constexpr uint32_t HasLinkTargetIDList = 0x00000001;
constexpr uint32_t HasLinkInfo         = 0x00000002;

// Machine types
constexpr uint16_t IMAGE_FILE_MACHINE_I386  = 0x014c;
constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;

// Optional header magic
constexpr uint16_t PE32_MAGIC  = 0x10b;
constexpr uint16_t PE32P_MAGIC = 0x20b;

// Data directory indices
constexpr int IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR = 14;

enum class WineBackend {
    Auto,
    Box86,
    Box64,
    Hangover,
    HangoverFex
};

enum class ExeArch {
    Unknown,
    X86,
    X86_64
};

struct ExeInfo {
    ExeArch arch = ExeArch::Unknown;
    bool isDotNet = false;
    bool valid = false;
};

// Wine binary paths
const char* BOX86_WINE = "/home/trixie/.local/share/wine/bin/wine";
const char* BOX64_WINE = "/home/trixie/.local/share/wine64/bin/wine64";
const char* HANGOVER_WINE = "/usr/bin/wine";
const char* BOX86_BIN = "/usr/local/bin/box86";
const char* BOX64_BIN = "/usr/local/bin/box64";

// Forward declarations
WineBackend autoDetectBackend(const ExeInfo& info);
const char* backendName(WineBackend b);

// Parse Windows .lnk shortcut to extract target path
std::string parseLnkTarget(const char* lnkPath) {
    FILE* f = fopen(lnkPath, "rb");
    if (!f) return "";

    // Read and verify header
    LnkHeader header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return "";
    }

    // Check magic: header size should be 0x4C
    if (header.headerSize != 0x4C) {
        fclose(f);
        return "";
    }

    // Check CLSID for shell link: 00021401-0000-0000-C000-000000000046
    static const uint8_t shellLinkCLSID[] = {
        0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46
    };
    if (memcmp(header.clsid, shellLinkCLSID, 16) != 0) {
        fclose(f);
        return "";
    }

    long currentPos = sizeof(LnkHeader);

    // Skip LinkTargetIDList if present
    if (header.linkFlags & HasLinkTargetIDList) {
        fseek(f, currentPos, SEEK_SET);
        uint16_t idListSize;
        if (fread(&idListSize, sizeof(idListSize), 1, f) != 1) {
            fclose(f);
            return "";
        }
        currentPos += 2 + idListSize;
    }

    // Read LinkInfo if present
    if (header.linkFlags & HasLinkInfo) {
        fseek(f, currentPos, SEEK_SET);

        LinkInfoHeader linkInfo;
        if (fread(&linkInfo, sizeof(linkInfo), 1, f) != 1) {
            fclose(f);
            return "";
        }

        // Check if local base path is present (flag bit 0)
        if ((linkInfo.linkInfoFlags & 0x01) && linkInfo.localBasePathOffset > 0) {
            // Seek to local base path
            fseek(f, currentPos + linkInfo.localBasePathOffset, SEEK_SET);

            // Read null-terminated string
            std::string path;
            char c;
            while (fread(&c, 1, 1, f) == 1 && c != '\0') {
                path += c;
            }

            fclose(f);
            return path;
        }
    }

    fclose(f);
    return "";
}

// Convert Windows path to Unix path (basic conversion)
std::string winPathToUnix(const std::string& winPath, const std::string& lnkDir) {
    if (winPath.empty()) return "";

    std::string path = winPath;

    // Replace backslashes with forward slashes
    for (char& c : path) {
        if (c == '\\') c = '/';
    }

    // Handle drive letters - try common wine prefix locations
    if (path.length() >= 2 && path[1] == ':') {
        char drive = path[0];
        if (drive >= 'A' && drive <= 'Z') drive = drive - 'A' + 'a';

        std::string remainder = path.substr(2);

        // Try various wine prefix locations
        std::string home = getenv("HOME") ? getenv("HOME") : "";
        std::vector<std::string> prefixes = {
            home + "/.wine32/dosdevices/" + drive + ":",
            home + "/.wine64/dosdevices/" + drive + ":",
            home + "/.wine-hangover/dosdevices/" + drive + ":",
            home + "/.wine/dosdevices/" + drive + ":",
            std::string(getenv("WINEPREFIX") ? getenv("WINEPREFIX") : "") + "/dosdevices/" + drive + ":",
        };

        for (const auto& prefix : prefixes) {
            if (prefix.empty() || prefix[0] != '/') continue;
            std::string fullPath = prefix + remainder;
            if (access(fullPath.c_str(), F_OK) == 0) {
                return fullPath;
            }
        }

        // Default to ~/.wine32 (primary prefix with games)
        return home + "/.wine32/dosdevices/" + drive + ":" + remainder;
    }

    // Relative path - relative to lnk file location
    if (!lnkDir.empty() && path[0] != '/') {
        return lnkDir + "/" + path;
    }

    return path;
}

ExeInfo analyzeExecutable(const char* path) {
    ExeInfo info;

    FILE* f = fopen(path, "rb");
    if (!f) {
        return info;
    }

    // Read DOS header
    DOSHeader dos;
    if (fread(&dos, sizeof(dos), 1, f) != 1) {
        fclose(f);
        return info;
    }

    // Check MZ signature
    if (dos.e_magic != 0x5A4D) { // "MZ"
        fclose(f);
        return info;
    }

    // Seek to PE header
    if (fseek(f, dos.e_lfanew, SEEK_SET) != 0) {
        fclose(f);
        return info;
    }

    // Read PE signature
    PESignature pe;
    if (fread(&pe, sizeof(pe), 1, f) != 1) {
        fclose(f);
        return info;
    }

    // Check PE signature
    if (pe.signature != 0x00004550) { // "PE\0\0"
        fclose(f);
        return info;
    }

    // Read COFF header
    COFFHeader coff;
    if (fread(&coff, sizeof(coff), 1, f) != 1) {
        fclose(f);
        return info;
    }

    // Determine architecture from machine type
    if (coff.machine == IMAGE_FILE_MACHINE_I386) {
        info.arch = ExeArch::X86;
    } else if (coff.machine == IMAGE_FILE_MACHINE_AMD64) {
        info.arch = ExeArch::X86_64;
    } else {
        fclose(f);
        return info;
    }

    info.valid = true;

    // Check for .NET by reading optional header and COM descriptor
    if (coff.sizeOfOptionalHeader == 0) {
        fclose(f);
        return info;
    }

    // Read optional header magic to determine PE32 vs PE32+
    uint16_t magic;
    if (fread(&magic, sizeof(magic), 1, f) != 1) {
        fclose(f);
        return info;
    }

    // Seek back
    fseek(f, -2, SEEK_CUR);

    uint32_t numberOfRvaAndSizes = 0;
    long dataDirectoryOffset = 0;

    if (magic == PE32_MAGIC) {
        OptionalHeader32 opt;
        if (fread(&opt, sizeof(opt), 1, f) != 1) {
            fclose(f);
            return info;
        }
        numberOfRvaAndSizes = opt.numberOfRvaAndSizes;
        dataDirectoryOffset = ftell(f);
    } else if (magic == PE32P_MAGIC) {
        OptionalHeader64 opt;
        if (fread(&opt, sizeof(opt), 1, f) != 1) {
            fclose(f);
            return info;
        }
        numberOfRvaAndSizes = opt.numberOfRvaAndSizes;
        dataDirectoryOffset = ftell(f);
    } else {
        fclose(f);
        return info;
    }

    // Check if COM descriptor directory exists (index 14)
    if (numberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR) {
        // Seek to COM descriptor directory entry
        fseek(f, dataDirectoryOffset + IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR * sizeof(DataDirectory), SEEK_SET);

        DataDirectory comDir;
        if (fread(&comDir, sizeof(comDir), 1, f) == 1) {
            // If virtualAddress and size are non-zero, it's a .NET assembly
            if (comDir.virtualAddress != 0 && comDir.size != 0) {
                info.isDotNet = true;
            }
        }
    }

    fclose(f);
    return info;
}

// Get directory containing a file
std::string getDirname(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

// Check if path looks like a Windows path
bool isWindowsPath(const char* path) {
    if (!path || !path[0]) return false;
    // Check for drive letter (C: or C:\)
    if (path[1] == ':') return true;
    // Check for backslashes
    return strchr(path, '\\') != nullptr;
}

// Find the target executable, resolving .lnk files and Windows paths
std::string findTargetExe(const char* path) {
    if (!path) return "";

    std::string unixPath;

    // Convert Windows path to Unix path first
    if (isWindowsPath(path)) {
        unixPath = winPathToUnix(path, "");
    } else {
        unixPath = path;
    }

    size_t len = unixPath.length();
    if (len < 4) return unixPath;

    const char* ext = unixPath.c_str() + len - 4;

    // If it's a .lnk file, parse it
    if (strcasecmp(ext, ".lnk") == 0) {
        std::string winTarget = parseLnkTarget(unixPath.c_str());
        if (!winTarget.empty()) {
            std::string target = winPathToUnix(winTarget, getDirname(unixPath));
            return target;
        }
    }

    return unixPath;
}

bool hasDisplay() {
    const char* display = getenv("DISPLAY");
    const char* wayland = getenv("WAYLAND_DISPLAY");
    return (display && display[0] != '\0') || (wayland && wayland[0] != '\0');
}

bool commandExists(const char* cmd) {
    std::string check = "command -v ";
    check += cmd;
    check += " >/dev/null 2>&1";
    return system(check.c_str()) == 0;
}

WineBackend showGuiDialog(WineBackend recommended) {
    // Default to Hangover if somehow Auto gets here
    if (recommended == WineBackend::Auto) recommended = WineBackend::Hangover;

    char cmd[1024];

    if (commandExists("zenity")) {
        snprintf(cmd, sizeof(cmd),
            "zenity --list --radiolist --hide-header "
            "--title='' --text=' ' "
            "--column='' --column='' "
            "%s 'Box86' "
            "%s 'Box64' "
            "%s 'Hangover' "
            "%s 'Hangover-FEX' "
            "--width=250 --height=350 2>/dev/null",
            recommended == WineBackend::Box86 ? "TRUE" : "FALSE",
            recommended == WineBackend::Box64 ? "TRUE" : "FALSE",
            recommended == WineBackend::Hangover ? "TRUE" : "FALSE",
            recommended == WineBackend::HangoverFex ? "TRUE" : "FALSE");
    } else if (commandExists("yad")) {
        snprintf(cmd, sizeof(cmd),
            "yad --list --radiolist --no-headers "
            "--title='' --text=' ' "
            "--column=':CHK' --column='' "
            "%s 'Box86' "
            "%s 'Box64' "
            "%s 'Hangover' "
            "%s 'Hangover-FEX' "
            "--width=200 --height=250 --print-column=2 2>/dev/null",
            recommended == WineBackend::Box86 ? "TRUE" : "FALSE",
            recommended == WineBackend::Box64 ? "TRUE" : "FALSE",
            recommended == WineBackend::Hangover ? "TRUE" : "FALSE",
            recommended == WineBackend::HangoverFex ? "TRUE" : "FALSE");
    } else if (commandExists("kdialog")) {
        snprintf(cmd, sizeof(cmd),
            "kdialog --menu '' "
            "'Box86' 'Box86' "
            "'Box64' 'Box64' "
            "'Hangover' 'Hangover' "
            "'Hangover-FEX' 'Hangover-FEX' 2>/dev/null");
    } else {
        return recommended;
    }

    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        return recommended;
    }

    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }

    int status = pclose(pipe);
    if (status != 0) {
        // User cancelled
        exit(0);
    }

    // Trim whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == '|')) {
        result.pop_back();
    }

    if (result.find("Box86") != std::string::npos) return WineBackend::Box86;
    if (result.find("Box64") != std::string::npos) return WineBackend::Box64;
    // Check Hangover-FEX before Hangover since it contains "Hangover"
    if (result.find("Hangover-FEX") != std::string::npos) return WineBackend::HangoverFex;
    if (result.find("Hangover") != std::string::npos) return WineBackend::Hangover;
    return WineBackend::Auto;
}

WineBackend showTerminalDialog(WineBackend recommended) {
    // Default to Hangover if somehow Auto gets here
    if (recommended == WineBackend::Auto) recommended = WineBackend::Hangover;

    char cmd[512];

    if (commandExists("whiptail")) {
        snprintf(cmd, sizeof(cmd),
            "whiptail --menu 'Wine Backend' 12 35 4 "
            "'Box86' '' "
            "'Box64' '' "
            "'Hangover' '' "
            "'Hangover-FEX' '' "
            "--default-item '%s' "
            "3>&1 1>&2 2>&3",
            backendName(recommended));
    } else if (commandExists("dialog")) {
        snprintf(cmd, sizeof(cmd),
            "dialog --menu 'Wine Backend' 12 35 4 "
            "'Box86' '' "
            "'Box64' '' "
            "'Hangover' '' "
            "'Hangover-FEX' '' "
            "--default-item '%s' "
            "3>&1 1>&2 2>&3",
            backendName(recommended));
    } else {
        // Fallback to simple text menu
        int rec = (recommended == WineBackend::Box86) ? 1 :
                  (recommended == WineBackend::Box64) ? 2 :
                  (recommended == WineBackend::HangoverFex) ? 4 : 3;
        fprintf(stderr, "1) Box86  2) Box64  3) Hangover  4) Hangover-FEX  [%d]: ", rec);

        char choice[16];
        if (fgets(choice, sizeof(choice), stdin)) {
            if (choice[0] == '\n') return recommended;
            switch (choice[0]) {
                case '1': return WineBackend::Box86;
                case '2': return WineBackend::Box64;
                case '3': return WineBackend::Hangover;
                case '4': return WineBackend::HangoverFex;
                default: return recommended;
            }
        }
        return recommended;
    }

    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        return recommended;
    }

    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }

    int status = pclose(pipe);
    if (status != 0) {
        // User cancelled
        exit(0);
    }

    // Trim whitespace and quotes
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == '\'')) {
        result.pop_back();
    }
    if (!result.empty() && result.front() == '\'') {
        result.erase(0, 1);
    }

    if (result == "Box86") return WineBackend::Box86;
    if (result == "Box64") return WineBackend::Box64;
    if (result == "Hangover-FEX") return WineBackend::HangoverFex;
    if (result == "Hangover") return WineBackend::Hangover;
    return recommended;
}

WineBackend selectBackend(WineBackend recommended) {
    if (hasDisplay()) {
        return showGuiDialog(recommended);
    } else {
        return showTerminalDialog(recommended);
    }
}

WineBackend autoDetectBackend(const ExeInfo& info) {
    if (!info.valid) {
        // Can't determine, default to hangover
        return WineBackend::Hangover;
    }

    if (info.isDotNet) {
        // .NET executable - use box86/box64 based on architecture
        if (info.arch == ExeArch::X86) {
            return WineBackend::Box86;
        } else if (info.arch == ExeArch::X86_64) {
            return WineBackend::Box64;
        }
    }

    // Non-.NET - use Hangover
    return WineBackend::Hangover;
}

std::string getHomeDir() {
    const char* home = getenv("HOME");
    return home ? home : "/tmp";
}

void setBox86Env() {
    std::string prefix = getHomeDir() + "/.wine32";
    setenv("WINEPREFIX", prefix.c_str(), 1);
    setenv("WINEARCH", "win32", 1);
    setenv("BOX86_DYNAREC", "1", 1);
    setenv("BOX86_DYNAREC_BIGBLOCK", "3", 1);
    setenv("BOX86_DYNAREC_STRONGMEM", "2", 1);
    setenv("BOX86_GL", "1", 1);
    setenv("BOX86_LOG", "1", 1);
    setenv("WINEESYNC", "0", 1);
    setenv("WINEFSYNC", "0", 1);
}

void setBox64Env() {
    std::string prefix = getHomeDir() + "/.wine64";
    setenv("WINEPREFIX", prefix.c_str(), 1);
    setenv("WINEARCH", "win64", 1);
    setenv("BOX64_DYNAREC", "1", 1);
    setenv("BOX64_DYNAREC_BIGBLOCK", "3", 1);
    setenv("BOX64_DYNAREC_STRONGMEM", "2", 1);
    setenv("BOX64_LOG", "1", 1);
    setenv("WINEESYNC", "0", 1);
    setenv("WINEFSYNC", "0", 1);
}

void setHangoverEnv() {
    std::string prefix = getHomeDir() + "/.wine-hangover";
    setenv("WINEPREFIX", prefix.c_str(), 1);
}

void setHangoverFexEnv() {
    std::string prefix = getHomeDir() + "/.wine-hangover";
    setenv("WINEPREFIX", prefix.c_str(), 1);
    setenv("HODLL", "libwow64fex.dll", 1);
}

// Detect which backend's wineserver is running (if any)
WineBackend detectRunningBackend() {
    FILE* pipe = popen("pgrep -a wineserver 2>/dev/null", "r");
    if (!pipe) return WineBackend::Auto;  // Auto means none detected

    char buffer[512];
    WineBackend running = WineBackend::Auto;

    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (strstr(buffer, ".local/share/wine/bin/wineserver")) {
            running = WineBackend::Box86;
            break;
        } else if (strstr(buffer, ".local/share/wine64/bin/wineserver")) {
            running = WineBackend::Box64;
            break;
        } else if (strstr(buffer, "/usr/bin/wineserver") ||
                   strstr(buffer, "/usr/lib/wine")) {
            running = WineBackend::Hangover;
            break;
        }
    }
    pclose(pipe);
    return running;
}

void killWineservers() {
    // Kill wineserver processes to avoid conflicts between backends
    system("pkill -9 -f wineserver 2>/dev/null");
    usleep(200000); // 200ms
}

void killIfDifferentBackend(WineBackend target) {
    WineBackend running = detectRunningBackend();
    if (running != WineBackend::Auto && running != target) {
        fprintf(stderr, "[wine-launcher] Killing %s wineserver to switch to %s\n",
                backendName(running), backendName(target));
        killWineservers();
    }
}

// Find all descendant PIDs of a process by reading /proc directly
std::vector<pid_t> getDescendants(pid_t parent) {
    std::vector<pid_t> descendants;

    DIR* proc = opendir("/proc");
    if (!proc) return descendants;

    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        // Skip non-numeric entries
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;

        pid_t pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        // Read the stat file to get ppid
        char statPath[64];
        snprintf(statPath, sizeof(statPath), "/proc/%d/stat", pid);
        FILE* f = fopen(statPath, "r");
        if (!f) continue;

        // Format: pid (comm) state ppid ...
        int rpid;
        char comm[256];
        char state;
        int ppid;
        if (fscanf(f, "%d %255s %c %d", &rpid, comm, &state, &ppid) == 4) {
            if (ppid == parent) {
                descendants.push_back(pid);
            }
        }
        fclose(f);
    }
    closedir(proc);

    // Recursively get grandchildren
    size_t count = descendants.size();
    for (size_t i = 0; i < count; i++) {
        auto grandchildren = getDescendants(descendants[i]);
        descendants.insert(descendants.end(), grandchildren.begin(), grandchildren.end());
    }

    return descendants;
}

void execWine(WineBackend backend, int argc, char** argv) {
    // Only kill wineserver if switching to a different backend
    killIfDifferentBackend(backend);

    std::vector<const char*> args;

    switch (backend) {
        case WineBackend::Box86:
            setBox86Env();
            args.push_back(BOX86_BIN);
            args.push_back(BOX86_WINE);
            break;

        case WineBackend::Box64:
            setBox64Env();
            args.push_back(BOX64_BIN);
            args.push_back(BOX64_WINE);
            break;

        case WineBackend::HangoverFex:
            setHangoverFexEnv();
            args.push_back(HANGOVER_WINE);
            break;

        case WineBackend::Hangover:
        default:
            setHangoverEnv();
            args.push_back(HANGOVER_WINE);
            break;
    }

    // Add remaining arguments
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }
    args.push_back(nullptr);

    // Print the full command with environment for debugging
    fprintf(stderr, "[wine-launcher] To reproduce, run:\n");
    fprintf(stderr, "WINEPREFIX=\"%s\"", getenv("WINEPREFIX") ? getenv("WINEPREFIX") : "");
    if (getenv("WINEARCH")) fprintf(stderr, " WINEARCH=\"%s\"", getenv("WINEARCH"));
    if (getenv("WINEDEBUG")) fprintf(stderr, " WINEDEBUG=\"%s\"", getenv("WINEDEBUG"));
    if (backend == WineBackend::Box86 || backend == WineBackend::Box64) {
        if (getenv("BOX86_LOG")) fprintf(stderr, " BOX86_LOG=\"%s\"", getenv("BOX86_LOG"));
        if (getenv("BOX64_LOG")) fprintf(stderr, " BOX64_LOG=\"%s\"", getenv("BOX64_LOG"));
    }
    if (backend == WineBackend::HangoverFex) {
        if (getenv("HODLL")) fprintf(stderr, " HODLL=\"%s\"", getenv("HODLL"));
    }
    for (size_t i = 0; args[i] != nullptr; i++) {
        fprintf(stderr, " ");
        // Quote args with spaces or backslashes
        if (strchr(args[i], ' ') || strchr(args[i], '\\')) {
            fprintf(stderr, "\"%s\"", args[i]);
        } else {
            fprintf(stderr, "%s", args[i]);
        }
    }
    fprintf(stderr, "\n");

    // Just exec wine directly
    execv(args[0], const_cast<char* const*>(args.data()));
    perror("execv failed");
    exit(1);
}

const char* backendName(WineBackend b) {
    switch (b) {
        case WineBackend::Box86: return "Box86";
        case WineBackend::Box64: return "Box64";
        case WineBackend::Hangover: return "Hangover";
        case WineBackend::HangoverFex: return "Hangover-FEX";
        default: return "Auto";
    }
}

void printUsage(const char* prog) {
    fprintf(stderr, "Usage: %s [options] [program.exe] [arguments...]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --backend=<type>   Select backend: auto, box86, box64, hangover, hangover-fex\n");
    fprintf(stderr, "  --info <file>      Show executable info and exit\n");
    fprintf(stderr, "  --help             Show this help\n");
    fprintf(stderr, "\nIf no --backend is specified, a selection dialog is shown.\n");
}

int main(int argc, char** argv) {
    WineBackend selectedBackend = WineBackend::Auto;
    bool showDialog = true;
    int firstArg = 1;

    // Parse our options (before the exe path)
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--backend=", 10) == 0) {
            const char* val = argv[i] + 10;
            if (strcasecmp(val, "box86") == 0) {
                selectedBackend = WineBackend::Box86;
            } else if (strcasecmp(val, "box64") == 0) {
                selectedBackend = WineBackend::Box64;
            } else if (strcasecmp(val, "hangover-fex") == 0) {
                selectedBackend = WineBackend::HangoverFex;
            } else if (strcasecmp(val, "hangover") == 0) {
                selectedBackend = WineBackend::Hangover;
            } else if (strcasecmp(val, "auto") == 0) {
                selectedBackend = WineBackend::Auto;
            } else {
                fprintf(stderr, "Unknown backend: %s\n", val);
                return 1;
            }
            showDialog = false;
            // Shift arguments
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            i--;
        } else if (strcmp(argv[i], "--info") == 0 && i + 1 < argc) {
            std::string target = findTargetExe(argv[i + 1]);
            ExeInfo info = analyzeExecutable(target.c_str());
            printf("File: %s\n", argv[i + 1]);
            if (target != argv[i + 1]) {
                printf("Target: %s\n", target.c_str());
            }
            if (info.valid) {
                printf("Architecture: %s\n", info.arch == ExeArch::X86 ? "x86 (32-bit)" :
                                             info.arch == ExeArch::X86_64 ? "x86_64 (64-bit)" : "Unknown");
                printf(".NET Assembly: %s\n", info.isDotNet ? "Yes" : "No");
                printf("Recommended backend: %s\n", backendName(autoDetectBackend(info)));
            } else {
                printf("Could not analyze executable\n");
                printf("Recommended backend: Hangover (default)\n");
            }
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            // First non-option argument
            firstArg = i;
            break;
        }
    }

    // Find the exe/lnk file in arguments
    const char* filePath = nullptr;
    for (int i = firstArg; i < argc; i++) {
        const char* arg = argv[i];
        size_t len = strlen(arg);
        if (len > 4) {
            const char* ext = arg + len - 4;
            if (strcasecmp(ext, ".exe") == 0 || strcasecmp(ext, ".msi") == 0 || strcasecmp(ext, ".lnk") == 0) {
                filePath = arg;
                break;
            }
        }
    }

    // Resolve target (handles .lnk files)
    std::string targetExe = findTargetExe(filePath);

    // Analyze the executable to determine recommendation
    WineBackend recommended = WineBackend::Hangover;
    if (!targetExe.empty()) {
        ExeInfo info = analyzeExecutable(targetExe.c_str());
        recommended = autoDetectBackend(info);
    }

    // Show dialog if no backend specified
    if (showDialog) {
        selectedBackend = selectBackend(recommended);
    }

    // If Auto was selected, use the recommendation
    if (selectedBackend == WineBackend::Auto) {
        selectedBackend = recommended;
        fprintf(stderr, "[wine-launcher] Using: %s\n", backendName(selectedBackend));
    }

    // Execute wine with selected backend
    execWine(selectedBackend, argc, argv);

    return 1; // Should not reach here
}
