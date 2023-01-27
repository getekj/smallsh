#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#define MAX_SIZE 512

// Initializing global variables
// struc commandline will be used to store parsed strings from user input
 struct commandline {
      char* command;
      char* arguments[MAX_SIZE];  
      char* input;
      char* output;
      int background;
    };

// value of 0 indicates foreground mode is off, value of 1 indicates foreground mode is on
static int fg_only = 0;

// used the track the current exit status of the process
int EXIT_STATUS;

// function declarations
char* read_input(char* input);
struct commandline parse_input(char* line_input);
struct commandline clear_cdline(struct commandline cdline);
void fg_mode(int signo);

int main(void) {

  // string to store inital user input, maximum 2048 characters
  char cd_input[2048];
  // declaring line input variable to store input after expansion    
  char* line_input;
  // initializing exit status
  EXIT_STATUS = 0;
  // creating an array of integers to store the background pids
  int bg_pids[5] = {0};
  // variable to keep track index in bg_pids when adding new values
  int v = 0;
  // declaring variable to store child status
  int child_status;

  // initializing struct to cleared
  struct commandline cdline;
  cdline.command = NULL;
  // initializing args array with null pointers
  for (int i = 0; i < 512; i++) {
     cdline.arguments[i] = NULL;
  }
  cdline.input = NULL;
  cdline.output = NULL;
  cdline.background = 0;

  //initalizing SIGINT_action
  struct sigaction SIGINT_action = {0};
  SIGINT_action.sa_handler = SIG_IGN;
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = 0;
  sigaction(SIGINT, &SIGINT_action, NULL);


  //initializing SIGTSTP_action
  struct sigaction SIGTSTP_action = {0};
  SIGTSTP_action.sa_handler = fg_mode;
  SIGTSTP_action.sa_flags = SA_RESTART;
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);
  sigprocmask(SIG_BLOCK, &SIGTSTP_action.sa_mask, NULL);
  


  for (;;) {

    // unblocking SIGTSTP
    sigprocmask(SIG_UNBLOCK, &SIGTSTP_action.sa_mask, NULL);

    // printing prompt and reading input
    printf(": ");
    fflush(stdout);
    line_input = read_input(cd_input);

    // blocking SIGTSTP
    sigprocmask(SIG_BLOCK, &SIGTSTP_action.sa_mask, NULL);

    // parsing input 
    if (line_input != NULL) {  
      cdline = parse_input(line_input);

      
      // checking built in commands
      if (strcmp(cdline.command, "exit") == 0) {
        // checking for any background commands that need to be terminated
        for (int i = 0; i < 5; i++) {
          if (bg_pids[i] != 0) {
            kill(bg_pids[i], SIGTERM);
          }
        }
        exit(0);

      } else if (strcmp(cdline.command, "cd") == 0) {
          if (cdline.arguments[1] == NULL) {
              // if there are no arguments with cd, change current directory to home
              char* home = getenv("HOME");
              if (chdir(home) == -1) {
                printf("error cannot change dir to home\n");
              } 
          } else {
              // if there is an argument given, change to specified directory
              char* path = cdline.arguments[1];
              if (chdir(path) == -1) {
                printf("error cannot change dir to path\n");
              }
            }
      } else if (strcmp(cdline.command, "status") == 0) {
          // checking how process was terminated and printing exit value or signal number
          if (WIFEXITED(child_status)) {
                // if child process was terminated normally, can print the exit value from WEXITSTATUS
                EXIT_STATUS = WEXITSTATUS(child_status);
                printf("exit value is %d\n", EXIT_STATUS);
                fflush(stdout);
              } else {
                // otherwise the child process was terminated abnormally and WTERMSIG returns the signal number causing the termination
                EXIT_STATUS = WTERMSIG(child_status);
                printf("terminated by signal %d\n", WTERMSIG(child_status));
                fflush(stdout);
              }

      } else {
          // otherwise fork() to create a new child process

          pid_t spawnPid = fork();

          switch(spawnPid) {
            case -1:
              perror("fork()\n");
              exit(1);
              break;
            case 0:
              // In the child process
                           
              //ignoring sigint if the process is in the background
              if (cdline.background == 1) {
                SIGINT_action.sa_handler = SIG_IGN;
                sigaction(SIGINT, &SIGINT_action, NULL);

              // otherwise if it is a foreground process allow default action 
              } else if (cdline.background == 0) {
                  SIGINT_action.sa_handler = SIG_DFL;
                  sigaction(SIGINT, &SIGINT_action, NULL);
              }
              


              // if an output file is indicated
              if (cdline.output != NULL) {

                // open file, create a new file if not found
                int outputFD = open(cdline.output, O_WRONLY | O_CREAT | O_TRUNC, 0640);
                if (outputFD == -1) {
                  perror("error on open() for outputFD");
                  exit(1);
                }

                // redirect from standard output to the outputFD
                int result = dup2(outputFD, 1);
                if (result == -1) {
                  perror("dup2 error");
                  exit(2);
                }   

                //close the file
                fcntl(outputFD, F_SETFD, FD_CLOEXEC);

              }

              // if child process is a background process (with foreground mode off) and no output file indicated
              if (cdline.background == 1 && cdline.output == NULL && fg_only == 0) {
                
                int output_dev_null = open("/dev/null", O_WRONLY);
                if (output_dev_null == -1) {
                  perror("error on open() for output_dev_null");
                  exit(1);
                }

                // redirecting output to /dev/null pointed at by output_dev_null
                int result = dup2(output_dev_null, 1);
                if (result == -1) {
                  perror("dup2 error");
                  exit(2);
                }

                fcntl(output_dev_null, F_SETFD, FD_CLOEXEC);

              }

              // if there is an input file indicated on the commandline
              if (cdline.input != NULL) {

                  // open the file
                  int inputFD = open(cdline.input, O_RDONLY);
                  if (inputFD == -1) {
                    perror("error on open() for inputFD");
                    exit(1); 
                  }
                
                  // redirect stdin to inputFD
                  int result = dup2(inputFD, 0);
                  if (result == -1) {
                    perror("dup2 error for input");
                    exit(2); //mhm
                  }

                  // clase file descriptor
                  fcntl(inputFD, F_SETFD, FD_CLOEXEC);
                  
              }

              // if the child process is a background process (with foreground mode off) and there is no input indicated
               if (cdline.background == 1 && cdline.input == NULL && fg_only == 0) {
                
                 int input_dev_null = open("/dev/null", O_RDONLY);
                 if (input_dev_null == -1) {
                   perror("error on open() for input_dev_null");
                   exit(1);
                  }

                 // redirecting from stdin to /dev/null pointed to by input_dev_null
                 int result = dup2(input_dev_null, 1);
                 if (result == -1) {
                   perror("dup2 error");
                   exit(2);
                 }

                 fcntl(input_dev_null, F_SETFD, FD_CLOEXEC);

              }             

              // replacing child process with process indicated by command and passing arguments
              execvp(cdline.command, cdline.arguments);
              // execvp only returns on error
              perror("execvp error");
              exit(1);
              break;
            default:

              // in the parent process

              // if we are running a foreground process, or foreground mode is on
              if ((cdline.background == 0) || (fg_only == 1 && cdline.background == 1)) {

                // Last argument is 0, parent process waits for child process to complete
                spawnPid = waitpid(spawnPid, &child_status, 0);

              // otherwise we are running a background process
              } else if (cdline.background == 1) {

                // WNOHANG specified, if child hasn't terminated, waitpid will immediately return with value 0
                int wait_return_val = waitpid(spawnPid, &child_status, WNOHANG);
                printf("background pid is %d\n", spawnPid);
                // adding background pid to array 
                bg_pids[v] = spawnPid;
                // incrementing index, so next pid is not overwritten
                v++;
                fflush(stdout);
              }

              // updating exit status if the child exited normally or abnormally by signal
              if (WIFEXITED(child_status)) {
                EXIT_STATUS = WEXITSTATUS(child_status);
              } else {
                EXIT_STATUS = WTERMSIG(child_status);
              }
              
              break;
          } 
         
      }  
     
    } 
    // checking if any background proccesses have completed, iterating through bg_pids[]
     for (int x = 0; x < 6; x++) {
       int return_waitpid = waitpid(bg_pids[x], &child_status, WNOHANG);
       if (return_waitpid != 0 && return_waitpid != -1) {
         // printing exit status if process has completed and status number
          if (WIFEXITED(child_status)) {
                printf("Child %d exited with status %d\n", bg_pids[x], WEXITSTATUS(child_status));
              } else {
                printf("Child %d terminated by signal %d\n", bg_pids[x], WTERMSIG(child_status));
              }
       fflush(stdout);
       }
     }

    // freeing memory that was allocated for line_input 
    free(line_input);
        
  }
}   



char* read_input(char* input) {
  /* this function takes an input string as an argument, reads input from stdin, performs the variable expansion
   and returns a new string with the variable $$ expanded */
 
  // reading user input from commandline and storing in input
  fgets(input, MAX_SIZE, stdin);

  //checking if input is a comment or blank line, and returning NULL if true
  if (input[0] == '#' || strcmp(input, "\n") == 0) {
        return NULL; 
  }

  // variable expansion for "$$"

  // length of $$ will always be 2
  int var$$_length = 2;
  // getting pid of process
  pid_t pid = getpid();
  //assuming max digit length of 8 + null terminator for casting pid_t to a string
  char pid_str[9]; 
  // casting the pid to a string to be able to replace in input string
  sprintf(pid_str, "%d", pid);
  // finding length of pid_str and input in order to calculate memory needed for new string
  int pid_length = strlen(pid_str);
  int input_length = strlen(input);

  //iterate through the input string and count number of times "$$" appears
  int num_var = 0;
  int i = 0;
  while (i < input_length - 1) {
    if (input[i] == '$' && input[i+1] == '$') {
      num_var++;
      // increment by two so we do not double count $$, for ex if there is "$$$" in the input
      i = i + 2;
    } else {
      // otherwise continue to search through string 
        i++;
    }
  }

  // calculating the difference between the length of pid and $$
  int substr_dif = pid_length - var$$_length;
  // to get the expanded input length we multiply the number of times $$ appears in the string, times the difference
  // in size between pid and $$
  int exp_input_length = input_length + (num_var * substr_dif);

  // We will iterate through the input and replace the occurrences with the substring pid_str
 
  // use the calculated size of the new expanded input (exp_input) and allocated an extra byte for the null terminator
  char* exp_input = malloc(sizeof(char) * (exp_input_length + 1));
  // index to follow old input
  int j = 0;
  // index to follow new expanded input
  int k = 0;

  while (j < input_length - 1) {
    if (input[j] == '$' && input[j+1] == '$') {
      // replaces the pid_str at the index where $$ started
      memcpy(&exp_input[k], pid_str, pid_length);
      j = j + 2;
      k = k + pid_length;
    } else {
      // otherwise copy the values from old input into new input and increment both indices to move along strings
        exp_input[k] = input[j];
        j++;
        k++;
    }
  }

  // replacing new line character with null terminator
  exp_input[k] = '\0';
  
  return exp_input;
}

struct commandline parse_input(char* line_input) {
  /* This function takes the string line_input from the user and parses the input
   into the struct commandline cdline */
 
  // declaring struct
  struct commandline cdline;
  // initalizing each part of the struct 
  cdline = clear_cdline(cdline);

  // first argument will be the command for execvp formatting, so we initalizing the index for arguments
  // array to 1
  int arg_count = 1;
  // declaring a buffer to store each token from strtok
  char* buffer;
  // calculating length of input so we can access the end char
  int line_input_length = strlen(line_input);

  // using strtok to parse line with a space as a deliminator 
  buffer = strtok(line_input, " ");
  // storing first string as the command and first arguement
  cdline.command = buffer;
  cdline.arguments[0] = buffer;

  buffer = strtok(NULL, " ");
  //continuing to call strtok to create tokens until the end of the string 
  while (buffer != NULL) {
    // adding pointer to input file
    if (strcmp(buffer, "<") == 0) {
      buffer = strtok(NULL, " ");
      cdline.input = buffer;
    // adding pointer to output file
    } else if (strcmp(buffer, ">") == 0) {
        buffer = strtok(NULL, " ");
        cdline.output = buffer;
    // if user entered & as the last character in the string, this indicates that we should run a background process
    } else if ((strcmp(buffer, "&") == 0) && (line_input[line_input_length - 1] == '&')) {
        cdline.background = 1;
    } else {
    // otherwise we assume the next token is an argument
        cdline.arguments[arg_count] = buffer;  
        arg_count++;
    }

      buffer = strtok(NULL, " ");            
  }

  return cdline;

}

struct commandline clear_cdline(struct commandline cdline) {
/* this function initalizes all parts of the struct for new commandline input
 takes a struct commandline as an argument and returns the cleared struct*/

  cdline.command = NULL;

  for (int i = 0; i < 512; i++) {
    cdline.arguments[i] = NULL;
  }
       
  cdline.input = NULL;
  cdline.output = NULL;
  cdline.background = 0;

  return cdline;

}

void fg_mode(int signo) {
/* Signal Handler for SIGTSTP, takes the signal as a parameter and does not return anything
 If foreground mode is on, it turns off foreground mode and vice versa, if foreground mode is
 off, it turns foreground mode back on*/

    char* message_in = "Foreground mode on\n: ";
    char* message_out = "Foreground mode off\n: ";
    
    // fg_only of 0 indicates the foreground mode is currently off 
   if (fg_only == 0) {
     write(STDOUT_FILENO, message_in, 22);
     fflush(stdout);
     fg_only = 1;
   // otherwise foreground mode is currently set to 1 indicating it is currently on
   } else {
     write(STDOUT_FILENO, message_out, 23);
     fflush(stdout);
     fg_only = 0;
   }
}

    

