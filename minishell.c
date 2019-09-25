#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

void check_children(int*, int**);
void end_children(pid_t* children, int num_chil);

//Functions that check and do things with input
void check_ignore(char[], int*);
void split_input(char[], char[], char***, int*, char**, int);
int check_specials(char***, int, char**, char**, int*);
void create_function_args(char***, char**, char**, int, int, int);
void expand(char**, int);

//Perform functions
void check_command(pid_t*, int*, char*, char***, char***, char*, char*, int, int, int, int*, pid_t**);

//Built in functions
int cd_func(char*);
void status_func(int);

//Used by child (redirect not allowed for built in functions)
void redirect_out(char*, char*, char***, char***, int, int, int, char*);
void redirect_in(char*, char*, char***, char***, int, int, int, char*);

void free_mem(char***, char***, int, int, char**, char**, char**);
void ignore_bg();

int allow_back = 1;	//For SIGTSTP, toggles background feature
int child_running = 0, term = 0;	//For SIGINT, if a child foreground process is running


int main(){
	/* For inputs */
	char input[2060], copy[2060], **arguments = NULL, **funargs = NULL, *command;
	char *infile, *outfile;
	int args, i, adjust, background, ignore;
	memset(copy, '\0', 2060);

	/* For IDs and Child processes */
	int process_id = getpid();
	pid_t childID = -5, *children = NULL;
	int child_exit = -5, num_chil = 0;
	//printf("ProcessID: %d\n", process_id); fflush(stdout);

	struct sigaction SIGTSTP_action = {0};		//Handle SIGTSTP for toggling background processes
	SIGTSTP_action.sa_handler = ignore_bg;		//Perform the ignore_bg function in SIGTSTP_action
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);	//Catch SIGTSTP and do the action

	struct sigaction ignore_action = {0};
	ignore_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &ignore_action, NULL);

	do{
		background = 0; args = 0; adjust = 0;
		infile = NULL; outfile = NULL; command = NULL;

		if(num_chil > 0){	//If there are children in background, check before getting input
			check_children(&num_chil, &children);
		}

		memset(input, '\0', 2060);				//Empty input before prompting user
		printf(":"); fflush(stdout);			//Print input prompt and flush stdout
		fgets(input, 2060, stdin);				//Read the user's input through stdin
		input[strlen(input)-1] = '\0';			//Remove newline char from input
		//printf("Your input was: %s with length: %d\n", input, strlen(input)); fflush(stdout);	//Print input
		check_ignore(input, &ignore);	

		if(ignore == 0){	//If ignore == 1, ignore input
			strcpy(copy, input);					//Copy the input, copy used to copy arguments
			split_input(input, copy, &arguments, &args, &command, process_id);	//Split input into argument list and count
				adjust = check_specials(&arguments, args, &infile, &outfile, &background);	//Check for < > &
				if(adjust > -1){	//If -1, too many arguments provided
					create_function_args(&funargs, arguments, &command, args, adjust, process_id);

					check_command(&childID, &child_exit, command, &funargs, &arguments, outfile, infile, args, adjust, background, &num_chil, &children);
				}

			free_mem(&arguments, &funargs, args, adjust, &command, &infile, &outfile);	
		}
	}while(strcmp(input, "exit") != 0);
	end_children(children, num_chil);
	if(children != NULL){
		free(children);
		children == NULL;
	}

	return 0;
}



void check_children(int* num_chil, pid_t** children){
	int i, child_exit, exit_status, result, j, complete = 0;
	pid_t* temp = NULL;
	for(i=0; i<*num_chil; i++){	//For all children
		result = waitpid((*children)[i], &child_exit, WNOHANG);	//Check if they have finished
		if(result == (*children)[i]){	//If child's process id is returned, it has completed
			complete = 1;
			if(WIFEXITED(child_exit)){	//Check if child exited normally
				exit_status = WEXITSTATUS(child_exit);
				printf("background pid %d is done: exit value %d\n", (*children)[i], exit_status); fflush(stdout);	//Print child's pid and exit value
			}
			else{						//Else child was terminated by signal
				exit_status = WTERMSIG(child_exit);
				printf("background pid %d is done: exit value %d\n", (*children)[i], exit_status); fflush(stdout);	//Print child's pid and exit value
			}
			for(j=i; j<*num_chil-1; j++){	//Shift all children's process id down in the array
				(*children)[j] = (*children)[j+1];
			}
			i -= 1;			//Adjust index to compensate for shifting
			*num_chil -= 1;	//Adjust limit of for loop to compensate for shifting
		}
	}
	if(complete == 1){	//If a child has completed
		temp = malloc(sizeof(pid_t) * (*num_chil));	//Make a new array to store the children still running
		for(i=0; i<(*num_chil); i++){
			temp[i] = (*children)[i];	//Since remaining children id shifted down in array, can copy directly over
		}
		free(*children);	//Free the old array
		*children = temp;	//Set the children array to new array
		temp = NULL;
	}
}

void end_children(pid_t* children, int num_chil){
	int i;
	//printf("Number of children: %d\n", num_chil); fflush(stdout);
	for(i=0; i<num_chil; i++){
		//printf("Child pid: %d\n", (children)[i]);
		kill(children[i], SIGTERM);	//Kill all child processes on exit
	}
}



/* Functions that check and do things with input */
void check_ignore(char input[], int* ignore){
	int i;
	if(strlen(input) > 0 && strlen(input) < 2048){	//If input is not empty or too long
		for(i=0; i<strlen(input); i++){	//Check if input is just spaces
			if(input[i] != ' '){		//If one char is not a space, then it's a valid input
				*ignore = 0;
				if(input[i] == '#')		//However, if the first char encountered is a #, ignore the input
					*ignore = 1;
				i += strlen(input);
			}
		}
	}
	else
		*ignore = 1;
}

void split_input(char input[], char copy[], char*** arguments, int* args, char** command, int pid){
	char *arg, *detect;
	int i;
	arg = strtok(input, " '\n'");			//First strtok is the command
	*command = malloc(sizeof(char) * strlen(arg)+1);
	memset(*command, '\0', strlen(arg)+1);
	strcpy(*command, arg);

	//printf("The command is: %s\n", *command); fflush(stdout);
	while(arg != NULL){						//Get the number of arguments in the input
		arg = strtok(NULL, " '\n'");
		//printf("Argument %d:%s\n", *args, arg); fflush(stdout);
		(*args) += 1;
	}
	(*args) -= 1;	//Remove the additional (null) char that was counted
	//printf("There were %d arguments.\n", *args); fflush(stdout);

	if((*args) > 0){	//If there are arguments
		arg = strtok(copy, " '\n'");				//First strtok is the command
		(*arguments) = malloc(sizeof(char*) * (*args));	//Allocate memory for all of the arguments
		for(i=0; i<(*args); i++){						//Copy the arguments into an array
			arg = strtok(NULL, " '\n'");
			//printf("Copying argument %d:%s\n", i+1, arg); fflush(stdout);
			(*arguments)[i] = malloc(sizeof(char) * strlen(arg)+1);
			memset((*arguments)[i], '\0', strlen(arg)+1);
			strcpy((*arguments)[i], arg);

			detect = strstr((*arguments)[i], "$$");	//Check for $$, if found, expand into process id
			if(detect != NULL)
				expand(&(*arguments)[i], pid);
		}
		strtok(NULL, " '\n'");	//Empty strtok
	}
}

int check_specials(char ***arguments, int args, char** infile, char** outfile, int* background){
	int i = 0, file_out = 1, file_in = 1;
	int adjust = 0;		//Keep track of how much to adjust indexing. Ex: if < or > is found, shift indexing by 2 to ignore filename and arrow once they have been processed
	int arglen = args;	//Store original number of arguments
	if(args > 0){
		if(strcmp((*arguments)[arglen-1], "&") == 0){	//If last argument is &
			//printf("Put process in background!\n"); fflush(stdout);
			if(allow_back == 1){	//If not in foreground only mode
				*background = 1;
			}
			args -= 1;		//One less argument to consider
			adjust += 1;	//One less argument to consider when indexing
		}
		while(i<2 && args >= 2){		//Loop twice if there are 2 or more arguments left
			if(args >= 2){				//If there are at least 2 more arguments left
				if(strcmp((*arguments)[arglen-(2+adjust)], ">") == 0 && file_out == 1){	//If there hasn't been a file output yet and if a > is found
					//printf("Redirect detected! New filename: %s\n", (*arguments)[arglen-(1+adjust)]); fflush(stdout);
					*outfile = malloc(sizeof(char) * strlen((*arguments)[arglen-(1+adjust)])+1);
					memset(*outfile, '\0', strlen((*arguments)[arglen-(1+adjust)])+1);
					strcpy(*outfile, (*arguments)[arglen-(1+adjust)]);	//Store outfile name
					args -= 2;			//Two less arguments to consider / fileout and arrow aren't arguments
					adjust += 2;		//Two less arguments to consider when indexing
					file_out = 0;	//File output has been done
				}
			}
			if(args >= 2){				//If there are at least 2 more arguments left
				if(strcmp((*arguments)[arglen-(2+adjust)], "<") == 0 && file_in == 1){	//If there hasn't been a file input yet and if a < is found
					//printf("Input detected! Filename: %s\n", (*arguments)[arglen-(1+adjust)]); fflush(stdout);
					*infile = malloc(sizeof(char) * strlen((*arguments)[arglen-(1+adjust)])+1);
					memset(*infile, '\0', strlen((*arguments)[arglen-(1+adjust)])+1);
					strcpy(*infile, (*arguments)[arglen-(1+adjust)]);	//Store infile name
					args -= 2;			//Two less arguments to consider / filein and arrow aren't arguments
					adjust += 2;		//Two less arguments to consider when indexing
					file_in = 0;	//File input has been done
				}
			}
			i++;
		}
	}
	if(args > 512){
		//printf("TOO MANY ARGUMENTS! ABORT!\n"); fflush(stdout);
		return -1;
	}
	return adjust;
}

void create_function_args(char*** funargs, char** arguments, char** command, int args, int adjust, int process_id){
	int i;
	char* detect;
	(*funargs) = malloc(sizeof(char*) * (args-adjust+2));		//Allocate memory for the command and all of the arguments

	detect = strstr(*command, "$$");	//Expand $$ in command into process id if detected
	if(detect != NULL)
		expand(command, process_id);
	
	(*funargs)[0] = malloc(sizeof(char) * strlen(*command)+1);	//Allocate memory for command
	memset((*funargs)[0], '\0', strlen(*command)+1);
	strcpy((*funargs)[0], *command);								//Copy command into first slot
	//printf("Create_function_args:: Command argument:%s\n", (*funargs)[0]); fflush(stdout);
	for(i=0; i<(args-adjust); i++){
		(*funargs)[i+1] = malloc(sizeof(char) * (strlen(arguments[i])+1));	//Allocate memory for arguments
		memset((*funargs)[i+1], '\0', strlen(arguments[i])+1);
		strcpy((*funargs)[i+1], arguments[i]);								//Copy arguments into array
		//printf("Create_function_args:: Function arguments:%s\n", (*funargs)[i]); fflush(stdout);
	}
	(*funargs)[args-adjust+1] = NULL;	//End argument list with a NULL pointer
}

void expand(char** var, int pid){	//Only works for dynamically allocated arrays, can't free string literal
	char charid[10];
	sprintf(charid, "%d", pid);	//Convert process id into a char array
	int i, length, j;
	for(i=0; i<(strlen(*var)-1); i++){	//For the length of the argument
		if((*var)[i] == '$' && (*var)[i+1] == '$'){	//If $$ is found
			length = strlen(*var) - 2 + strlen(charid);	//-2 $$, + length of process id
			char* temp;
			temp = malloc(sizeof(char) * length+1);	//Allocate memory for expanded argument, +1 for null char
			memset(temp, '\0', length+1);
			for(j=0; j<length; j++){
				if(j<i)									//Copy the argument up to where the $$ is found
					temp[j] = (*var)[j];
				else if(j>=i && j<(i+strlen(charid)))	//Copy the processid into where the $$ used to be
					temp[j] = charid[j-i];
				else if(j>=(i+strlen(charid)))			//Copy the rest of the argument after the $$
					temp[j] = (*var)[j-strlen(charid)];
			}
			free(*var);		//Free the unexpanded argument
			*var = temp;	//Save the unexpanded argument
			temp = NULL;
			i += strlen(*var);	//Break out of the for loop
		}
	}
}



/* Perform functions */
void check_command(pid_t* childID, int* child_exit, char* command, char*** funargs, char*** arguments, char* outfile, char* infile, int args, int adjust, int background, int* num_chil, pid_t** children){
	int i;
	if(strcmp(command, "exit") != 0){	//If command is exit, don't make child
		if(strcmp(command, "cd") == 0)	//If command is cd, do cd function
			*child_exit = cd_func((*funargs)[1]);
		else if(strcmp(command, "status") == 0)	//If command is status, do status function
			status_func(*child_exit);	//Print exit status of previous child

		else{
			*childID = fork();	//Fork process
			if(*childID == 0){	//If child, not parent
				if(background == 0){	//If child is in background, keep ignoring sigint
					struct sigaction newSIGINT = {0};
					newSIGINT.sa_handler = SIG_DFL;
					sigaction(SIGINT, &newSIGINT, NULL);	//Reset signal handler for child process
				}

				free(*children);	//Children don't have to worry about siblings
				int newID = getpid();	//Child's process ID
				//printf("Child PID: %d\nUsing getpid: %d\n", *childID, newID); fflush(stdout);
				
				if(background == 1){
					printf("background pid is %d\n:", newID); fflush(stdout);
				}
				redirect_out(infile, outfile, arguments, funargs, args, adjust, background, command);
				redirect_in(infile, outfile, arguments, funargs, args, adjust, background, command);

				if(execvp(command, *funargs) == -1){	//If exec can't run command, free memory and exit
					printf("%s: Command not found\n", command); fflush(stdout);
					free_mem(arguments, funargs, args, adjust, &command, &infile, &outfile);
					exit(1);
				}	//Note: When exec is successful, it automatically frees memory
			}

			if(background == 0){
				//printf("Waiting for child to finish\n"); fflush(stdout);
				waitpid(*childID, child_exit, 0);	//Waits for foreground to terminate, also gets exit status of child

				if(WIFSIGNALED(*child_exit) != 0){	//Check for signal termination
					if(WTERMSIG(*child_exit) == 2){	//If child process got SIGINTed, immediately print termination signal number
						printf("terminated by signal %d\n", *child_exit); fflush(stdout);
					}
				}
				//printf("Child terminated\n"); fflush(stdout);
			}
			else if(background == 1){
				*num_chil += 1;
				pid_t* temp = *children;
				*children = malloc(sizeof(pid_t) * (*num_chil));
				for(i=0; i<(*num_chil-1); i++){
					(*children)[i] = temp[i];
				}
				free(temp);
				(*children)[i] = *childID;
			}
		}
	}
}



/* Built in functions */
int cd_func(char* path){
	int result;
/*
	char cwd[200];					//To test current directory function
	memset(cwd, '\0', 200);
	getcwd(cwd, sizeof(cwd));
	printf("Directory before CD: %s\n", cwd); fflush(stdout);
*/
	if(path == NULL || strcmp(path, "~") == 0){	//If path specified doesn't exist or is '~'
		result = chdir(getenv("HOME"));			//Go to home directory
		//getcwd(cwd, sizeof(cwd));
		//printf("Current Dir after CD: %s\n", cwd); fflush(stdout);
	}
	else{
		result = chdir(path);		//Else go to directory specified (works for absolute and relative paths)
		//getcwd(cwd, sizeof(cwd));
		//printf("Current Dir after CD: %s\n", cwd); fflush(stdout);
	}
	if(result == -1){	//If changing directory failed
		printf("error, directory could not be changed\n"); fflush(stdout);
		return 1;
	}
	return 0;
}

void status_func(int child_exit){
	int exit_status;
	if(WIFEXITED(child_exit)){	//Check if child exited normally
		exit_status = WEXITSTATUS(child_exit);
		printf("exit value %d\n", exit_status); fflush(stdout);
	}
	else{						//Else child was terminated by signal
		exit_status = WTERMSIG(child_exit);
		printf("terminated by signal %d\n", exit_status); fflush(stdout);
	}
}



/* Used by child to redirect (built in functions not allowed to redirect) */
void redirect_out(char* infile, char* outfile, char*** arguments, char*** funargs, int args, int adjust, int background, char* command){
	int out;
	if(outfile != NULL)									//If an output file was specified (for foreground or background)
		out = open(outfile, O_CREAT|O_TRUNC|O_WRONLY, 0644);	//Create/find a file to write to, permission: 4=read, 2=write, 1=exec
	else if(outfile == NULL && background == 1)			//If background process, but no file
		out = open("/dev/null", O_WRONLY);				//If background process, but file not specified, dump to /dev/null
	
	if(outfile != NULL || background == 1){				//If any redirection needs to happen (file specified/background child)
		if(out == -1){									//If file could not be opened for writing
			printf("Cannot open %s for writing\n", outfile); fflush(stdout);
			free_mem(arguments, funargs, args, adjust, &command, &infile, &outfile);	//Free memory and exit
			exit(1);
		}
		dup2(out, 1);						//Redirect standard out to the output file
		fcntl(out, F_SETFD, FD_CLOEXEC);	//Stop redirect after execute
	}
}

void redirect_in(char* infile, char* outfile, char*** arguments, char*** funargs, int args, int adjust, int background, char* command){
	int in;
	if(infile != NULL)									//If an input file was specified (for foreground or background)
		in = open(infile, O_RDONLY);					//Open the file to read
	else if(infile == NULL && background == 1)			//If background process, but no file
		in = open("/dev/null", O_RDONLY);				//If process is background, but file not specified, read from /dev/null

	if(infile != NULL || background == 1){				//If any redirection needs to happen (file specified/background child)
		if(in == -1){
			if(errno == ENOENT){						//If file doesn't exist
				printf("%s: no such file or directory\n", infile); fflush(stdout);
			}
			else if(errno == EACCES){					//If file can't be read
				printf("Cannot open %s for input\n", infile); fflush(stdout);
			}
			free_mem(arguments, funargs, args, adjust, &command, &infile, &outfile);	//If errors happen, free memory and exit
			exit(1);
		}
		dup2(in, 0);						//Redirect standard in to the input file
		fcntl(in, F_SETFD, FD_CLOEXEC);		//Stop redirect after execute
	}
}



void free_mem(char*** arguments, char*** funargs, int args, int adjust, char** command, char** infile, char** outfile){
	int i;
	if((*funargs) != NULL){			//If there were arguments
		for(i=0; i<(args-adjust+1); i++){	//Free memory
			//printf("Freeing :%s\n", (*funargs)[i]);
			free((*funargs)[i]);
		}
		free(*funargs);
		(*funargs) = NULL;
	}
	if((*arguments) != NULL){		//If there were arguments
		for(i=0; i<args; i++){		//Free memory
			free((*arguments)[i]);
		}
		free(*arguments);
		(*arguments) = NULL;
	}
	if((*command) != NULL){			//If there was a command
		free(*command);				//Free memory
		*command = NULL;
	}
	if((*infile) != NULL){			//If there was an infile
		free(*infile);				//Free memory
		*infile = NULL;
	}
	if((*outfile) != NULL){			//If there was an outfile
		free(*outfile);				//Free memory
		*outfile = NULL;
	}
}

void ignore_bg(){
	if(allow_back == 1){
		allow_back = 0;
		const char message[] = "\nEntering foreground-only mode (& is now ingnored)\n";
		write(1, message, sizeof(message));	fflush(stdout);	//Can't use printf() functions in signal handlers
	}
	else if(allow_back == 0){
		allow_back = 1;
		const char message[] = "\nExiting foreground-only mode\n";
		write(1, message, sizeof(message)); fflush(stdout);	//Can't use printf() functions in signal handlers
	}
}
