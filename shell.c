/* Copyright (C) 2021 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// Code inspired by https://github.com/brenns10/lsh

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/wait.h>

#include "commands.h"
#include "sys.h"
#include "shell.h"


#define SHELL_LINE_BUFSIZE 1024
#define SHELL_TOK_BUFSIZE  128
#define SHELL_TOK_DELIM    " \t\r\n\a"


/**
 * Callback function for main function of commands
 **/
typedef int (main_t)(int argc, char **argv);


/**
 * Map names of commands to function entry points.
 **/
typedef struct shell_command {
  const char *name;
  main_t *main;
  int fork;
} shell_command_t;


static int main_help(int argc, char **argv);


shell_command_t commands[] = {
  {"cd", main_cd, 0},
  {"cp", main_cp, 1},
  {"dmesg", main_dmesg, 1},
  {"env", main_env, 0},
  {"exit", main_exit, 0},
  {"help", main_help, 1},
  {"id", main_id, 1},
  {"jailbreak", main_jailbreak, 0},
  {"kill", main_kill, 1},
  {"ls", main_ls, 1},
  {"mkdir", main_mkdir, 1},
  {"mount", main_mount, 1},
  {"pwd", main_pwd, 1},
  {"rmdir", main_rmdir, 1},
  {"sleep", main_sleep, 1},
  {"stat", main_stat, 1},
  {"uname", main_uname, 1},
};


#define NB_SHELL_COMMANDS (sizeof(commands)/sizeof(shell_command_t))



/**
 * Read a line from stdin
 **/
static char*
shell_readline(void) {
  int bufsize = SHELL_LINE_BUFSIZE;
  int position = 0;
  char *buffer_backup;
  char *buffer = malloc(sizeof(char) * bufsize);
  char c;

  if(!buffer) {
    fprintf(stderr, "malloc: %s\n", strerror(errno));
    return NULL;
  }

  while(1) {
    int len = read(STDIN_FILENO, &c, 1);
    if(len == -1 && errno == EINTR) {
      continue;
    }

    if(len <= 0) {
      free(buffer);
      return NULL;
    }

    if(c == '\n') {
      buffer[position] = '\0';
      return buffer;
    }
  
    buffer[position++] = c;

    if(position >= bufsize) {
      bufsize += SHELL_LINE_BUFSIZE;
      buffer_backup = buffer;
      buffer = realloc(buffer, bufsize);
      if(!buffer) {
	fprintf(stderr, "realloc: %s\n", strerror(errno));
	free(buffer_backup);
	return NULL;
      }
    }
  }
}


/**
 * Split a string into an array of substrings seperated by 
 * SHELL_TOK_DELIM
 **/
static char**
shell_splitline(char *line) {
  int bufsize = SHELL_TOK_BUFSIZE;
  int position = 0;
  char **tokens = malloc(bufsize * sizeof(char*));
  char *token, **tokens_backup;

  if(!tokens) {
    printf("malloc: %s\n", strerror(errno));
    return NULL;
  }

  token = strtok(line, SHELL_TOK_DELIM);
  while(token != NULL) {
    tokens[position] = token;
    position++;

    if(position >= bufsize) {
      bufsize += SHELL_TOK_BUFSIZE;
      tokens_backup = tokens;
      tokens = realloc(tokens, bufsize * sizeof(char*));
      if(!tokens) {
	printf("realloc: %s\n", strerror(errno));
	free(tokens_backup);
	return NULL;
      }
    }
    
    token = strtok(NULL, SHELL_TOK_DELIM);
  }
  tokens[position] = NULL;
  return tokens;
}


/**
 * Print the shell prompt to stdout
 **/
static void
shell_prompt(void) {
  char buf[PATH_MAX];
  char *cwd;

  if(!(cwd = getenv("PWD"))) {
    cwd = getcwd(buf, sizeof(buf));
  }
  
  fprintf(stdout, "%s$ ", cwd ? cwd : "(null)");
}


/**
 * Fork the execution of a command
 **/
static int
shell_fork(main_t *main, int argc, char **argv) {
  pid_t pid = sys_fork();  
  if (pid == 0) {
    int rc = main(argc, argv);
    exit(rc);
    return 0;
    
  } else if (pid < 0) {
    fprintf(stderr, "fork: %s\n", strerror(errno));
    return -1;
    
  } else {
    int status = 0;
    do {
      sys_waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    return WEXITSTATUS(status);
  }
}


/**
 * Execute a shell command
 **/
static int
shell_execute(char **argv) {
  shell_command_t *cmd = commands;
  int argc = 0;

  while(argv[argc]) {
    argc++;
  }
  
  if(!argc) {
    return -1;
  }

  for(int i=0; i<NB_SHELL_COMMANDS; i++) {
    if(strcmp(argv[0], commands[i].name)) {
      continue;
    }
    
    if(commands[i].fork) {
      return shell_fork(commands[i].main, argc, argv);
    } else {
      return commands[i].main(argc, argv);
    }
  }
  
  printf("%s: command not found\n", argv[0]);
  return -1;
}


/**
 * Shell entry point
 **/
void
shell_loop(void) {
  char **history = NULL;
  int running = 1;
  
  printf("\n");
  printf("Welcome to a tiny PS4 shell, ");
  printf("compiled %s at %s\n", __DATE__, __TIME__);
  printf("Type 'help' for a list of commands\n");
  printf("\n");

  setenv("HOME", "/", 0);
  setenv("PWD", "/", 0);
  

  while(running) {
    char *line = NULL;
    char **args = NULL;
    int exit_code = 0;
    
    shell_prompt();

    setvbuf(stdout, NULL, _IONBF, 0);
    line = shell_readline();
    setvbuf(stdout, NULL, _IOLBF, 0);
    
    if(line) {
      args = shell_splitline(line);
    }
    if(args) {
      exit_code = shell_execute(args);
    }

    fflush(NULL);
    
    if(line) {
      free(line);
    }
    if(args) {
      free(args);
    }
  }
}


/**
 * Display a list of registered commands
 **/
static int
main_help(int argc, char **argv) {
  printf("Available commands are:\n");
  for(int i=0; i<NB_SHELL_COMMANDS; i++) {
    printf("  %s\n", commands[i].name);
  }
  
  return 0;
}

