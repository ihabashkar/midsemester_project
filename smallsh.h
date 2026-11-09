#ifndef SMALLSH_H
#define SMALLSH_H

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define EOL         1
#define ARG         2
#define AMPERSAND   3
#define SEMICOLON   4
#define MAXARG      512
#define MAXBUF      512

#define FOREGROUND  0
#define BACKGROUND  1

#define MAXJOBS     64

// Job states
#define RUNNING     1
#define STOPPED     2

struct job {
    pid_t pid;
    int jid;
    int state;
    char cmd[MAXBUF];
};

// Function prototypes
int userin(char *p);
int gettok(char **outptr);
int inarg(char c);
int procline(void);
int runcommand(char **cline, int where);
void init_shell(void);
void add_job(pid_t pid, int state, char *cmd);
void remove_job(pid_t pid);
struct job *get_job_by_jid(int jid);
struct job *get_job_by_pid(pid_t pid);
void builtin_jobs(void);
void builtin_bg(char **args);
void builtin_fg(char **args);
void builtin_kill(char **args);

#endif
