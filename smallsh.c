#include "smallsh.h"

// Buffers used by userin() and gettok() to read input lines and extract tokens
static char inpbuf[MAXBUF], tokbuf[2 * MAXBUF];
static char *ptr = inpbuf, *tok = tokbuf;

// Job table: Tracks active background and stopped jobs in the shell
static struct job job_list[MAXJOBS];
static int next_jid = 1; // Auto-incrementing Job ID (JID)

// Shell process group and terminal control variables for job control
static pid_t shell_pgid;
static int shell_terminal;
static int shell_is_interactive;

/**
 * init_shell - Initializes job control for the shell.
 * It ensures the shell runs in its own process group and takes control 
 * of the terminal so it can properly manage foreground/background child processes.
 */
void init_shell(void) {
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {
        // Loop until the shell is in the foreground of the terminal
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        // Ignore interactive signals in the shell itself so Ctrl+C / Ctrl+Z 
        // terminate or stop child processes instead of crashing the shell.
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);

        // Put the shell into its own independent process group
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("Couldn't put shell in its own process group");
            exit(1);
        }

        // Grab control of the terminal
        tcsetpgrp(shell_terminal, shell_pgid);
    }
}

/**
 * add_job - Registers a new background or stopped job into the job tracking table.
 */
void add_job(pid_t pid, int state, char *cmd) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == 0) { // Find an empty slot in the job table
            job_list[i].pid = pid;
            job_list[i].jid = next_jid++;
            job_list[i].state = state;
            snprintf(job_list[i].cmd, MAXBUF, "%s", cmd);
            return;
        }
    }
}

/**
 * remove_job - Clears a terminated job from the job tracking table using its PID.
 */
void remove_job(pid_t pid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            job_list[i].pid = 0;
            job_list[i].jid = 0;
            job_list[i].state = 0;
            memset(job_list[i].cmd, 0, MAXBUF);
            return;
        }
    }
}

// Helper: Find a job struct by its Job ID (JID, e.g., %1)
struct job *get_job_by_jid(int jid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (job_list[i].jid == jid) return &job_list[i];
    }
    return NULL;
}

// Helper: Find a job struct by its Process ID (PID)
struct job *get_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) return &job_list[i];
    }
    return NULL;
}

/**
 * userin - Prints the command prompt and reads a line of input character-by-character.
 */
int userin(char *p) {
    int c, count;
    ptr = inpbuf;
    tok = tokbuf;

    printf("%s", p);
    fflush(stdout);
    count = 0;

    while (1) {
        if ((c = getchar()) == EOF)
            return (EOF);
        if (count < MAXBUF)
            inpbuf[count++] = c;
        if (c == '\n' && count < MAXBUF) {
            inpbuf[count] = '\0';
            return count;
        }
        if (c == '\n') {
            printf("smallsh: input line too long\n");
            count = 0;
            printf("%s", p);
            fflush(stdout);
        }
    }
}

/**
 * inarg - Checks whether a character is part of a normal command argument 
 * or a special shell delimiter (spaces, tabs, ampersands, semicolons).
 */
int inarg(char c) {
    static char special[] = {' ', '\t', '&', ';', '\n', '\0'};
    char *wrk;
    for (wrk = special; *wrk; wrk++) {
        if (c == *wrk) return (0);
    }
    return (1);
}

/**
 * gettok - Extracts individual lexical tokens (arguments, operators) from the input buffer.
 */
int gettok(char **outptr) {
    int type;
    while (*ptr == ' ' || *ptr == '\t')
        ptr++;

    *outptr = tok;

    if (*ptr == '\n') {
        *tok++ = '\0';
        ptr++;
        return (EOL);
    }

    if (*ptr == ';') {
        *tok++ = '\0';
        ptr++;
        return (SEMICOLON);
    }

    if (*ptr == '&') {
        *tok++ = '\0';
        ptr++;
        return (AMPERSAND);
    }

    type = ARG;
    while (inarg(*ptr)) {
        *tok++ = *ptr++;
    }
    *tok++ = '\0';
    return (type);
}

// ==========================================
// BUILT-IN COMMAND IMPLEMENTATIONS
// ==========================================

/**
 * builtin_jobs - Implements the 'jobs' built-in command. 
 * Lists all active background or stopped jobs currently tracked by the shell.
 */
void builtin_jobs(void) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid != 0) {
            char *stat_str = (job_list[i].state == RUNNING) ? "Running" : "Stopped";
            printf("[%d] (%d) %s %s\n", job_list[i].jid, (int)job_list[i].pid, stat_str, job_list[i].cmd);
        }
    }
}

/**
 * builtin_bg - Implements the 'bg <job>' built-in command. 
 * Resumes a stopped job in the background by sending the SIGCONT signal.
 */
void builtin_bg(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "bg: expected job id\n");
        return;
    }
    int jid = atoi(args[1] + 1); // Parse job ID after '%' symbol
    struct job *job = get_job_by_jid(jid);
    if (job == NULL) {
        fprintf(stderr, "bg: no such job\n");
        return;
    }
    job->state = RUNNING;
    printf("[%d] Running %s\n", job->jid, job->cmd);
    kill(-job->pid, SIGCONT); // Continue process group in background
}

/**
 * builtin_fg - Implements the 'fg <job>' built-in command. 
 * Brings a background or stopped job into the foreground and waits for it to complete.
 */
void builtin_fg(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "fg: expected job id\n");
        return;
    }
    int jid = atoi(args[1] + 1);
    struct job *job = get_job_by_jid(jid);
    if (job == NULL) {
        fprintf(stderr, "fg: no such job\n");
        return;
    }

    job->state = RUNNING;
    pid_t pid = job->pid;
    
    // Transfer terminal control to the job's process group
    tcsetpgrp(shell_terminal, pid);
    kill(-pid, SIGCONT); // Resume execution if it was stopped

    int status;
    // Wait for the foreground job to finish or get stopped by Ctrl+Z
    waitpid(pid, &status, WUNTRACED);

    // Reclaim terminal control back to the shell
    tcsetpgrp(shell_terminal, shell_pgid);

    if (WIFSTOPPED(status)) {
        job->state = STOPPED;
        printf("\n[%d] Stopped %s\n", job->jid, job->cmd);
    } else {
        remove_job(pid); // Clean up if it exited normally
    }
}

/**
 * builtin_kill - Implements the 'kill' built-in command. 
 * Terminates a specified job using its PID or job ID (JID).
 */
void builtin_kill(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "kill: expected pid or job id\n");
        return;
    }
    pid_t pid;
    if (args[1][0] == '%') {
        int jid = atoi(args[1] + 1);
        struct job *job = get_job_by_jid(jid);
        if (job == NULL) {
            fprintf(stderr, "kill: no such job\n");
            return;
        }
        pid = job->pid;
    } else {
        pid = atoi(args[1]);
    }
    kill(pid, SIGTERM); // Send termination signal
    remove_job(pid);
}

/**
 * runcommand - Executes parsed command tokens, handles built-ins, 
 * creates child processes via fork(), and manages foreground/background process execution.
 */
int runcommand(char **cline, int where) {
    pid_t pid;
    int status;

    // Check if the command matches any shell built-ins before forking
    if (strcmp(cline[0], "jobs") == 0) {
        builtin_jobs();
        return 0;
    }
    if (strcmp(cline[0], "bg") == 0) {
        builtin_bg(cline);
        return 0;
    }
    if (strcmp(cline[0], "fg") == 0) {
        builtin_fg(cline);
        return 0;
    }
    if (strcmp(cline[0], "kill") == 0) {
        builtin_kill(cline);
        return 0;
    }
    if (strcmp(cline[0], "exit") == 0) {
        exit(0);
    }

    // Reconstruct the full command string for storage in the job table
    char full_cmd[MAXBUF] = "";
    for (int i = 0; cline[i] != NULL; i++) {
        strcat(full_cmd, cline[i]);
        strcat(full_cmd, " ");
    }

    // Fork a new process to run external Unix commands
    switch (pid = fork()) {
        case -1:
            perror("smallsh");
            return (-1);
            
        case 0: // ================= CHILD PROCESS =================
            // Reset signals back to default behavior for the executed program
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

            setpgid(0, 0); // Put child in its own process group
            if (where == FOREGROUND) {
                tcsetpgrp(shell_terminal, getpid()); // Give terminal control to foreground child
            }

            // Replace child process image with the target program using PATH lookup
            execvp(cline[0], cline);
            perror(cline[0]); // Executed only if execvp fails
            exit(1);
        
        default: // ================= PARENT PROCESS (SHELL) =================
            setpgid(pid, pid);

            if (where == BACKGROUND) {
                // Background job: Add to job list and return immediately without waiting
                add_job(pid, RUNNING, full_cmd);
                printf("[%d] %d\n", next_jid - 1, (int)pid);
                return (0);
            } else {
                // Foreground job: Track job, pass terminal control, and wait for completion/stop
                add_job(pid, RUNNING, full_cmd);
                tcsetpgrp(shell_terminal, pid);

                // waitpid with WUNTRACED tracks both termination and process stops (Ctrl+Z)
                if (waitpid(pid, &status, WUNTRACED) == -1) {
                    perror("waitpid");
                }

                // Reclaim terminal control back to the shell
                tcsetpgrp(shell_terminal, shell_pgid);

                if (WIFSTOPPED(status)) {
                    // If the user pressed Ctrl+Z, mark the job as stopped
                    struct job *j = get_job_by_pid(pid);
                    if (j) j->state = STOPPED;
                    printf("\n[%d]+ Stopped\t%s\n", j ? j->jid : 0, full_cmd);
                } else {
                    // Otherwise, clean it up from the job table
                    remove_job(pid);
                }
                return (status);
            }
    }
}

/**
 * procline - Parses lines token-by-token and dispatches commands to runcommand().
 */
int procline(void) {
    char *arg[MAXARG + 1];
    int toktype;
    int narg = 0;
    int type;

    for (;;) {
        switch (toktype = gettok(&arg[narg])) {
            case ARG:
                if (narg < MAXARG)
                    narg++;
                break;
            case EOL:
            case SEMICOLON:
            case AMPERSAND:
                // Determine whether execution should be in background or foreground
                type = (toktype == AMPERSAND) ? BACKGROUND : FOREGROUND;
                if (narg != 0) {
                    arg[narg] = NULL;
                    runcommand(arg, type);
                }
                if (toktype == EOL)
                    return 0;
                narg = 0;
                break;
        }
    }
}

/**
 * main - Main execution loop of the smallsh command processor shell.
 */
int main(void) {
    char *prompt = "smallsh> ";
    init_shell();

    // Automatically reap background zombie processes when they exit
    signal(SIGCHLD, SIG_IGN);

    // Main shell loop: Reads input lines and processes them until EOF (Ctrl+D)
    while (userin(prompt) != EOF) {
        procline();
    }
    return 0;
}
