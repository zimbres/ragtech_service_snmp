#include "config.h"
#include "log.h"
#include "ups.h"
#include "ups_snmp.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#endif

static config_t g_config;
#ifdef _WIN32
static platform_flag_t g_shutdown=0;
static HANDLE g_shutdown_complete;
static char g_config_path[CONFIG_FILE_MAX_PATH]="ragtech.conf";
static BOOL WINAPI console_handler(DWORD event) {
    if(event!=CTRL_C_EVENT && event!=CTRL_BREAK_EVENT && event!=CTRL_CLOSE_EVENT &&
       event!=CTRL_LOGOFF_EVENT && event!=CTRL_SHUTDOWN_EVENT)return FALSE;
    platform_flag_set(&g_shutdown,true);
    /* Closing the console terminates the process after this callback returns. */
    if(event!=CTRL_C_EVENT && event!=CTRL_BREAK_EVENT && g_shutdown_complete)
        WaitForSingleObject(g_shutdown_complete,4500);
    return TRUE;
}
static bool shutdown_requested(void) { return platform_flag_get(&g_shutdown); }
#else
static volatile sig_atomic_t g_shutdown=0;
static char g_config_path[CONFIG_FILE_MAX_PATH]="/etc/ragtech/ragtech.conf";
static void signal_handler(int sig) { if(sig==SIGTERM || sig==SIGINT)g_shutdown=1; }
static bool shutdown_requested(void) { return g_shutdown!=0; }
static void daemonize(void) {
    pid_t pid=fork();
    if(pid<0) { LOG_ERROR("fork failed: %s",strerror(errno));exit(EXIT_FAILURE); }
    if(pid>0)exit(EXIT_SUCCESS);
    if(setsid()<0) { LOG_ERROR("setsid failed: %s",strerror(errno));exit(EXIT_FAILURE); }
    pid=fork();
    if(pid<0) { LOG_ERROR("fork failed: %s",strerror(errno));exit(EXIT_FAILURE); }
    if(pid>0)exit(EXIT_SUCCESS);
    if(chdir("/")<0)exit(EXIT_FAILURE);
    close(STDIN_FILENO);close(STDOUT_FILENO);close(STDERR_FILENO);
    if(open("/dev/null",O_RDONLY)<0 || open("/dev/null",O_WRONLY)<0 || open("/dev/null",O_WRONLY)<0)
        exit(EXIT_FAILURE);
}
#endif
static void print_usage(const char *name) {
    printf("Usage: %s [options]\n",name);
#ifdef _WIN32
    printf("  -c, --config FILE     Config file (default: ./ragtech.conf)\n");
#else
    printf("  -c, --config FILE     Config file (default: /etc/ragtech/ragtech.conf)\n");
    printf("  -d, --daemon          Run as daemon\n");
    printf("  -p, --pid-file FILE   PID file path\n");
#endif
    printf("  -f, --foreground      Run in foreground\n");
    printf("  -l, --log-level LEVEL debug, info, warn, error\n");
    printf("  -t, --test-config     Validate configuration and exit\n");
    printf("  -h, --help            Show help\n");
    printf("  -v, --version         Show version\n");
}
/* Small portable option parser; no getopt library required on Windows. */
static bool matches(const char *arg,const char *short_name,const char *long_name) {
    size_t length=strlen(long_name);
    return (arg[0]=='-' && arg[1]==short_name[1]) || strcmp(arg,long_name)==0 ||
           (strncmp(arg,long_name,length)==0 && arg[length]=='=');
}
static const char *option_value(int *index,int argc,char **argv) {
    if(argv[*index][0]=='-' && argv[*index][1]!='-' && argv[*index][2])return argv[*index]+2;
    const char *eq=strchr(argv[*index],'=');
    if(eq)return eq[1] ? eq+1 : NULL;
    if(*index+1>=argc)return NULL;
    return argv[++*index];
}
static bool copy_option(char *out,size_t capacity,const char *value) {
    if(!value || !*value || strlen(value)>=capacity)return false;
    strcpy(out,value);return true;
}
int main(int argc,char **argv) {
    bool test_config=false;
    int daemon_override=-1;
    const char *pid_file=NULL,*log_level=NULL;
    for(int i=1;i<argc;i++) {
        const char *arg=argv[i];
        if(matches(arg,"-c","--config")) {
            if(!copy_option(g_config_path,sizeof(g_config_path),option_value(&i,argc,argv)))goto bad_options;
        } else if(matches(arg,"-p","--pid-file")) {
            pid_file=option_value(&i,argc,argv);
            if(!pid_file || !*pid_file || strlen(pid_file)>=CONFIG_MAX_STRING)goto bad_options;
        } else if(matches(arg,"-l","--log-level")) {
            log_level=option_value(&i,argc,argv);if(!log_level)goto bad_options;
        } else if(strcmp(arg,"-d")==0 || strcmp(arg,"--daemon")==0)daemon_override=1;
        else if(strcmp(arg,"-f")==0 || strcmp(arg,"--foreground")==0)daemon_override=0;
        else if(strcmp(arg,"-t")==0 || strcmp(arg,"--test-config")==0)test_config=true;
        else if(strcmp(arg,"-h")==0 || strcmp(arg,"--help")==0) { print_usage(argv[0]);return EXIT_SUCCESS; }
        else if(strcmp(arg,"-v")==0 || strcmp(arg,"--version")==0) { puts("ragtech_service v3.0.0");return EXIT_SUCCESS; }
        else if(arg[0]=='-' && arg[1]!='-' && arg[1]) {
            /* Preserve getopt's short-option bundles, such as -ft or -fl debug. */
            for(size_t position=1;arg[position];position++) {
                char option=arg[position];
                if(option=='f')daemon_override=0;
                else if(option=='d')daemon_override=1;
                else if(option=='t')test_config=true;
                else if(option=='h') { print_usage(argv[0]);return EXIT_SUCCESS; }
                else if(option=='v') { puts("ragtech_service v3.0.0");return EXIT_SUCCESS; }
                else if(option=='c' || option=='p' || option=='l') {
                    const char *value=arg[position+1] ? arg+position+1 : (++i<argc ? argv[i] : NULL);
                    if(!value || !*value)goto bad_options;
                    if(option=='c') {
                        if(!copy_option(g_config_path,sizeof(g_config_path),value))goto bad_options;
                    } else if(option=='p') {
                        if(strlen(value)>=CONFIG_MAX_STRING)goto bad_options;
                        pid_file=value;
                    } else log_level=value;
                    break;
                } else goto bad_options;
            }
        } else goto bad_options;
    }
    if(!config_load(g_config_path,&g_config)) {
        fprintf(stderr,"Failed to load configuration from %s\n",g_config_path);return EXIT_FAILURE;
    }
    if(daemon_override>=0)g_config.daemonize=daemon_override!=0;
    if(pid_file)strcpy(g_config.pid_file,pid_file);
    if(log_level) {
        if(strcmp(log_level,"debug")==0)g_config.log_level=LOG_DEBUG;
        else if(strcmp(log_level,"info")==0)g_config.log_level=LOG_INFO;
        else if(strcmp(log_level,"warn")==0)g_config.log_level=LOG_WARN;
        else if(strcmp(log_level,"error")==0)g_config.log_level=LOG_ERROR;
        else goto bad_options;
    }
#ifdef _WIN32
    if(g_config.daemonize || g_config.pid_file[0]) {
        fputs("Windows runs in foreground: set daemonize=false and leave pid_file empty.\n",stderr);
        return EXIT_FAILURE;
    }
#endif
    log_init((log_level_t)g_config.log_level,g_config.daemonize,"ragtech_service");
    LOG_INFO_S("=== ragtech_service v3.0.0 ===");
    LOG_INFO("Config file: %s",g_config_path);config_print(&g_config);
    if(test_config) { LOG_INFO_S("Configuration test passed");log_cleanup();return EXIT_SUCCESS; }
#ifndef _WIN32
    if(g_config.daemonize) { daemonize();log_init((log_level_t)g_config.log_level,true,"ragtech_service"); }
    if(g_config.pid_file[0]) {
        FILE *file=fopen(g_config.pid_file,"w");
        if(file) { fprintf(file,"%ld\n",(long)getpid());fclose(file); }
        else LOG_WARN("Cannot write PID file: %s",strerror(errno));
    }
    signal(SIGTERM,signal_handler);signal(SIGINT,signal_handler);
    signal(SIGHUP,SIG_IGN);signal(SIGPIPE,SIG_IGN);
#else
    g_shutdown_complete=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!g_shutdown_complete || !SetConsoleCtrlHandler(console_handler,TRUE)) {
        LOG_ERROR("Cannot initialize console shutdown handler: %s",platform_error());log_cleanup();return EXIT_FAILURE;
    }
#endif
    if(ups_init(&g_config)<0) { LOG_ERROR_S("Failed to initialize UPS");goto cleanup; }
    if(shutdown_requested())goto cleanup;
    if(snmp_init(&g_config)<0) { LOG_ERROR_S("Failed to initialize SNMP");goto cleanup; }
    if(ups_start()<0) { LOG_ERROR_S("Failed to start UPS polling");goto cleanup; }
    LOG_INFO_S("UPS SNMP agent started successfully");
    while(!shutdown_requested())snmp_poll(1000);
    LOG_INFO_S("Shutting down...");
cleanup:
    snmp_cleanup();ups_cleanup();
#ifndef _WIN32
    if(g_config.pid_file[0])remove(g_config.pid_file);
#else
    /* Kept until process exit so a concurrent console-close callback can wait. */
    if(g_shutdown_complete)SetEvent(g_shutdown_complete);
#endif
    log_cleanup();return shutdown_requested() ? EXIT_SUCCESS : EXIT_FAILURE;
bad_options:
    fputs("Invalid or incomplete command-line option.\n",stderr);print_usage(argv[0]);return EXIT_FAILURE;
}
