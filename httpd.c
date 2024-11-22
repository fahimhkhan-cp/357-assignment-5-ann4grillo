#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <sysexits.h>

#define PORT 1032

void sigchild_handler(int signo){

   int status;
   pid_t pid;

   while((pid = waitpid(-1, &status, WNOHANG)) > 0){
      printf("child process %d terminated\n", pid);
   }
}

void handle_request(int nfd)
{
   char tmpname[1024];
   char *line = NULL;
   size_t size = 0;
   ssize_t num;

   sprintf(tmpname, "/tmp/anna-tmp-%d", getpid());
   FILE *network = fdopen(nfd, "a+");

   if (network == NULL)
   {
      perror("fdopen");
      close(nfd);
      return;
   }
  
   while ((num = getline(&line, &size, network)) >= 0)
   {
      char *token = strtok(line, " ");
      //printf("token:%s:\n", token);
      if (strncmp(token, "GET", 3) == 0 || strncmp(token, "HEAD", 4) == 0){
         printf("found GET or HEAD \n");

         char *path = strtok(NULL, " ");
         char *version = strtok(NULL, " ");

         if (path == NULL || version == NULL){
            fprintf(network, "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\n\r\n<pre>Bad Request</pre>\r\n");
            break;
         }
         printf("path:%s\n", path);
         char *ret = strstr(path, "..");

         if (ret){
            fprintf(network, "HTTP/1.0 403 Permission Denied\r\nContent-Type: text/html\r\n\r\n<pre>Permission Denied</pre>\r\n");
            break;
         }
         if (strncmp(path, "/cgi-like/", 10) == 0){
            char *program = NULL;
            char *args[1024];
            int argcount = 0;

            if (strstr(path, "?")){
               
               program = strtok(path+10, "?");
               printf("program: %s\n", program);

               
               char *start = strstr(path, "?");
               args[argcount++] = program;
               char *arg = strtok(start, "&");
               
               if (arg == NULL){
                  arg = strtok(start, " ");
               }
               printf("arg1: %s", arg);
               
               args[argcount++] = arg;
               while(arg && argcount < 1023){
                  arg = strtok(NULL, "&");
                  if (arg == NULL){
                     break;
                  }
                  printf("arg: %s\n", arg);
                  args[argcount++] = arg;
                  
               }
               args[argcount] = NULL;
            
               free(start);
            }else{
               program = strtok(path+10, " ");
               printf("program no args: %s\n", program);
               args[0] = program;
               args[1] = NULL;
            }
            char newpath[200];
            newpath[0] = '\0';
            //getcwd(newpath, sizeof(newpath));
            char *cgi = "./cgi-like/";
            strcat(newpath, cgi);
            strcat(newpath, args[0]);
            printf("newpath: %s\n", newpath);
            //args[0] = newpath;
            
            FILE *tmp = fopen(tmpname, "w+");
            if (tmp == NULL){
               perror("tmpfile");
               fprintf(network, "HTTP/1.0 500 Internal Error\r\nContent-Type: text/html\r\n");
               fclose(network);
               free(line);
               exit(1);
            }
            printf("tmp file created\n");
            
            signal(SIGCHLD, SIG_DFL);
            
            pid_t pid = fork();
            if (pid == 0){
               printf("newpath name: %s\n args: %s\n", newpath, args[0]);
               dup2(fileno(tmp), STDOUT_FILENO);
               dup2(fileno(tmp), STDERR_FILENO);
               
               execvp(newpath, args);
               //perror("exec");
               fprintf(network, "HTTP/1.0 404 cgi Not Found\r\nContent-Type: text/html\r\n\r\n<pre>Not Found</pre>\r\n");
               fprintf(network, "\ttest\n");
               fclose(network);
               fclose(tmp);
               free(line);
               exit(EX_UNAVAILABLE);
               

            }else if(pid > 0){
               int status;
               
               //printf("sleeping...\n");
               //sleep(10000);

               waitpid(pid, &status, 0);
               struct stat mybuf;
               int size = 0;

               if (lstat(tmpname, &mybuf) != -1){
                  size = mybuf.st_size;
               }

               printf("\texit status %d\t and base status: %d\n", WEXITSTATUS(status), EX__BASE);
      
               if (WEXITSTATUS(status) == EX__BASE || WEXITSTATUS(status) == 0){
                  fprintf(network, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n", (int)size);
                  char buffer[1024];
                  //buffer[1023] = '\0';
                  ssize_t n;
                  fclose(tmp);
                  tmp = fopen(tmpname, "r");
                  while(!feof(tmp)){

                     n = fread(buffer, sizeof(char), sizeof(buffer), tmp);
                     fwrite(buffer, sizeof(char), n, network);
                     
                  }
                  
               }
               fclose(tmp);
            }else{
               fprintf(network, "HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/html\r\n\r\n<pre>Internal Server Error</pre>\r\n");
            }
            unlink(tmpname);
            fclose(network);
            free(line);
            exit(1);
         }
         char b[1024];
	      getcwd(b, sizeof(b));
         strcat(b, path);
         printf("path on disk == %s\n", b);
         FILE *fp = fopen(b, "r");
         if (fp == NULL){
            printf("failed to open %s\n", b);
            fprintf(network, "HTTP/1.0 404 Not Found %s\r\nContent-Type: text/html\r\n\r\n<pre>Not Found</pre>\r\n", b);
            fclose(network);
            free(line);
            exit(1);

         } else {
            struct stat mybuf;
            int size = 0;

            if (lstat(b, &mybuf) != -1){
               size = mybuf.st_size;
            }
            
            fprintf(network, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", (int)size);
           
            if (strncmp(token, "GET", 3) == 0){
               char buffer[1024];
               //buffer[1023] = '\0';
               ssize_t n;

               while(!feof(fp)){

                  //printf("entered while\n");
                  n = fread(buffer, sizeof(char), sizeof(buffer), fp);
                  //printf("n:%d\n", (int) n);
                  fwrite(buffer, sizeof(char), n, network);
                  
               }
               
            }

            
         }
         fclose(fp);
      } else {
         fprintf(network, "HTTP/1.0 501 Not Implemented\r\nContent-Type: text/html\r\n\r\n<pre>Not Implemented</pre>\r\n");
      }
      printf("writing back to cilent\n");
      
   }
   fclose(network);
   free(line);
   exit(1);
}

void run_service(int fd)
{
   while (1)
   {
      int nfd = accept_connection(fd);
      if (nfd != -1)
      {
         printf("Connection established\n");

         pid_t pid = fork();
         if (pid == 0){
            printf("in child\n");
            close(fd);
            handle_request(nfd);
            printf("Connection closed\n");
            close(nfd);
            exit(0);

         }else if (pid > 0){
            printf("still in parent\n");
            close(nfd);  
         }else{
            
            perror("fork");
            close(nfd);
         }
         
         
      }
   }
}

int main(int argc, char *argv[])
{
   int port = PORT;
   printf("%s\n", argv[1]);
   if (argc > 1){
      port = atoi(argv[1]);
   }

   signal(SIGCHLD, sigchild_handler);

   int fd = create_service(port);

   if (fd == -1)
   {
      perror(0);
      exit(1);
   }

   printf("listening on port: %d\n", port);
   run_service(fd);
   close(fd);

   return 0;
}
