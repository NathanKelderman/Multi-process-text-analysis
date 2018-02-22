/**************************************************************
 * 
 * Nathan Kelderman
 * Professor Wolffe
 * CIS 452 Project 2: Multi-Process Text Analysis 
 * 
 **************************************************************/

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/select.h>
#include <signal.h>

#define READ 0
#define WRITE 1

#define MAX_NUM_FILES 10
#define MAX_FILENAME_LENGTH 500
#define MAX_KEYWORD_LENGTH 152
#define QUIT_COMMAND "$$$"

// struct to pass the results back to parent through pipe
typedef struct results {
  int pid;
  int occurences;
  char keyword[MAX_KEYWORD_LENGTH];
  char filename[MAX_FILENAME_LENGTH];
} results;

// searches the given filename for any occurences of the keyword and returns the count
int search(char *filename, char *keyword) {
  FILE *fp = fopen(filename, "r");

  if (fp == NULL) {
    return -1;
  }
  
  printf("Child %d searching file %s for keyword %s\n", getpid(), filename, keyword);

  int occurences = 0;
  char buf[100];
  while(fscanf(fp, "%s", buf) == 1) {
    if(strcmp(buf, keyword) == 0) {
      occurences++;
    }
  }
  fclose(fp);
  printf("File: %s Occurences: %d\n", filename, occurences);
  return occurences;
}

// global variables for access in interrupt handler function
int pipeToChild[2];
int pipeToParent[2];
char filename[MAX_FILENAME_LENGTH];

// each process kills itself and connecting pipes
void interruptHandler( int signum) {
  if( strcmp(filename, "PARENT") == 0) {
    close( pipeToParent[READ]);
    close( pipeToChild[WRITE]);
    close( pipeToParent[WRITE]);
    close( pipeToChild[READ]);
    printf("Shutting down parent and its connections to the pipes\n");
  } else {
    printf("Shutting down child %d and closing its connections to the pipes\n", getpid());
    close( pipeToParent[WRITE]);
    close( STDIN_FILENO);
    close( STDOUT_FILENO);
  }
  exit(0);
}

int main(int argc, char* argv[]) 
{
  pid_t pid;
  int id = getpid();
  strcpy(filename, "PARENT");

  printf("Parent id: %d\n", id);

  // create pipe from parent to children
  if( pipe(pipeToChild) < 0) {
    perror("Error creating pipe to child");
    exit(1);
  }
  printf("Pipe to future children created.\n");
 
  // create pipe from children to parent
  if( pipe(pipeToParent) < 0) {
    perror("Error creating pipe to parent");
    exit(1);
  }
  printf("Pipe from future children created.\n");
  
  // create handle interrupts
  signal(SIGINT, interruptHandler);

  // creates a set of file descriptors and set it to zero
  fd_set pipes;
  FD_ZERO(&pipes);

  printf("Enter the files one at a time (max 10) or $$$ to stop entering files.\n");
  
  int x;
  int num_of_children=0;
  // loop until hits MAX_NUM_FILES or user notifies its done entering files
  // creates chirldren and gives them the entered filename
  for(x = 0; x < MAX_NUM_FILES; x++) {
    printf("Enter file %d\n", x+1);

    // copy in the input and remove the newline
    char file[MAX_FILENAME_LENGTH];
    fgets(file, MAX_FILENAME_LENGTH, stdin);
    file[strcspn(file, "\n")] = 0;

    // end loop if input was QUIT_COMMAND
    if (strcmp(file, QUIT_COMMAND) == 0) break;
    
    // create a child
    if((pid = fork()) < 0) {
      perror ("Fork failed");
      kill(id, SIGINT);
    } else if(!pid) {
      // store childs pid
      id = getpid();
      strcpy(filename, file);

      printf("Child process created with id: %d and and was given filename: %s\n", id, filename);

      // create pipes to comminicate with parent
      dup2( pipeToChild[READ], STDIN_FILENO);
      
      close( pipeToParent[READ]);
      close( pipeToChild[READ]);
      close( pipeToChild[WRITE]);

      // add file descriptor to the set 
      FD_SET(STDIN_FILENO, &pipes);
      break;
    } else if (pid > 0) {
      // keep track of how many children there are
      num_of_children++;
    }
  }
 
  if( pid > 0) {
    
    // if no children were spawned and therefore no files entered, notify user and kill myself
    if( num_of_children < 1) {
      printf("No files entered\n");
      kill(id, SIGINT);
    }
    // set up parent pipes
    close( pipeToParent[WRITE]);
    close( pipeToChild[READ]);
    printf("\nNumber of children: %d\n", num_of_children);
    
    // add STDIN and pipeToParent[READ] to set of file descriptors
    FD_SET(STDIN_FILENO, &pipes);
    FD_SET(pipeToParent[READ], &pipes);
    
    int total = 0;
    int childrenCount = 0;

    // asks for keyword and keeps checking the pipes for any input
    printf("Enter a keyword: \n\n");
    while(1) {
      // temp fd set to manipulate
      fd_set tmp_set = pipes;
      int z;
      // check if there is any data in any of the pipes
      select(FD_SETSIZE, &tmp_set, NULL, NULL, NULL);
      for(z=0; z < FD_SETSIZE; z++) {
        // if fd 'z' is part of the set and contains data
        if( FD_ISSET(z, &tmp_set)) {
          // if z is the fd for stdin then take the input from the keyboard and send it
          // to the children as the next keyword
          if( z == STDIN_FILENO ) {
            char keyword[MAX_KEYWORD_LENGTH];
            fgets(keyword, MAX_KEYWORD_LENGTH, stdin);
            keyword[strcspn(keyword, "\n")] = 0;
            int children;
            // send the keyword to the pipe for as many children as there are
            for(children = 0; children < num_of_children; children++ ) {
              write(pipeToChild[WRITE], keyword, sizeof(keyword));
            }
            // ask for another keyword
            printf("Enter another keyword when ready: \n\n");
          }
          // if z is the fd for the pipe coming from the children then read the results
          // into a results struct and print the data
          if( z == pipeToParent[READ] ) {
            results r;
            read(pipeToParent[READ], &r, sizeof(results));
            // if occurences is < 0 then remove 1 from num_of_children because
            // child will kill itself
            if (r.occurences < 0) {
              num_of_children--;
              if (num_of_children < 1) {
                printf("All the children are dead\n"); 
                kill(id, SIGINT);
              }
            } else { 
              total += r.occurences;
              childrenCount++;
              printf("Child %d reading %s found %d occurences of \"%s\"\n", r.pid, r.filename, r.occurences, r.keyword);
            }
            
            // keep track of how many children send results, if all children did then 
            // print the total occurences of the keyword
            if( childrenCount == num_of_children) {
              printf("Total number of occurences of \"%s\" are %d\n", r.keyword, total);
              total = 0;
              childrenCount = 0;
            }
          }
        }
      }
    }
  }
  
  if(!pid) {
    // each child keeps looping and checking the pipe from the parent for data
    while(1) {
      fd_set tmp_set = pipes;
      int z;
      select(FD_SETSIZE, &tmp_set, NULL, NULL, NULL);
      for(z=0; z < FD_SETSIZE; z++) {
        if( FD_ISSET(z, &tmp_set)) {
          // if there is data on the pipe from the parent then read it and send it
          // as the keyword and the filename it was given to the search function
          if( z == STDIN_FILENO ) {
            char keyword[MAX_KEYWORD_LENGTH];
            read(STDIN_FILENO, keyword, sizeof(keyword));
            printf("Child %d recieved keyword %s\n", id, keyword);
            int occurences = search(filename, keyword);
            results r;
            r.pid = id;
            r.occurences = occurences;
            strcpy(r.filename, filename);
            strcpy(r.keyword, keyword);
            // send the struct of results and child information to the parent
            write(pipeToParent[WRITE], (void*) &r, sizeof(results));
            
            // if search returned an error, print the problem and still send the results
            // of 0 occurences to the parent and then kill itself
            if( occurences < 0) {
              printf("File \"%s\" could not be opened by child %d\n", filename, id);
              kill(id, SIGINT);
            }
          }
        }
      }
    }
  }
  return 0;
}
