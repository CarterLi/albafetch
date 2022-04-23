#ifndef CONFIG_H
#define CONFIG_H

#include "logos.h"
#include <stdlib.h>

// CONFIGURATION OPTIONS:
#define SPACING "    "                          // defines the distance between the logo and the infos
#define SEPARATOR "\e[0m------------------"     // defines what is used as separator between sections
#define DASH_COLOR "\e[39m"                     // \e[39m or DEFAULT_COLOR to make it white or like the text
#define DASH ":"

// options
#define PRINT_CPU_FREQ false // doesn't work for AMD Ryzen processors

// Labels:
#define HOSTNAME_LABEL "Hostname"
#define USER_LABEL "User"
#define UPTIME_LABEL "Uptime"
#define OS_LABEL "OS"
#define KERNEL_LABEL "Kernel"
#define DESKTOP_LABEL "Desktop"
#define SHELL_LABEL "Shell"
#define TERM_LABEL "Terminal"
#define PACKAGES_LABEL "Packages"
#define HOST_LABEL "Host"
#define BIOS_LABEL "BIOS"
#define CPU_LABEL "CPU"
#define GPU_LABEL "GPU"
#define MEM_LABEL "Memory"
#define PUB_IP_LABEL "Publ. IP"
#define PRIV_IP_LABEL "Priv. IP"

// was just too lazy to do it right, so it's hard coded. fuck you <3
#define GPU "Intel UHD Graphics 620"    // what will be printed as gpu

// add more logos in src/logos.h if you want, I couldn't care less
// would reccomend also changing the --help display output to show the correct default

// examples:
// static char **logo = debian;     // set debian as default logo
// static char *color = "\e[31m";   // set red as default color
// static char *bold = "\e[1m";     // use bold. leave "" to turn the bold off 

static char **logo = debian;        // use NULL for OS default 
static char *color = NULL;          // use NULL for logo default
static char *bold = "\e[1m";        // leave "" to turn off

#endif
