#define _DEFAULT_SOURCE

#include <string.h>
#include <stdio.h>
#include <libgen.h>     // basename
#include <ctype.h>      // toupper
#include <sys/wait.h>

#ifdef __APPLE__
#include "bsdwrap.h"
#include "macos_infos.h"
#else
#include <sys/sysinfo.h>// uptime, memory
#ifndef __ANDROID__
#include <pci/pci.h>    // gpu
#endif // __linux__
#endif // __APPLE__

#include <sys/utsname.h> // uname
#include <pwd.h>        // username
#include <ifaddrs.h>    // local ip
#include <arpa/inet.h>  // local ip
#include <time.h>       // date
#include <curl/curl.h>  // public ip
#include <dirent.h>     // packages

#include "info.h"
#include "queue.h"
#include "utils.h"

// print the current user
int user(char *dest) {
    struct passwd *pw;

    unsigned uid = geteuid();
    if((int)uid == -1) {
        // couldn't get UID
        return 1;
    }

    pw = getpwuid(uid);

    strncpy(dest, pw->pw_name, 255);

    return 0;
}

// print the machine hostname
int hostname(char *dest) {
    char hostname[HOST_NAME_MAX + 1];
    gethostname(hostname, HOST_NAME_MAX + 1);

    char *ptr = strstr(hostname, ".local");
    if(ptr)
        *ptr = 0;

    strncpy(dest, hostname, 256);

    return 0;
}

// print the current uptime
int uptime(char *dest) {
    #ifdef __APPLE__
        struct timeval boottime;
        int error;
        long uptime;
        error = sysctl_wrap(&boottime, sizeof(boottime), CTL_KERN, KERN_BOOTTIME);

        if(error < 0)
            return 1;

        time_t boot_seconds = boottime.tv_sec;
        time_t current_seconds = time(NULL);

        uptime = (long)difftime(current_seconds, boot_seconds);
    #else
        struct sysinfo info;
        if(sysinfo(&info))
            return 1;

        const long uptime = info.uptime;
    #endif // __APPLE__

    long days = uptime/86400;
    char hours = uptime/3600 - days*24;
    char mins = uptime/60 - days*1440 - hours*60;

    char result[256] = "";
    char str[24] = "";

    if(days) {
        snprintf(str, 24, "%ldd%c", days, hours || mins ? ' ' : 0);     // print the number of days passed if more than 0
        strcat(result, str);
    }
    if(hours) {
        snprintf(str, 24, "%dh%c", hours, mins ? ' ' : 0);      // print the number of days passed if more than 0
        strcat(result, str);
    }
    if(mins) {
        snprintf(str, 24, "%dm", mins);       // print the number of minutes passed if more than 0
        strcat(result, str);
    }
    else if(uptime < 60) {
        snprintf(str, 24, "%lds", uptime);       // print the number of seconds passed if less than 60
        strcat(result, str);
    }

    strncpy(dest, result, 256);

    return 0;
}

// print the operating system name and architecture (uname -m)
int os(char *dest) {
    struct utsname name;
    uname(&name);

    #ifdef __APPLE__
        snprintf(dest, 256, "macOS %s", os_arch ? name.machine : "");
    #else
    #ifdef __ANDROID__
        int pipes[2];
        char version[16];

        if(pipe(pipes))
            return 1;
        if(!fork()) {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);

            execlp("getprop", "getprop", "ro.build.version.release", NULL); 
        }

        wait(0);
        close(pipes[1]);
        version[read(pipes[0], version, 16) - 1] = 0;
        close(pipes[0]);

        snprintf(dest, 256, "Android %s%s%s", version, version[0] ? " " : "", os_arch ? name.machine : "");
    #else
        FILE *fp = fopen("/etc/os-release", "r");
        if(!fp) {
            fp = fopen("/usr/lib/os-release", "r");
            if(!fp)
                return 1;
        }

        char buf[64];
        char *os_name = buf;
        char *end;

        read_after_sequence(fp, "PRETTY_NAME", buf, 64);
        fclose(fp);

        if(!buf[0])
            return 1;

        if(os_name[0] == '"' || os_name[0] == '\'')
            ++os_name;
        
        if(!(end = strchr(os_name, '\n')))
            return 1;
        *end = 0;

        if((end = strchr(os_name, '"')))
            *end = 0;
        else if((end = strchr(os_name, '\'')))
            *end = 0;

        snprintf(dest, 256, "%s %s", os_name, os_arch ? name.machine : "");
    #endif // __ANDROID__
    #endif // __APPLE__

    return 0;
}

// print the running kernel version (uname -r)
int kernel(char *dest) {
    struct utsname name;
    uname(&name);
    char *ptr = name.release, *type = NULL;
    
    if(kernel_type) {
        while((ptr = strchr(ptr, '-')))
            type = ++ptr;
    }

    if(kernel_short) {
        if((ptr = strchr(name.release, '-')))
            *ptr = 0;
    }

    if(kernel_type && type)
        snprintf(dest, 256, "%s (%s)", name.release, type);
    else
        strncpy(dest, name.release, 256);

    return 0;
}

// get the current desktop environment
int desktop(char *dest) {
    #ifdef __APPLE__
        strcpy(dest, "Aqua");
    #else
        char *desktop = getenv("SWAYSOCK") ? "Sway" :
                            (desktop = getenv("XDG_CURRENT_DESKTOP")) ? desktop :
                            (desktop = getenv("DESKTOP_SESSION")) ? desktop :
                            getenv("KDE_SESSION_VERSION") ? "KDE" :
                            getenv("GNOME_DESKTOP_SESSION_ID") ? "GNOME" :
                            getenv("MATE_DESKTOP_SESSION_ID") ? "MATE" :
                            getenv("TDE_FULL_SESSION") ? "Trinity" :
                            // !strcmp("linux", getenv("TERM")) ? "none" :      // running in tty
                            NULL;
        if(!desktop)
            return 1;

        strcpy(dest, desktop);

        if(de_type) {
            if(getenv("WAYLAND_DISPLAY"))
                strncat(dest, " (Wayland)", 255-strlen(dest));
            else if((desktop = getenv("XDG_SESSION_TYPE"))) {
                if(!desktop[0])
                    return 0;
                desktop[0] = toupper(desktop[0]);
                
                char buf[32];
                snprintf(buf, 32, " (%s) ", desktop);
                strncat(dest, buf, 255-strlen(dest));
            }
        }
    #endif

    return 0;
}

// get the current GTK Theme
int gtk_theme(char *dest){ 
    int pipes[2];

    char *theme = getenv("GTK_THEME");

    // try using GTK_THEME (faster)
    if(theme) {
        strncpy(dest, theme, 256);

        return 0;
    }

    // try using gsettings (fallback)
    if(!access("/bin/gsettings", F_OK)){
        if(pipe(pipes))
            return 1;

        if(!fork()) {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);

            execlp("gsettings", "gsettings" , "get", "org.gnome.desktop.interface", "gtk-theme", NULL); 
        }

        wait(0);
        close(pipes[1]);
        dest[read(pipes[0], dest, 256) - 1] = 0;
        close(pipes[0]);

        // cleanup
        if(dest[0] == '\'') {
            strcpy(dest, dest+1);

            char *ptr = strchr(dest, '\'');
            if(ptr)
                *ptr = 0;
        }

        return 0;
    }

    return 1;
}

// get the parent process name (usually the shell)
int shell(char *dest) {
    #ifdef __linux__
        char path[32];

        sprintf(path, "/proc/%d/cmdline", getppid());

        FILE *fp = fopen(path, "r");
        if(fp) {
            char shell[256];
            shell[fread(shell, 1, 255, fp)] = 0;
            fclose(fp);

            if(shell[0] == '-') { // cmdline is "-bash" when login shell
                strncpy(dest, shell_path ? shell+1 : basename(shell+1), 256);
                return 0;
            }

            strncpy(dest, shell_path ? shell : basename(shell), 256);
            return 0;
        }
    #endif

    char *shell = getenv("SHELL");
    if(shell && shell[0]) {
        strncpy(dest, shell_path ? shell : basename(shell), 256);
        return 0;
    }

    return 1;
}

// get the current login shell
int login_shell(char *dest) {
    char *buf = getenv("SHELL");

    if(buf && buf[0]) {
        strncpy(dest, shell_path ? buf : basename(buf), 256);
        return 0;
    }

    return 1;
}

// get the current terminal
int term(char *dest) {
    // TODO: print terminal version (using env variables, parsing --version outputs, ...)
    char *terminal = NULL;

    char *terminals[][2] = {
     // {"ENVIRONMENT_VARIABLE", "terminal"},
        {"ALACRITTY_SOCKET", "alacritty"},
        {"KITTY_PID", "kitty"},
        {"VSCODE_INJECTION", "vscode"},
        {"TERMUX_VERSION", "termux"},
        {"KONSOLE_VERSION", "konsole"},
        {"GNOME_TERMINAL_SCREEN", "gnome-terminal"},
        {"WT_SESSION", "windows terminal"},
    };

    for(size_t i = 0; i < sizeof(terminals)/sizeof(terminals[0]); ++i)
        if(getenv(terminals[i][0]))
            terminal = terminals[i][1];

    if(!terminal) {
        terminal = getenv("TERM");
        if(!terminal)
            return 1;
        
        if(!strcmp(terminal, "xterm-kitty"))
            terminal = "kitty";
    }

    if(term_ssh && getenv("SSH_CONNECTION"))
        snprintf(dest, 256, "%s (SSH)", terminal);
    else
        strncpy(dest, terminal, 256);

    return 0;
}

// get the number of installed packages
int packages(char *dest) {
    dest[0] = 0;
    char buf[256] = "", str[128] = "", path[256] = "";
    DIR *dir;
    struct dirent *entry;
    unsigned count = 0;
    int pipes[2];
    bool done = false;

    #ifndef __APPLE__   // package managers that won't run on macOS
        FILE *fp;

        path[0] = 0;
        if(getenv("PREFIX"))
            strncpy(path, getenv("PREFIX"), 255);
        strncat(path, "/var/lib/pacman/local", 256-strlen(path));
        if(pkg_pacman && (dir = opendir(path))) {
            while((entry = readdir(dir)) != NULL)
                if(entry->d_type == DT_DIR && strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
                    ++count;

            if(count) {
                snprintf(dest, 256 - strlen(buf), "%s%u%s", done ? ", " : "", count, pkg_mgr ? " (pacman)" : "");
                done = true;
            }
            closedir(dir);
        }

        path[0] = 0;
        if(getenv("PREFIX"))
            strncpy(path, getenv("PREFIX"), 255);
        strncat(path, "/var/lib/dpkg/status", 256-strlen(path));
        if(pkg_dpkg && (fp = fopen(path, "r"))) {   // alternatively, I could use "dpkg-query -f L -W" and strlen
            fseek(fp, 0, SEEK_END);
            size_t len = ftell(fp);
            rewind(fp);

            char *dpkg_list = malloc(len);
            dpkg_list[fread(dpkg_list, 1, len, fp) - 1] = 0;

            fclose(fp);

            count = 0;
            // this will be wrong if some package does not have "\nInstalled-Size: "
            // or if some package (for some reason) has it in the package description
            while((dpkg_list = strstr(dpkg_list, "\nInstalled-Size: "))) {
                ++count;
                ++dpkg_list;
            }
            free(dpkg_list);

            if(count) {
                snprintf(buf, 256, "%u%s", count, pkg_mgr ? " (dpkg)" : "");
                done = true;
                strncat(dest, buf, 256 - strlen(dest));
            }
        }

        path[0] = 0;
        if(getenv("PREFIX"))
            strncpy(path, getenv("PREFIX"), 255);
        strncat(path, "/var/lib/rpm/rpmdb.sqlite", 256-strlen(path));
        if(pkg_rpm && !access(path, F_OK)) {
            if(pipe(pipes))
                return 1;

            if(!fork()) {
                close(pipes[0]);
                dup2(pipes[1], STDOUT_FILENO);

                execlp("sh", "sh", "-c", "sqlite3 /var/lib/rpm/rpmdb.sqlite 'SELECT count(*) FROM Packages' 2>/dev/null", NULL);
            }
            wait(0);
            close(pipes[1]);
            str[read(pipes[0], str, 16) - 1] = 0;
            close(pipes[0]);

            if(str[0] != '0' && str[0]) {
                snprintf(buf, 256 - strlen(buf), "%s%s%s", done ? ", " : "", str, pkg_mgr ? " (rpm)" : "");
                done = true;
                strncat(dest, buf, 256 - strlen(dest));
            }
        }

        path[0] = 0;
        if(getenv("PREFIX"))
            strncpy(path, getenv("PREFIX"), 255);
        strncat(path, "/var/lib/flatpak/runtime", 256-strlen(path));
        if(pkg_flatpak && (dir = opendir(path))) {
            count = 0;
            while((entry = readdir(dir)) != NULL)
                if(entry->d_type == DT_DIR && strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
                    ++count;

            if(count) {
                snprintf(buf, 256 - strlen(buf), "%s%u%s", done ? ", " : "", count, pkg_mgr ? " (flatpak)" : "");
                done = true;
                strncat(dest, buf, 256 - strlen(dest));
            }
            closedir(dir);
        }

        path[0] = 0;
        if(getenv("PREFIX"))
            strncpy(path, getenv("PREFIX"), 255);
        strncat(path, "/bin/snap", 256-strlen(path));
        if(pkg_snap && !access(path, F_OK)) {
            if(pipe(pipes))
                return 1;

            if(!fork()) {
                close(pipes[0]);
                dup2(pipes[1], STDOUT_FILENO);

                execlp("sh", "sh", "-c", "snap list 2>/dev/null | wc -l", NULL); 
            }

            wait(0);
            close(pipes[1]);
            str[read(pipes[0], str, 16) - 1] = 0;
            close(pipes[0]);

            if(str[0] != '0' && str[0]) {
                snprintf(buf, 256 - strlen(buf), "%s%d%s", done ? ", " : "", atoi(str)-1, pkg_mgr ? " (snap)" : "");
                done = true;
                strncat(dest, buf, 256 - strlen(dest));
            }
        }
    #endif
    if(pkg_brew && (!access("/usr/local/bin/brew", F_OK) || !access("/opt/homebrew/bin/brew", F_OK) || !access("/bin/brew", F_OK))) {
        if(pipe(pipes))
            return 1;

        if(!fork()) {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);
            execlp("brew", "brew", "--cellar", NULL); 
        }

        close(pipes[1]);

        wait(0);

        str[read(pipes[0], str, 128) - 1] = 0;
        close(pipes[0]);

        if(str[0]) {
            if((dir = opendir(str))) {
                count = 0;

                while((entry = readdir(dir)) != NULL)
                    if(entry->d_type == DT_DIR && strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
                        ++count;

                if(count) {
                    snprintf(buf, 256, "%s%u%s", done ? ", " : "", count, pkg_mgr ? " (brew)" : "");
                    done = true;
                    strncat(dest, buf, 256 - strlen(dest));
                }

                closedir(dir);
            }
        }
    }


    path[0] = 0;
    if(getenv("PREFIX"))
        strncpy(path, getenv("PREFIX"), 255);
    strncat(path, "/bin/pip", 256-strlen(path));
    if(pkg_pip && !access(path, F_OK)) {
        if(pipe(pipes))
            return 1;

        if(!fork()) {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);

            execlp("sh", "sh", "-c", "pip list 2>/dev/null | wc -l", NULL); 
        }

        wait(0);
        close(pipes[1]);
        str[read(pipes[0], str, 16) - 1] = 0;
        close(pipes[0]);

        if(str[0] != '0' && str[0]) {
            snprintf(buf, 256 - strlen(buf), "%s%d%s", done ? ", " : "", atoi(str)-2, pkg_mgr ? " (pip)" : "");
            done = true;
            strncat(dest, buf, 256 - strlen(dest));
        }
    }

    return !done;
}

// get the machine name and eventually model version
int host(char *dest) {
    #ifdef __APPLE__
        size_t BUF_SIZE = 256;
        sysctlbyname("hw.model", dest, &BUF_SIZE, NULL, 0);
    #else
    #ifdef __ANDROID__
        int pipes[2];
        char brand[64], model[64];

        if(pipe(pipes))
            return 1;
        if(!fork()) {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);

            execlp("getprop", "getprop", "ro.product.brand", NULL); 
        }

        wait(0);
        close(pipes[1]);
        brand[read(pipes[0], brand, 64) - 1] = 0;
        close(pipes[0]);

        if(pipe(pipes))
            return 1;
        if(!fork()) {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);

            execlp("getprop", "getprop", "ro.product.model", NULL); 
        }

        wait(0);
        close(pipes[1]);
        model[read(pipes[0], model, 64) - 1] = 0;
        close(pipes[0]);

        if(!(brand[0] || model[0]))
            return 1;

        snprintf(dest, 256, "%s%s%s", brand, brand[0] ? " ": "", model);
    #else
        char *name = NULL, *version = NULL;
        FILE *fp = NULL;
        size_t len = 0;

        if((fp = fopen("/sys/devices/virtual/dmi/id/product_name", "r"))) {
            fseek(fp, 0, SEEK_END);
            len = ftell(fp);
            rewind(fp);

            name = malloc(len);
            name[fread(name, 1, len, fp) - 1] = 0;
            
            fclose(fp);
        }
       
        if((fp = fopen("/sys/devices/virtual/dmi/id/product_version", "r"))) {
            fseek(fp, 0, SEEK_END);
            len = ftell(fp);
            rewind(fp);

            version = malloc(len);
            version[fread(version, 1, len, fp) - 1] = 0;

            fclose(fp);
        }

        // filtering out some shitty defaults because the file can't just be empty
        const char *errors[] = {"System Product Name", "System Version", "To Be Filled By O.E.M.", ""};
        bool name_defined = true;
        bool version_defined = true;

        for(unsigned long i = 0; i < sizeof(errors)/sizeof(errors[0]); ++i) {
            if(name)
                if(!strcmp(name, errors[i]))
                    name_defined = false;
            if(version)
                if(!strcmp(version, errors[i]))
                    version_defined = false;
        }

        if(name && version && name_defined && version_defined)
            snprintf(dest, 256, "%s %s", name, version);
        else if(name && name_defined)
            strncpy(dest, name, 256);
        else if(version && version_defined)
            strncpy(dest, version, 256);
        else
            return 1;

        free(name);
        free(version);
    #endif // __ANDROID__
    #endif // __APPLE__

    return 0;
}

// get the current BIOS vendor and version (Linux only!)
int bios(char *dest) {
    #ifdef __APPLE__
        (void)dest; // avoid unused parameter warning - lmao
        return 1;
    #else
    char *vendor = NULL, *version = NULL;
    FILE *fp = NULL;
    size_t len = 0;

    if((fp = fopen("/sys/devices/virtual/dmi/id/bios_vendor", "r"))) {
        fseek(fp, 0, SEEK_END);
        len = ftell(fp);
        rewind(fp);

        vendor = malloc(len);
        vendor[fread(vendor, 1, len, fp) - 1] = 0;

        fclose(fp);
    }

    if((fp = fopen("/sys/devices/virtual/dmi/id/bios_version", "r"))) {
        fseek(fp, 0, SEEK_END);
        len = ftell(fp);
        rewind(fp);

        version = malloc(len);
        version[fread(version, 1, len, fp) - 1] = 0;

        fclose(fp);
    }

    if(vendor && version)
        snprintf(dest, 256, "%s %s", vendor, version);
    else if(vendor)
        strncpy(dest, vendor, 256);
    else if(version)
        strncpy(dest, version, 256);
    else
        return 1;

    free(vendor);
    free(version);
    
    return 0;
    #endif
}

// get the cpu name and frequency
int cpu(char *dest) {
    char *cpu_info;
    char *end;
    int count = 0;
    char freq[24] = "";

    #ifdef __APPLE__
        size_t BUF_SIZE = 256;
        char buf[BUF_SIZE];
        buf[0] = 0;
        sysctlbyname("machdep.cpu.brand_string", buf, &BUF_SIZE, NULL, 0);

        if(!buf[0])
            return 1;

        if(!(cpu_freq)) {
            if((end = strstr(buf, " @")))
                *end = 0;
            else if((end = strchr(buf, '@')))
                *end = 0;
        }

        cpu_info = buf;
    #else
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if(!fp)
        return 1;

    char *buf = malloc(0x10001);
    buf[fread(buf, 1, 0x10000, fp)] = 0;
    fclose(fp);

    cpu_info = buf;
    if(cpu_count) {
        end = cpu_info;
        while((end = strstr(end, "processor"))) {
            ++count;
            ++end;
        }
    }

    cpu_info = strstr(cpu_info, "model name");
    if(!cpu_info) {
        return 1;
        free(cpu_info);
    }

    cpu_info += 13;

    // I have no clue why valgrind complains about this part
    end = strstr(cpu_info, " @");
    if(end)
        *end = 0;
    else {
        end = strchr(cpu_info, '\n');
        if(!end) {
            return 1;
            free(cpu_info);
        }
            
        *end = 0;
    }

    /* I might eventually add an option to get the "default" clock speed
     * by parsing one or more of the following files:
     * - /sys/devices/system/cpu/cpu0/cpufreq/cpupower_max_freq
     * - /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
     * - /sys/devices/system/cpu/cpu0/cpufreq/bios_limit
     * - /sys/devices/system/cpu/cpu0/cpufreq/base_frequency
     */
    // Printing the clock frequency the first thread is currently running at
    ++end;
    char *frequency = strstr(end, "cpu MHz");
    if(frequency && cpu_freq) {
        frequency = strchr(frequency, ':');
        if(frequency) {
            frequency += 2;

            end = strchr(frequency, '\n');
            if(end) {
                *end = 0;

                snprintf(freq, 24, " @ %g GHz", (float)(atoi(frequency)/100) / 10);
            }
        }
    }
    #endif

    // cleaning the string from various garbage
    if((end = strstr(cpu_info, "(R)")))
        memmove(end, end+3, strlen(end+3)+1);
    if((end = strstr(cpu_info, "(TM)")))
        memmove(end, end+4, strlen(end+4)+1);
    if((end = strstr(cpu_info, " CPU")))
        memmove(end, end+4, strlen(end+4)+1);
    if((end = strstr(cpu_info, "th Gen ")))
        memmove(end-2, end+7, strlen(end+7)+1);
    if((end = strstr(cpu_info, " with Radeon Graphics")))
        *end = 0;
    if((end = strstr(cpu_info, "-Core Processor"))) {
        end -= 4;
        end = strchr(end, ' ');
        *end = 0;
    }

    if(!(cpu_brand)) {
        if((end = strstr(cpu_info, "Intel Core ")))
            memmove(end, end+11, strlen(end+1));
        else if((end = strstr(cpu_info, "Apple ")))
            memmove(end, end+6, strlen(end+6)+1);
        else if((end = strstr(cpu_info, "AMD ")))
            memmove(end, end+4, strlen(end+1));
    }

    strncpy(dest, cpu_info, 256);
    #ifdef __linux__
        free(buf);
    #endif

    if(freq[0])
        strncat(dest, freq, 255-strlen(dest));

    if(count && cpu_count) {
        char core_count[16];
        snprintf(core_count, 16, " (%d) ", count);
        strncat(dest, core_count, 255-strlen(dest));
    }
    // final cleanup ("Intel Core i5         650" lol)
    while((end = strstr(dest, "  ")))
        memmove(end, end+1, strlen(end));

    return 0;
}

// get the first gpu
int gpu(char *dest) {
    char *gpus[] = {NULL, NULL, NULL};
    char *end;

    #ifdef __APPLE__
        struct utsname name;
        uname(&name);

        if(!strcmp(name.machine, "x86_64"))
            gpus[0] = get_gpu_string();  // only works on x64
        if(!gpus[0] || strcmp(name.machine, "x86_64")) {     // fallback
            char buf[1024];
            int pipes[2];
            if(pipe(pipes))
                return 1;

            if(!fork()) {
                dup2(pipes[1], STDOUT_FILENO);
                close(pipes[0]);
                close(pipes[1]);
                execlp("/usr/sbin/system_profiler", "system_profiler", "SPDisplaysDataType", NULL);
            }
            close(pipes[1]);
            wait(0);
            size_t bytes = read(pipes[0], buf, 1024);
            close(pipes[0]);

            if(bytes < 1)
                return 1;

            gpus[0] = strstr(buf, "Chipset Model: ");
            if(!gpus[0])
                return 1;
            gpus[0] += 15;
            char *end = strchr(gpus[0], '\n');
            if(!end)
                return 1;
            *end = 0;
        }
    #else
    # ifdef __ANDROID__
        return 1;
    # else
        // based on https://github.com/pciutils/pciutils/blob/master/example.c

        char device_class[256], namebuf[768];
        struct pci_dev *dev;
        struct pci_access *pacc = pci_alloc();		// get the pci_access structure;

        pci_init(pacc);		// initialize the PCI library
        pci_scan_bus(pacc);		// we want to get the list of devices

        int i = 0;

        for(dev=pacc->devices; dev; dev=dev->next)	{ // iterates over all devices
            pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS);	// fill in header info

            pci_lookup_name(pacc, device_class, 256, PCI_LOOKUP_CLASS, dev->device_class);
            if(!strcmp(device_class, "VGA compatible controller") || !strcmp(device_class, "3D controller")) {
                // look up the full name of the device
                if(config.gpu_index == 0) {
                    gpus[i] = pci_lookup_name(pacc, namebuf+i*256, 256, PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);
                    
                    if(i < 2)
                        ++i;
                    else
                        break;
                }
                else {
                    if(i == config.gpu_index-1) {
                        gpus[0] = pci_lookup_name(pacc, namebuf, 256, PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);
                        break;
                    }

                    if(i < 2)
                        ++i;
                    else
                        break;
                }
            }
        }



        pci_cleanup(pacc);  // close everything
        // fallback (will only get 1 gpu)

        char gpu[256];

        if(!gpus[0]) {
            if(config.gpu_index != 0)   // lol why would you choose a non-existing GPU
                return 1;

            int pipes[2];

            if(pipe(pipes))
                return 1;

            if(!fork()) {
                close(pipes[0]);
                dup2(pipes[1], STDOUT_FILENO);
                execlp("lspci", "lspci", "-mm", NULL);
            }
            
            close(pipes[1]);
            char *lspci = malloc(0x2000);
            
            wait(NULL);
            lspci[read(pipes[0], lspci, 0x2000)] = 0;
            close(pipes[0]);

            gpus[0] = strstr(lspci, "3D");
            if(!gpus[0]) {
                gpus[0] = strstr(lspci, "VGA");
                if(!gpus[0]) {
                    free(lspci);
                    return 1;
                }
            }

            for(int i = 0; i < 4; ++i) {
                gpus[0] = strchr(gpus[0], '"');
                if(!gpus[0]) {
                    free(lspci);
                    return 1;
                }
                ++gpus[0];

                /* class" "manufacturer" "name"
                 *  "manufacturer" "name"
                 * manufacturer" "name"
                 *  "name"
                 * name"
                 */
            }

            char *end = strchr(gpus[0], '"');   // name
            if(!end) {
                free(lspci);
                return 1;
            }
            *end = 0;
            
            strncpy(gpu, gpus[0], 255);
            free(lspci);
            gpus[0] = gpu;
        }
    # endif // __ANDROID__
    #endif // __APPLE__

    if(!gpus[0])
        return 1;

    // this next part is just random cleanup
    // also, I'm using end as a random char* - BaD pRaCtIcE aNd CoNfUsInG - lol stfu

    dest[0] = 0;    //  yk it's decent a yk it works
    for(unsigned i = 0; i < sizeof(gpus)/sizeof(gpus[0]) && gpus[i%3]; ++i) {
        if(!(gpu_brand)) {
            if((end = strstr(gpus[i], "Intel ")))
                gpus[i] += 6;
            else if((end = strstr(gpus[i], "AMD ")))
                gpus[i] += 4;
            else if((end = strstr(gpus[i], "Apple ")))
                gpus[i] += 6;
        }

        if((end = strchr(gpus[i], '['))) {   // sometimes the gpu is "Architecture [GPU Name]"
            char *ptr = strchr(end, ']');
            if(ptr) {
                gpus[i] = end+1;
                *ptr = 0;
            }
        if((end = strstr(gpus[i], " Integrated Graphics Controller")))
            *end = 0;
        if((end = strstr(gpus[i], " Rev. ")))
            *end = 0;
        }

        // (finally) writing the GPUs into dest
        if(i > 0)
            strncat(dest, ", ", 256-strlen(dest));
        strncat(dest, gpus[i], 256-strlen(dest));
    }

    return 0;
}

// get used and total memory
int memory(char *dest) {
    #ifdef __APPLE__ 
        bytes_t usedram = used_mem_size();
        bytes_t totalram = system_mem_size();

        if(!usedram || !totalram) { 
            return 1;
        }

        snprintf(dest, 256, "%llu MiB / %llu MiB", usedram/1048576, totalram/1048576);
    #else
        struct sysinfo info;
        if(sysinfo(&info))
            return 1;

        unsigned long totalram = info.totalram / 1024;
        unsigned long freeram = info.freeram / 1024;
        // unsigned long sharedram = info.sharedram / 1024;

        FILE *fp = fopen("/proc/meminfo", "r");

        if(!fp)
            return 1;

        char buf[256];
        char *cachedram = buf;

        read_after_sequence(fp, "Cached:", buf, 256);
        fclose(fp);

        if(!(buf[0]))
            return 1;
        cachedram += 2;

        char *end = strstr(cachedram, " kB");
        
        if(!end)
            return 1;
        
        *end = 0;

        unsigned long usedram = totalram - freeram - atol(cachedram);
        // usedram -= sharedram;

        snprintf(dest, 256, "%lu MiB / %lu MiB", usedram/1024, totalram/1024);
    #endif

    if(mem_perc) {
        const size_t len = 256-strlen(dest);
        char perc[len];
        
        snprintf(perc, len, " (%lu%%)", (unsigned long)((usedram * 100) / totalram));
        strcat(dest, perc);
    }

    return 0;
}

// get the current public ip
int public_ip(char *dest) {
    CURL *curl_handle = curl_easy_init();
    CURLcode res;

    // fallback
    char ip_str[32] = "";
    int pipes[2];

    struct MemoryStruct chunk;
    chunk.memory = malloc(4096);
    chunk.size = 0;

    if(!curl_handle) {
        curl_easy_cleanup(curl_handle);
        free(chunk.memory);

        goto fallback;
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, "ident.me");
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    res = curl_easy_perform(curl_handle);

    if(res != CURLE_OK) {
        curl_easy_cleanup(curl_handle);
        free(chunk.memory);

        goto fallback;
    }

    strncpy(dest, chunk.memory, 256);

    curl_easy_cleanup(curl_handle);
    free(chunk.memory);

    return 0;

    fallback:
        if(pipe(pipes))
            return 1;
        
        if(!fork()) {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);

            execlp("curl", "curl", "-s", "ident.me", NULL); 
        }
        wait(0);
        close(pipes[1]);
        ip_str[read(pipes[0], ip_str, 32) - 1] = 0;
        close(pipes[0]);

    if(!ip_str[0]) {
        return 1;
    }

    strcpy(dest, ip_str);
    return 0;
}

// get all local ips
int local_ip(char *dest) {
    struct ifaddrs *addrs=NULL;
    bool done = false;
    int buf_size = 256;
    
    getifaddrs(&addrs);

    while(addrs) {
        // checking if the ip is valid
       if(addrs->ifa_addr && addrs->ifa_addr->sa_family == AF_INET) {
            // filtering out docker or localhost ips
            if((strcmp(addrs->ifa_name, "lo") || loc_localhost) && (strcmp(addrs->ifa_name, "docker0") || loc_docker)) {
                struct sockaddr_in *pAddr = (struct sockaddr_in *)addrs->ifa_addr;
                
                // saving it to the list of interfaces
                snprintf(dest, buf_size, "%s%s (%s)", done ? ", " : "", inet_ntoa(pAddr->sin_addr), addrs->ifa_name);
                dest += strlen(dest);
                buf_size -= strlen(dest);
                done = true;
            }
        }

        addrs = addrs->ifa_next;
    }

    freeifaddrs(addrs);
    
    return !done;
}

// get the current working directory
int pwd(char *dest) {
    if(!(pwd_path)) {
        char buf[256];

        if(!getcwd(buf, 256))
            return 1;

        strncpy(dest, buf, 256);
    }

    if(!getcwd(dest, 256))
        return 1;

    return 0;
}

// get the current date and time
int date(char *dest) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    snprintf(dest, 256, config.date_format, tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return 0;
}

// show the terminal color configuration
int colors(char *dest) {
    memset(dest, 0, 256);
    for(int i = 0; i < 8; ++i) {
        sprintf(dest+(5+config.col_block_len)*i, "\033[4%dm", i);
        for(int i = 0; i < config.col_block_len; ++i)
            strcat(dest, " ");
    }

    strcat(dest, "\033[0m");

    return 0;
}
// show the terminal color configuration (light version)
int light_colors(char *dest) {
    if(config.col_block_len > 16)
        return 1;

    memset(dest, 0, 256);
    for(int i = 0; i < 8; ++i) {
        sprintf(dest+(6+config.col_block_len)*i, "\033[10%dm", i);
        for(int i = 0; i < config.col_block_len; ++i)
            strcat(dest, " ");
    }

    strcat(dest, "\033[0m");

    return 0;
}
