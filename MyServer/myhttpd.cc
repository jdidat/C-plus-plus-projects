const char * usage =
"                                                               \n"
"myhttpd:                                                       \n"
"                                                               \n"
"An Iterative HTTP server                                       \n"
"                                                               \n"
"   ./myhttpd [-f|-t|-p] [port]                                 \n"
"                                                               \n"
"Where 1024 < port < 65536.                                     \n"
"      -f to create new process                                 \n"
"      -t to create new thread                                  \n"
"      -p to create pool of threads                             \n"
"                                                               \n";


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

int QueueLength = 5;

// Processes request
void processRequest(int socket);
void processRequestThread(int socket);
void poolSlave(int socket);
void browseDirectories(DIR * d, int fd);
void printDirectories(int fd);

pthread_mutex_t mt;
pthread_mutexattr_t mattr;

clock_t start;

clock_t beginRequest;

int min = INT_MAX;
int max = INT_MIN;


int nRequests = 0;
int nEntries = 0;
int maxEntries = 20;
char ** fileArray;
int toggle = 1;
double diff = 0;

time_t get_mtime(const char * path) {
  struct stat statbuf;
  if (stat(path, &statbuf) == -1) {
    perror(path);
    exit(1);
  }
  return statbuf.st_mtime;
}

off_t get_size(const char * path) {
  struct stat statbuf;
  if (stat(path, &statbuf) == -1) {
    perror(path);
    exit(1);
  }
  return statbuf.st_size;
}

int compareName (const void * f1, const void * f2) {
  const char * str1 = *(const char **) f1;
  const char * str2 = *(const char **) f2;
  return (strcmp(str1, str2) * toggle);
}

int compareMTime (const void * f1, const void * f2) {
  int f = get_mtime((const char *) f1);
  int s = get_mtime((const char *) f2);
  return (int) ((f - s) * toggle);
}

int compareSize (const void * f1, const void * f2) {
  int f = get_size((const char *) f1);
  int s = get_size((const char *) f2);
  return (int) ((f - s) * toggle);
}

extern "C" void killzombie(int sig);

int main(int argc, char ** argv) {
  // Print usage if not enough arguments
  int port = 0;
  int processFlag = 0;
  int threadFlag = 0;
  int poolFlag = 0;
  if (argc > 3) {
    fprintf( stderr, "%s", usage );
    exit(-1);
  }
  if (argc == 1) {
    port = 2017;
  }
  if (argc > 1 && argv[1][0] != '-') {
    port = atoi(argv[1]);
  }
  if (argc > 1 && argv[1][0] == '-') {
    if (argv[1][1] == 'f') {
      processFlag = 1;
    }
    if (argv[1][1] == 't') {
      threadFlag = 1;
    }
    if (argv[1][1] == 'p') {
      poolFlag = 1;
    }
    port = atoi(argv[2]);
  }

  // Catch the zombie processes
  struct sigaction signalAction;
  signalAction.sa_handler = killzombie;
  sigemptyset(&signalAction.sa_mask);
  signalAction.sa_flags = SA_RESTART;
  int error1 = sigaction(SIGCHLD, &signalAction, NULL);
  if (error1) {
    perror("sigaction");
    exit(-1);
  }
  
  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress));
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);
  
  // Allocate a socket
  int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
  if (masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the sae port number
  int optval = 1; 
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, (char *) &optval, sizeof(int));
   
  // Bind the socket to the IP address and port
  int error = bind( masterSocket, (struct sockaddr *)&serverIPAddress, sizeof(serverIPAddress));
  if (error) {
    perror("bind");
    exit(-1);
  }
  
  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections
  error = listen(masterSocket, QueueLength);
  if (error) {
    perror("listen");
    exit(-1);
  }
  
  if (poolFlag) {
    pthread_mutexattr_init(&mattr);
    pthread_mutex_init(&mt, &mattr); 
    pthread_t tid[5];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); 
    for(int i = 0; i < 5; i++) {
      pthread_create(&tid[i], &attr, (void * (*)(void *)) poolSlave, (void *) masterSocket);
    }
    pthread_join(tid[0], NULL);
  }
  else {
    start = clock();
    nRequests = 0;
    while (1) {
      struct sockaddr_in clientIPAddress;
      int alen = sizeof( clientIPAddress );
      int slaveSocket = accept(masterSocket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
      if (slaveSocket < 0) {
        perror( "accept" );
        exit(-1);
      }
      else if (processFlag) {
        pid_t slave = fork();
        if (slave == 0) {
	  processRequest(slaveSocket);
	  close(slaveSocket);
	  exit(EXIT_SUCCESS);
        }
        close(slaveSocket);
      }
      else if (threadFlag) {
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        pthread_create(&tid, &attr, (void * (*)(void *)) processRequestThread, (void *) slaveSocket);
      }
      else {
        processRequest(slaveSocket);
        close(slaveSocket);
      }
    }
  }
}

void processRequestThread(int socket) {
  processRequest(socket);
  close(socket);
}

void poolSlave(int socket) {
  while(1) {
    pthread_mutex_lock(&mt);
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
    int slaveSocket = accept(socket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
    pthread_mutex_unlock(&mt);
    if (slaveSocket < 0 && errno == EINTR) {
      continue;
    }
    if (slaveSocket < 0) { 
      perror("accept");
      exit(-1);
    }
    processRequest(slaveSocket);
    close(slaveSocket);
  }
}

extern "C" void killzombie(int sig) {
  int pid = wait3(0, 0, NULL);
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

void processRequest(int fd) {
  // Buffer used to store the name received from the client
  int length = 0;
  int n;
  unsigned char newChar;
  unsigned char oldChar = 0;
  int gotGet = 0;
  int buffSize = 4024;
  char curr_string[buffSize + 1];
  char docpath[buffSize + 1] = {0};
  // The client should send <name><cr><lf>
  // Read the name of the client character by character until a
  // <CR><LF> is found.
  
  nRequests++;
  beginRequest = clock();

  const char * secretKey = "imisstheoldkanye";

  while ((n = read(fd, &newChar, sizeof(newChar)))) {
    curr_string[length] = newChar;
    length++;
    oldChar = newChar;
    if (newChar == '\n') {
      if (curr_string[length - 2] == '\r') {
	if (curr_string[length - 3] == '\n') {
	  if (curr_string[length - 4] == '\r') {
	    break;
	  }
	}
      }
    }
  }
  
  char * copy = strdup(curr_string);
  char * temp = strchr(copy, '/');
  temp = strchr(temp + 1, '/');
  char * end = strchr(temp, ' ');
  *end = '\0';
  char * get = strtok(curr_string, " ");
  if (strcmp(get, "GET")) {
    const char * err = "Not a request\n";
    write(fd, err, strlen(err));
    return;
  }

  printf("222\n");
  
  char * sKey = strtok(NULL, "/");
  if (strcmp(sKey, secretKey)) {
    const char * err = "Secret Key does not match, access denied\n";
    write(fd, err, strlen(err));
    return;
  }
  char * doc = strtok(NULL, "/");
  char * p = strchr(doc, ' ');
    
  if(p != NULL) {
    *p = 0;
  }

  printf("333\n");
  
  strcpy(docpath, doc);
  char buffer[2048] = "./http-root-dir/htdocs";
  strcat(buffer,temp);
  DIR * d = opendir(buffer);
  if(d != NULL && strcmp(buffer,"./http-root-dir/htdocs/") != 0) {
      browseDirectories(d, fd);
      clock_t endRequest = clock() - beginRequest;
      diff = (endRequest * 1000) / CLOCKS_PER_SEC;
      if (diff < (double) min) {
	min = diff;
      }
      if (diff > (double) max) {
	max = diff;
      }
      return;
  }

  char cwd[buffSize + 1] = {0};
  getcwd(cwd, sizeof(cwd));
  if (!strncmp(docpath, "/icons", 6)) {
    strcat(cwd, "/http-root-dir/");
    strcat(cwd, docpath);
  }
  else if (!strncmp(docpath, "/htdocs", 7)) {
    strcat(cwd, "/http-root-dir/");
    strcat(cwd, docpath);
  }
  if (strstr(temp, "/cgi-bin") != NULL) {
    int tempIn = dup(0);
    int tempOut = dup(1);
    strcat(cwd, "/http-root-dir");
    strcat(cwd, temp);
    char * docCpy = strdup(temp);
    char * question = strchr(docCpy, '?');
//    printf("%s\n",question);
    if (question == NULL) {
        question = "";
	//setenv("QUERY_STRING",question + 1,1);
    }
    else{
	question++;
    }
    write(fd, "HTTP/1.0 200", 12);
    write(fd, "\r\n", 2);
    setenv("REQUEST_METHOD", "GET", 1);
    setenv("QUERY_STRING", question, 1);
    pid_t child = fork();
    dup2(fd, 1);
    if (child == 0) {
      execvp(cwd, NULL);
      exit(1);
      waitpid(child,NULL,0);
    }
    dup2(tempIn, 0);
    dup2(tempOut, 1);
    clock_t endRequest = clock() - beginRequest;
    diff = (endRequest * 1000) / CLOCKS_PER_SEC;
    if (diff < (double) min) {
      min = diff;
    }
    if (diff > (double) max) {
      max = diff;
    }
    return;
  }

  if (strstr(temp, "/stats") != NULL) {
    printf("here\n");
    const char * serverType = "CS 252 lab5";
    write(fd, "HTTP/1.0 200", 12);
    write(fd, "\r\n", 2);
    write(fd, "Server: ", 8);
    write(fd, serverType, strlen(serverType));
    write(fd, "\r\n", 2);
    write(fd, "Content-type:", 13);
    write(fd, " ", 1);
    write(fd, "text/html", 9);
    write(fd, "\r\n", 2);
    write(fd, "\r\n", 2);
    write(fd, "<h1>", 4);
    write(fd, "Stats Page\n", 11);
    write(fd, "</h1>", 5);
    write(fd, "<ul>", 4);
    write(fd, "<li>", 4);
    write(fd, "Student(s) whom wrote the project: ", 35);
    write(fd, getenv("LOGNAME"), strlen(getenv("LOGNAME")));
    write(fd, "<br>",4);
    write(fd, "<li>", 4);
    clock_t end = clock() - start;
    double t = (end * 1000) / CLOCKS_PER_SEC;
    char buff[50];
    sprintf(buff, "%lf", t);
    write(fd, "Time the server has been up: ", 29);
    write(fd, buff, strlen(buff));
    write(fd, " ms", 3);
    write(fd, "<br>",4);
    write(fd, "<li>", 4);
    char buff2[50];
    sprintf(buff2, "%d", nRequests);
    write(fd, "Number of requests since the server started: ", 45);
    write(fd, buff2, strlen(buff2));
    write(fd, "<br>",4);
    write(fd, "<li>", 4);
    char buff3[50];
    sprintf(buff3, "%d", min);
    write(fd, "Minimum service time and the URL request that took this time: ", 62);
    write(fd, buff3, strlen(buff3));
    write(fd, " ms", 3);
    write(fd, "<br>",4);
    write(fd, "<li>", 4);
    char buff4[50];
    sprintf(buff4, "%d", max);
    write(fd, "Maximum service time and the URL request that took this time: ", 62);
    write(fd, buff4, strlen(buff4));
    write(fd, " ms", 3);
    write(fd, "</ul>", 5);
    write(fd, "<br>", 4);
    write(fd, "\r\n", 2);
    write(fd, "\r\n", 2);
    clock_t endRequest = clock() - beginRequest;
    diff = (endRequest * 1000) / CLOCKS_PER_SEC;
    if (diff < (double) min) {
      min = diff;
    }
    if (diff > (double) max) {
      max = diff;
    }
    return;
  }
  
  if (strstr(temp, "/logs") != NULL) {
    const char * serverType = "CS 252 lab5";
    write(fd, "HTTP/1.0 200", 12);
    write(fd, "\r\n", 2);
    write(fd, "Server: ", 8);
    write(fd, serverType, strlen(serverType));
    write(fd, "\r\n", 2);
    write(fd, "Content-type:", 13);
    write(fd, " ", 1);
    write(fd, "text/html", 9);
    write(fd, "\r\n", 2);
    write(fd, "\r\n", 2);
    write(fd, "<h1>", 4);
    write(fd, "Logs Page\n", 11);
    write(fd, "</h1>", 5);
    write(fd, "<ul>", 4);
    write(fd, "<li>", 4);
    write(fd, "Source host of the request: ", 28);
    write(fd, "<br>",4);
    write(fd, "<li>", 4);;
    write(fd, "Directory requested: ", 21);
    write(fd, "</ul>", 5);
    write(fd, "<br>", 4);
    write(fd, "\r\n", 2);
    write(fd, "\r\n", 2);
    clock_t endRequest = clock() - beginRequest;
    diff = (endRequest * 1000) / CLOCKS_PER_SEC;
    if (diff < (double) min) {
      min = diff;
    }
    if (diff > (double) max) {
      max = diff;
    }
    return;
  }

  if (strstr(temp, "/Filename") != NULL) {
    toggle = toggle * -1;
    qsort(fileArray, nEntries, sizeof(char *), compareName);
    printDirectories(fd);
    clock_t endRequest = clock() - beginRequest;
    diff = (endRequest * 1000) / CLOCKS_PER_SEC;
    if (diff < (double) min) {
      min = diff;
    }
    if (diff > (double) max) {
      max = diff;
    }
    return;
  }
  if (strstr(temp, "/ModTime") != NULL) {
    qsort(fileArray, nEntries, sizeof(char *), compareMTime);
    printDirectories(fd);
    clock_t endRequest = clock() - beginRequest;
    diff = (endRequest * 1000) / CLOCKS_PER_SEC;
    if (diff < (double) min) {
      min = diff;
    }
    if (diff > (double) max) {
      max = diff;
    }
    return;
  }
  if (strstr(temp, "/Size") != NULL) {
    qsort(fileArray, nEntries, sizeof(char *), compareSize);
    printDirectories(fd);
    clock_t endRequest = clock() - beginRequest;
    diff = (endRequest * 1000) / CLOCKS_PER_SEC;
    if (diff < (double) min) {
      min = diff;
    }
    if (diff > (double) max) {
      max = diff;
    }
    return;
  }
  
  else {
    if (strlen(docpath) == 0) {
      strcat(docpath, "/index.html");
    }
    strcat(cwd, "/http-root-dir/htdocs/");
    strcat(cwd, docpath);
  }
  
  printf("888\n");

  char contentType[buffSize + 1] = {0};
  if (strstr(docpath, "..")) {
    char checkPath[buffSize + 1] = {0};
    char * realPath = realpath(docpath, checkPath);
    if (realPath) {
      if (strlen(checkPath) >= strlen(cwd)) {
	strcpy(cwd, checkPath);
      }
    }
  }
    
  if (strstr(docpath, ".html") || strstr(docpath, ".html/")) {
    strcpy(contentType, "text/html");
  }
  else if (strstr(docpath, ".jpg") || strstr(docpath, ".jpg/")) {
    strcpy(contentType, "image/jpeg");
  }
  else if (strstr(docpath, ".png") || strstr(docpath, ".png/")) {
    strcpy(contentType, "image/png");
  }
  else if (strstr(docpath, ".xbm") || strstr(docpath, ".xbm/")) {
    strcpy(contentType, "image/xbm");
  }
  else if (strstr(docpath, ".svg") || strstr(docpath, ".svg/")) {
    strcpy(contentType, "image/svg+xml");
  }
  else if (strstr(docpath, ".gif") || strstr(docpath, ".gif/")) {
    strcpy(contentType, "image/gif");
  }
  else {
    strcpy(contentType, "text/plain");
  }

  printf("999\n");

  FILE * f;
  if (strstr(cwd, "image/")) {
    f = fopen(cwd, "rb");
  }
  else {
    f = fopen(cwd, "r");
  }
  if (f == NULL) {
    const char * notFound = "File not Found";
    const char * serverType = "CS 252 lab5";
    write(fd, "HTTP/1.0 404 File Not Found", 27);
    write(fd," \r\n", 2);
    write(fd, "Server: ", 8);
    write(fd, serverType, strlen(serverType));
    write(fd, "\r\n", 2);
    write(fd, "Content-type: ", 14);
    write(fd, contentType, strlen(contentType));
    write(fd, "\r\n", 2);
    write(fd, "\r\n", 2);
    write(fd, notFound, strlen(notFound)); 
  }

  //printf("101010\n");
  
  else {
    const char * serverType = "CS 252 lab5";
    write(fd, "HTTP/1.0 200", 12);
    write(fd, "\r\n", 2);
    write(fd, "Server: ", 8);
    write(fd, serverType, strlen(serverType));
    write(fd, "\r\n", 2);
    write(fd, "Content-type:", 13);
    write(fd, " ", 1);
    write(fd, contentType, strlen(contentType));
    write(fd, "\r\n", 2);
    write(fd, "\r\n", 2);
    long count = 0;
    char c;
    while (count = read(fileno(f), &c, sizeof(c))) {
      if (write(fd, &c, sizeof(c)) != count) {
        //perror("write");
      }
    }
    fclose(f);
  }
  clock_t endRequest = clock() - beginRequest;
  diff = (endRequest * 1000) / CLOCKS_PER_SEC;
  if (diff < (double) min) {
    min = diff;
  }
  if (diff > (double) max) {
    max = diff;
  }
}

void printDirectories(int fd) {
  const char * serverType = "CS 252 lab5";
  const char * contentType = "text/html";
  write(fd, "HTTP/1.0 200", 12);
  write(fd, "\r\n", 2);
  write(fd, "Server:", 7);
  write(fd, " ", 1);
  write(fd, serverType, strlen(serverType));
  write(fd, "\r\n", 2);
  write(fd, "Content-type:", 13);
  write(fd, " ", 1);
  write(fd, contentType, strlen(contentType));
  write(fd, "\r\n", 2);
  write(fd, "\r\n", 2);
  write(fd, "<h1>", 4);
  write(fd, "Directory\n", 10);
  write(fd, "</h1>", 5);
  write(fd, "Sorting:", 8);
  write(fd, "<a style=\"margin: 10px\" href=\"", 30);
  write(fd, "Filename", 8);
  write(fd, "\">", 2);
  write(fd, "Filename", 8);
  write(fd, "</a>", 4);
  write(fd, "<a style=\"margin: 10px\" href=\"", 30);
  write(fd, "ModTime", 7);
  write(fd, "\">", 2);
  write(fd, "ModTime", 7);
  write(fd, "</a>", 4);
  write(fd, "<a style=\"margin: 10px\" href=\"", 30);
  write(fd, "Size", 4);
  write(fd, "\">", 2);
  write(fd, "Size", 4);
  write(fd, "</a>", 4);
  write(fd, "<ul>", 4);
  for (int i = 0; i < nEntries; i++) {
    write(fd, "<li>", 4);
    write(fd, "<a href=\"", 9);
    write(fd, fileArray[i], strlen(fileArray[i]));
    write(fd, "\">", 2);
    write(fd, fileArray[i], strlen(fileArray[i]));
    write(fd, "</a>", 4);
    write(fd, "<br>",4);
  }
  write(fd, "</ul>", 5);
}

void browseDirectories(DIR * d, int fd) {
  if (fileArray != NULL) {
    memset(fileArray, 0, nEntries * sizeof(char *));
    nEntries = 0;
  }
  fileArray = (char **)malloc(sizeof(char *) * maxEntries);
  struct dirent * ent;
  while((ent = readdir(d)) != NULL) {
    if (nEntries == maxEntries) {
      maxEntries *= 2;
      fileArray = (char **)realloc(fileArray, maxEntries * sizeof(char *));
    }
    fileArray[nEntries] = strdup(ent->d_name);
    nEntries++;
  }
  printDirectories(fd);
}



