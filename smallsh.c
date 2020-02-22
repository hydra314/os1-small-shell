#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

#define STR_LIMIT 2048
#define ARG_LIMIT 512

void cdCommand(char **, int);
void statusCommand(int);
void useShell(char **, int, int *, int);
void executeCommand(char **, int, int *, int, int, char *, char *, int);
void getFilePath(char *);
void checkForRedirects(char **, int *, char *, char *, int *, int *);
void sigintHandler(int);
void sigtstpHandler(int);
int mode;

// Prevent Ctrl + C from exiting the program
void sigintHandler(int sig){
	signal(SIGINT, sigintHandler);
	printf("\nTerminated by signal %d\n", sig); fflush(stdout);
}

// Prevent Ctrl + Z from exiting, make it toggle between modes (& allowed and not allowed)
void sigtstpHandler(int sig){
	signal(SIGTSTP, sigtstpHandler);
	if(mode == 0){
		mode = 1;
		printf("\nEntering foreground-only mode (& is now ignored).\n: "); fflush(stdout);
	}

	else if(mode == 1){
		mode = 0;
		printf("\nExiting foreground-only mode.\n: "); fflush(stdout);
	}
}

// This function is only here for debugging and testing purposes
void pwd(){
	char cwd[1024];
	if(getcwd(cwd, sizeof(cwd)) != NULL){
		fprintf(stdout, "Current working directory: %s\n", cwd);
		fflush(stdout);
	}
}

// Execute the 'cd' command
void cdCommand(char *args[ARG_LIMIT], int argc){
	if(argc == 1){	// no arguments
		chdir(getenv("HOME"));
	}
	else if(argc == 2){	// 1 argument
		//printf("%s\n", arg);
		chdir(args[1]);
	}
	else{	// more than 1 argument
		fprintf(stderr, "cd only accepts 1 argument\n");
	}

	//pwd();
}

// Display the last exit status or the terminating signal of the last foreground process
// Nonnegative status: exit
// Negative status: signal
void statusCommand(int status){
	if(status >= 0)
		printf("Exit value %d\n", status);
	else if(status < 0)
		printf("Terminated by signal %d\n", status * -1);
	fflush(stdout);
}

// Search the path for a file
void getFilePath(char *file){
	if(file[0] == '/')
		return;		// no processing needed on an absolute path

	//printf("Getting file path...\n");
	int i = 0, j = 0;
	char *pathTokens[256], buffer[256], *path;
	path = getenv("PATH");

	// Split path variable into tokens
	pathTokens[i] = strtok(path, ":");
	while(pathTokens[i] != NULL){
		pathTokens[++i] = strtok(NULL, ":");
		//printf("%s\n", pathTokens[i - 1]);
	}

	// Try all path tokens with the file. Use access() to see if the file exists.
	for(j = 0; j < i; j++){
		strcpy(buffer, "");		// empty buffer at beginning of loop
		strcat(buffer, pathTokens[j]);
		strcat(buffer, "/");
		strcat(buffer, file);
		if(access(buffer, F_OK) == 0){	// if the file exists
			strcpy(file, buffer);
			return;
		}
	}
}

// Check to see if any '>' or '<' redirects are used in a command
void checkForRedirects(char *args[ARG_LIMIT], int *argc, char *file, char *file2, int *redirStdin, int *redirStdout){
	int i;
	//printf("Checking for redirects...\n");
	//printf("argc: %d\n", *argc);
	for(i = 0; i < *argc; i++){
		//printf("%d: %s\n", i, args[i]);
		if(strcmp(args[i], "$$") == 0){	// Convert $$ to the process ID
			int pid = getpid();
			sprintf(args[i], "%d", pid);
		}
		if(strcmp(args[i], "<") == 0 && i < *argc - 1 && i > 0){	// the '<' character can't be the first or last argument
			strcpy(file, args[i + 1]);	// the target file is the arg to the right of the '<'
			//printf("filename: %s\n", file);
			getFilePath(file);
			//printf("file pathname: %s\n", file);

			// Remove '<' and affected file from arguments array
			args[i] = NULL;
			args[++i] = NULL;
			*redirStdin = 1;
		}
		else if(strcmp(args[i], ">") == 0 && i < *argc - 1 && i > 0){	// the '>' character can't be the first or last argument
			strcpy(file2, args[i + 1]); // the target file is the arg to the right of the '>'
			//printf("filename: %s\n", file);
			getFilePath(file2);
			//printf("file pathname: %s\n", file);
			
			// Remove '>' and affected file from arguments array
			args[i] = NULL;
			args[++i] = NULL;
			*redirStdout = 1;
		}
	}
	//printf("Your file path is: %s\n", file);
}

// Called by default if a built-in command isn't used
void useShell(char *args[ARG_LIMIT], int argc, int *status, int bg){
	int childProcess, i = 0, j, target, source, result, redirStdin = 0, redirStdout = 0, childExitMethod;
	char *file = (char *)malloc(sizeof(char) * 256), *file2 = (char *)malloc(sizeof(char) * 256);
	pid_t currChild;
	signal(SIGINT, sigintHandler);	// Make sure Ctrl + C exits child process

	childProcess = fork();
	if(childProcess < 0){
		*status = 1;
		exit(1);
	}
	else if(childProcess == 0){
		if(bg){	// if executed in background
			printf("\b\bBackground PID is %d\n: ", getpid()); fflush(stdout);
			signal(SIGINT, SIG_IGN);	// Ctrl + C shouldn't do anything to background process
		}
		checkForRedirects(args, &argc, file, file2, &redirStdin, &redirStdout);
		executeCommand(args, argc, status, redirStdin, redirStdout, file, file2, bg);
		fflush(stdout);
		exit(0);
	}
	else if(childProcess < 0){	// issue with fork()
		fprintf(stderr, "Error using fork().\n");
		*status = 1;
		return;
	}
	else if(!bg){	// parent process
		waitpid(childProcess, &childExitMethod, 0);	// wait for child to terminate

		// Make sure the right termination status is sent
		if(WIFSIGNALED(childExitMethod) == 0)	
			*status = WEXITSTATUS(childExitMethod);
		else if(WIFSIGNALED(childExitMethod) != 0)
			*status = -WTERMSIG(childExitMethod);
		free(file);
		free(file2);
	}
}

// Execute the non-built-in command entered by the user
void executeCommand(char *args[ARG_LIMIT], int argc, int *status, int redirStdin, int redirStdout, char *file, char *file2, int bg){
	int i, j, source, result, target, termSig;
	char *argv[ARG_LIMIT];

	if(redirStdin){	// stdin is redirected by '<': redirect stdin to the chosen file
		source = open(file, O_RDONLY);
		if(source == -1){ fprintf(stderr, "Cannot open %s for input.\n", file); exit(1); }
		result = dup2(source, 0);
		if(result == -1){ fprintf(stderr, "Canot redirect STDIN to %s.\n", file); exit(2); }
	}

	if(redirStdout){	// stdout is redirected by '>': redirect stdout to the chosen file
		target = open(file2, O_WRONLY | O_CREAT | O_TRUNC, 0664);
		if(target == -1){ fprintf(stderr, "Cannot open %s for input.\n", file2); exit(1);	}
		result = dup2(target, 1);
		if(result == -1){ fprintf(stderr, "Cannot redirect STDOUT to %s\n.", file2); exit(2); }
	}

	if(bg && !redirStdin){	// if process is in the background, redirect stdin to /dev/null
		source = open("/dev/null", O_RDONLY);
		if(source == -1){ fprintf(stderr, "Cannot open /dev/null for input.\n"); exit(1); }
		result = dup2(source, 0);
		if(result == -1){ fprintf(stderr, "Couldn't successfully redirect STDIN to /dev/null\n"); exit(2); }
	}

	for(i = 0, j = 0; i < argc; i++){
		if(args[i] != NULL){
			argv[j] = (char *)malloc(sizeof(char) * 64);
			strcpy(argv[j], args[i]);	// don't copy over nullified arguments - '>', '<', and the files affected by them
			j++;
		}
	}
	argv[j] = NULL;	// execvp requires the last string to be null

	// Make sure execution succeeds; if it doesn't, show an error and exit with status 1
	if(execvp(argv[0], argv) < 0){
		fprintf(stderr, "Invalid command: %s\n", argv[0]);
		if(source) close(source);	// close files
		if(target) close(target);
		exit(1);
	}
	
	fflush(stdout);
	if(source) close(source);	// close files
	if(target) close(target);
}

int main(int argc, char *argv[]){
	char input[STR_LIMIT], *args[ARG_LIMIT];
	int status = 0, i = 0, bg = 0, childProcess;
	mode = 0;
	signal(SIGTSTP, sigtstpHandler);	// Ctrl + Z should only affect the parent shell, so separate it from fg/bg processes

	// Keep running until user types in "exit"
	while(strcmp(input, "exit")){
		i = 0, bg = 0;
		signal(SIGINT, SIG_IGN);	// ignore Ctrl + C: make it not quit program
		printf(": "); fflush(stdout);
		fgets(input, STR_LIMIT, stdin);	// get input
		strtok(input, "\n");	// Strip trailing newline

		// Split input string into tokens
		args[i] = strtok(input, " ");
		while(args[i] != NULL){
			args[++i] = strtok(NULL, " ");
			// printf("%s\n", args[i - 1]);
		}

		// Test to see if the first arg of the input matches a built-in command or is a special case (comment, newline, ampersand at end)
		if(strcmp(args[0], "cd") == 0){
			cdCommand(args, i);
		}
		else if(strcmp(args[0], "status") == 0){
			statusCommand(status);
		}
		else if(strcmp(args[0], "exit") == 0){
			break;	// quit the program
		}
		else if(args[0][0] == '#'){
			;	// do nothing when comment is inputted
		}
		else if(args[0][0] == '\n'){
			;	// do nothing when newline is inputted
		}
		else if(strcmp(args[i - 1], "&") == 0){	// check for ampersand for background mode
			args[i - 1] = NULL;
			if(!mode)	// only go into background mode if in the correct mode
				bg = 1;
			useShell(args, i - 1, &status, bg);
		}
		else{
			useShell(args, i, &status, bg);
		}

		// Check to see if any background processes have terminated
		childProcess = waitpid(-1, &status, WNOHANG);
		while(childProcess > 0){
			printf("Background PID %d is done: ", childProcess); fflush(stdout);

			// Vary output by exit vs. signal
			if(WIFEXITED(&status))
				statusCommand(status);
			else if(WIFSIGNALED(&status))
				statusCommand((status) * -1);

			childProcess = waitpid(-1, &status, WNOHANG);
		}
	}

	return 0;
}