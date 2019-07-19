/***************************************************************************
 *   Copyright (C) 2018 Microsemi Corporation                              *
 *   soc_tech@microsemi.com                                                *
 ***************************************************************************/


//#define MICROSEMI_SOCKET_CLIENT_VERBOSE 1

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef __MINGW32__
#include <winsock2.h>
#include <windows.h>
#else
#include <signal.h>
#include <stdlib.h>
#endif

#include "microsemi_socket_client.h"
#include "microsemi_timeout.h"

#ifndef __MINGW32__
#include <wordexp.h>   // command argument parser is needed only for linux
#endif

bool                microsemi_socket_connected                                           = false;
int                 microsemi_socket_port                                                = 3334;
char                microsemi_socket_ip[MICROSEMI_IP_STRING_LEN]                         = "127.0.0.1";
struct sockaddr_in  microsemi_server;
char                microsemi_client_path[MICROSEMI_SERVER_PATH_STRING_LEN]              = "";
char                microsemi_current_path[MICROSEMI_CURRENT_PATH]                       = "";
char                microsemi_server_absolute_basepath[MICROSEMI_SERVER_PATH_STRING_LEN] = "";
char                microsemi_server_binary[MICROSEMI_SERVER_PATH_STRING_LEN]            = "./";
char                microsemi_server_reply[MICROSEMI_MAX_SOCKET_BUFFER_SIZE];

#ifdef FP_SERVER_SIDE
// Standalone is more convient and has the extra features enabled by default
bool                microsemi_server_autostart                                           = true;
bool                microsemi_server_autokill                                            = true;
#else
// OpenOCD variant is more conservative/safer and extra features have to be enabled
bool                microsemi_server_autostart                                           = true; // should be false but wasted too much time getting confused when I forgot about cfg files
bool                microsemi_server_autokill                                            = true; // should be false but wasted too much time getting confused when I forgot about cfg files
#endif

#ifdef __MINGW32__
// On Windows when it's standalone and whent it's part of OpenOCD, splitter below expect the / delimiter for this path no matter of OS
#ifdef FP_SERVER_SIDE
char                microsemi_server_path[MICROSEMI_SERVER_PATH_STRING_LEN]              = "fpServer.exe";
#else
char                microsemi_server_path[MICROSEMI_SERVER_PATH_STRING_LEN]              = "../../fpServer/bin/fpServer.exe";
#endif

PROCESS_INFORMATION microsemi_server_process_info;
#else

// On Linux when it's standalone and whent it's part of OpenOCD
#ifdef FP_SERVER_SIDE
char                microsemi_server_path[MICROSEMI_SERVER_PATH_STRING_LEN]              = "./fpServer";
#else
char                microsemi_server_path[MICROSEMI_SERVER_PATH_STRING_LEN]              = "../../fpServer/bin/fpServer";
#endif

// i don't want to have it static because it will change make it unique for each include microsemi_server_pid
pid_t               microsemi_server_pid                                                 = 0;
#endif

#ifdef __MINGW32__
HANDLE hThreadTimeoutWatchdog;
#endif


void sleep_portable(int miliseconds) {
#ifdef __MINGW32__
  Sleep(miliseconds);
#else
  usleep(miliseconds *1000);
#endif
}


#ifdef  __MINGW32__
struct timeval watchdog_timeout = {0,0};

// will set the watchdog to expire "timeout_ms" ms into the future
void watchdog_set_timeout(int timeout_ms) {
  unsigned long microseconds = (timeout_ms % 1000) * 1000;
  unsigned int  seconds      = timeout_ms / 1000;

  gettimeofday(&watchdog_timeout, NULL);

//  printf("fpClient: Current time %ld.%06ld and adding %d ms \n",
//         (long long)watchdog_timeout.tv_sec, watchdog_timeout.tv_usec, timeout_ms);

  watchdog_timeout.tv_usec = (watchdog_timeout.tv_usec + microseconds) % 1000000L;
  if (watchdog_timeout.tv_usec < microseconds) watchdog_timeout.tv_sec++;
  watchdog_timeout.tv_sec  += seconds;

//  printf("fpClient: Now the watchdog is set to %ld.%06ld \n",
//         (long long)watchdog_timeout.tv_sec, watchdog_timeout.tv_usec);

}


// will disable the watchdog so it will not expire
void watchdog_disable_timeout() {
  watchdog_timeout.tv_sec = 0;
}


// will compare two timestamps
int is_left_timestamp_before_right(struct timeval *left, struct timeval *right) {
  if (left->tv_sec == right->tv_sec) {
    // if the seconds are the same, decide upon seconds
    return left->tv_usec < right->tv_usec;
  }
  else {
    // if the seconds are different, then decide upon seconds which one was first
    return left->tv_sec < right->tv_sec;
  }
}


// Windows and Linux watchdogs are implemented differently, on Windows the watchdog is a thread which sleeps
// in a loop and checks the current time against global variable if the whole process should be killed.
// The watchdog itself should check for itself if it was disabled and not calculate
unsigned __stdcall client_api_call_timeout_windows(void *ArgList) {
  struct timeval current_time;

  while (1) {
    sleep_portable(100);

    // TODO this is not very thread safe
//    printf("fpClient: Waited 100ms, current time %ld.%06ld and watchdog_timeout %ld.%06ld \n",
//           (long long)current_time.tv_sec, current_time.tv_usec,
//           (long long)watchdog_timeout.tv_sec, watchdog_timeout.tv_usec);

    if (watchdog_timeout.tv_sec!= 0) {
      // if watchdog is enabled then check if it's expired
      gettimeofday(&current_time, NULL);

      if (is_left_timestamp_before_right(&watchdog_timeout, &current_time)) {
        fprintf(stderr, "fpClient: watchdog timeout on API call, exiting.\n");
        exit(1);
      }
    }
  }
}
#else

// On Linux the watchdog is just simple callback assigned to signal and there is alarm, timeout set to send
// SIG_ALARM signal when that happens, while disabling the alarm even if it was processed in-time, means that the Linux
// watchdog.
void client_api_call_timeout_linux(int ignore_this_argument) {
  fprintf(stderr, "fpClient: watchdog timeout on API call, exiting\n");
  exit(1);
}
#endif


void detect_client_path() {
#ifdef __MINGW32__
  // Windows implementation to get actual full binary path no matter what current directory is
  strcpy(microsemi_client_path, _pgmptr);
#else
  // This might not work under some OS's like FreeBSD
  readlink("/proc/self/exe", microsemi_client_path, sizeof(microsemi_client_path));
#endif

  getcwd(microsemi_current_path, sizeof(microsemi_current_path));

  //printf("fpClient: Microsemi fpClient path %s \n", microsemi_client_path);

  // Depending on the OS use different directory delimiters
#ifdef __MINGW32__
  const char delimiter = '\\';
#else
  const char delimiter = '/';
#endif

  // find last delimiter position to cut off the binary name and leave just the base path
  char* ptr = strrchr(microsemi_client_path, delimiter);
  if (ptr == NULL) {
    strcpy(microsemi_client_path, "");
  }
  else {
    // include the delimiter itself but then terminate the string with 0 in es
    *(++ptr) = 0;
  }

  //printf("fpClient: Microsemi fpClient path without binary %s \n", microsemi_client_path);
}


void change_directory(char *path) {
#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
  printf("fpClient: changing dir to %s\n", path);
#endif

  chdir(path);
}


void get_fpServer_absolute_path() {
  char server_basepath[MICROSEMI_SERVER_PATH_STRING_LEN] = "";

  // TODO review this, I might have included a bracket somewhere where it's redundant, or maybe do not add bracked in the end of the paths
  // and it might cause problem in some rare cases, or detect the Windows brackets (windows can use both / and \ under certain conditions)
  // so this might be a problematic, but I'm not going to do it now, sorry to my future me.

  // In simple terms: Because there are problems, the current directory might not be the client's path and we reference the server relative to the client
  // We figured out where the client binary is and ripped the path from it (in detect_client_path() call)
  // We know the relative path to the server relative to client, so rip the path from that too and save the binary part as well
  // Then we fabricate new ABSOLUTE path where the server is located so we can chdir directly to it for a moment
  // execute the server binary directly inside its directory
  // and return back to original directory where OpenOCD was previously so everything will work without any change

  char *ptr = strrchr(microsemi_server_path, '/');
  // split the relative server path which is referencing from the client           "../fpServer/fpServer.exe"
  strncat(server_basepath, microsemi_server_path, ptr - microsemi_server_path); // "../fpServer/"
  strncat(microsemi_server_binary, ptr+1, strlen(ptr)-1);                       // "./" + "fpServer.exe" = "./fpServer.exe"

  strcpy(microsemi_server_absolute_basepath, microsemi_client_path);            // "/opt/microsemi/fpClient"
  strcat(microsemi_server_absolute_basepath, "/");                              // "/opt/microsemi/fpClient/"  just in case it's not included
  strcat(microsemi_server_absolute_basepath, server_basepath);                  // "/opt/microsemi/fpClient/../fpServer/
}


void start_server() {
  detect_client_path();

  char cmdline[MICROSEMI_SERVER_PATH_STRING_LEN + MICROSEMI_SERVER_PATH_STRING_LEN + 16]; // allow to fit a full sized path + some extra parameters
  get_fpServer_absolute_path();

  // tell server to have only 12 idle second timeout
  sprintf(cmdline, "%s -p %d -o %d", microsemi_server_binary, microsemi_socket_port, microsemi_serveridle_timeout_when_autostarted);


#ifdef __MINGW32__
  STARTUPINFO startupinfo;

  memset(&startupinfo, 0, sizeof(startupinfo));
  startupinfo.cb = sizeof(startupinfo);

  memset(&microsemi_server_process_info, 0, sizeof(microsemi_server_process_info));

  change_directory(microsemi_server_absolute_basepath); // go to fpServer's absolute path so the binary is exactly there

  if (!CreateProcess( NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &startupinfo, &microsemi_server_process_info)) {
    fprintf(stderr, "fpClient: failed to start fpServer process!\r\n");
    fprintf(stderr, "fpClient: used the following command: %s\r\n", cmdline);

    microsemi_client_settings();
    exit(1);
  }

  SetPriorityClass(microsemi_server_process_info.hProcess, ABOVE_NORMAL_PRIORITY_CLASS); // elevate the priority slightly as the cypress is bit timing finicky

  change_directory(microsemi_current_path); // go back to previous path

#else
  pid_t child_pid;

  // parse/process the arguments into a expected structure
  wordexp_t result;
  wordexp(cmdline, &result, WRDE_SHOWERR | WRDE_UNDEF);

#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
  printf("fpClient: commmand line to start %s \n", cmdline);

  for (int i =0; i<= result.we_wordc; i++) {
    printf("fpClient: parsed arguments %d = %s \n", i, result.we_wordv[i]);
  }
#endif

  // split it process to parent and child
  child_pid = fork();

  if (child_pid == 0) {
    // child will start the fpServer

    change_directory(microsemi_server_absolute_basepath); // go to fpServer's absolute path so the binary is exactly there

#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
    printf("fpClient: child %d starting fpServer\n", getpid());
    printf("fpClient: %s\n", cmdline);
#endif

    execvp(result.we_wordv[0], result.we_wordv);  // execution of the fpServer is blocking this child process

    // this point is reached only when fpServer exits for whatever reason it is.
    wordfree(&result);

    fprintf(stderr, "fpClient: process for the fpServer exited.\n");

//    microsemi_client_settings();
    change_directory(microsemi_current_path); // go back to previous path
    exit(1);
  }

  // now when we are in the parent we can set the child's pid, there were problems doing to globally straight away
  microsemi_server_pid = child_pid;
#endif
}


int microsemi_socket_connect() {

  if (microsemi_socket_connected) {
#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
    printf("fpClient: already connected to the fpServer, any subsequent connect calls are ignored\n");
#endif
    return 0;
  }

  if (microsemi_server_autostart) {
    start_server();
  }

#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
  printf("fpClient: trying to connect to the fpServer port %d using API %d.\n", microsemi_socket_port, MICROSEMI_API_CALLS_VERSION);
#endif

#ifdef __MINGW32__
  int iResult;
  WSADATA wsaData;

  iResult = WSAStartup(MAKEWORD(2,2), &wsaData);

  if (iResult !=0) {
    fprintf(stderr, "fpClient: WSAStartup failed with error: %d \n", iResult);
    return 1;
  }
#endif

  //Create socket
  connectSocket = socket(AF_INET , SOCK_STREAM , 0);

#ifdef __MINGW32__
  if (connectSocket == SOCKET_ERROR) {
    fprintf(stderr, "fpClient: could not create socket, error %d \n",WSAGetLastError());
    return 1;
  }
#else
  if (connectSocket == -1)
  {
    fprintf(stderr, "fpClient: could not create a socket.\n");
    return 1;
  }
#endif

#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
  puts("fpClient socket created\n");
#endif

  microsemi_server.sin_addr.s_addr = inet_addr(microsemi_socket_ip);
  microsemi_server.sin_family      = AF_INET;
  microsemi_server.sin_port        = htons(microsemi_socket_port);


  // Connect to remote server

  if (microsemi_server_autostart) {
    // if autostart is used give it few attempts and wait a bit to get the server running

    const int retry_limit = 10;
    int count = 0;

    // retry 10x with 150ms delays => 1.5 seconds
    do {
      count++;
      sleep_portable(150);
    } while (connect(connectSocket, (struct sockaddr *)&microsemi_server, sizeof(microsemi_server)) < 0 && count < retry_limit);

#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
    printf("fpClient: connected to the fpServer, successfully in %d attempts\n", count);
#endif

    if (count >= retry_limit) {
      // only fail if the retry-limit was reached

      fprintf(stderr, "fpClient: connection to the fpServer failed after multiple retries.\n");
      // microsemi_client_settings();
      return 1;
    }

    sleep_portable(100); // after successful connection wait bit anyway

  }
  else {
    // if autostart is not used, then it should be running on the very first attempt

    if (connect(connectSocket, (struct sockaddr *)&microsemi_server, sizeof(microsemi_server)) < 0) {
      fprintf(stderr, "fpClient: connect() failed.\n");
      return 1;
    }
  }

  // setting up watchdogs for both win/linux
#ifdef __MINGW32__
  unsigned int ThreadId;

  hThreadTimeoutWatchdog = (HANDLE) _beginthreadex(NULL, 0, client_api_call_timeout_windows, NULL, 0, &ThreadId);
#else
  signal(SIGALRM, client_api_call_timeout_linux);
#endif


//  printf("fpClient v%d established connection to fpServer using API v%d to %s:%d \n",
//         MICROSEMI_FPSERVER_VERSION,
//         MICROSEMI_API_CALLS_VERSION,
//         microsemi_socket_ip,
//         microsemi_socket_port);

  microsemi_socket_connected = true;

  return 0;
}


int microsemi_socket_close() {
#ifdef __MINGW32__
  closesocket(connectSocket);
  WSACleanup();

  CloseHandle(microsemi_server_process_info.hThread);
  CloseHandle(microsemi_server_process_info.hProcess);
  CloseHandle(hThreadTimeoutWatchdog);
#else
  close(connectSocket);

  if (microsemi_server_pid) {
    kill(microsemi_server_pid, SIGTERM);
  }
#endif
  return 0;
}


void* microsemi_socket_send_unprotected(binn *request, binn **response) {
  int recieved_data_size = 0;
  int iResult;

#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
  printf("fpClient: going to send data to a fpServer socket, connected=%d\n", microsemi_socket_connected);
#endif

  if (!microsemi_socket_connected) {
    microsemi_socket_connect();
  }

  iResult = send(connectSocket, binn_ptr(request), binn_size(request), 0);
  binn_free(request);
  if( iResult < 0) {
    // for SOCKET_ERROR == -1 for both platforms

#ifdef __MINGW32__
    printf("fpClient: send() to fpServer API failed, error %d\n", WSAGetLastError());
#else
    puts("fpClient: send() to the fpServer API failed.\n");
#endif

    // microsemi_client_settings();
    exit(1);  // be aggressive to errors
  }

  if( (recieved_data_size = recv(connectSocket, microsemi_server_reply, MICROSEMI_MAX_SOCKET_BUFFER_SIZE, 0)) < 0) {


#ifdef __MINGW32__
    fprintf(stderr, "fpClient: recv() function from fpServer API failed, error %d\n", WSAGetLastError());
#else
    fprintf(stderr, "fpClient: recv() function from fpServer API failed.\n");
#endif

    // microsemi_client_settings();
    exit(1);  // be aggressive to errors
  }
  *response = binn_open(microsemi_server_reply);

#ifdef MICROSEMI_SOCKET_CLIENT_VERBOSE
  printf("fpClient: got %d bytes data back, first word of the payload: %04x \n", recieved_data_size, *(int *)binn_ptr(response));
#endif
}


int microsemi_socket_send(binn *request, binn **response, microsemi_fp_request timeout_type) {

  if (timeout_type == -1 || microsemi_fp_request_timeout_weights[timeout_type] == 0 || microsemi_client_timeout == 0) {
    // invoke api call without any timeout watchdog when either:
    // call invoked by -1 type
    // or the weight for that call type is 0
    // or global microsemi_client_timeout multiplier is set to 0
    microsemi_socket_send_unprotected(request, response);
    return 0;
  }
  else {
    // figure out how much time the call should take at maximum to execute
    unsigned int timeout_ms = (unsigned int)microsemi_fp_request_timeout_weights[timeout_type] * microsemi_client_timeout;
    if (!microsemi_socket_connected) {
      // if we are not connected to the server yet, it means we will have to autostart it and that will need extra time
//      printf("fpClient: Allowing extra time %d for the fpServer to start up\n", microsemi_serverautostart_timeout);
      timeout_ms += microsemi_serverautostart_timeout;
    }

//    printf("fpClient: Command type %d, multiplier for timeout %d, waiting for %d \n", (int)timeout_type, microsemi_fp_request_timeout_weights[timeout_type], timeout_ms);

    unsigned int timeout_s = (timeout_ms+500) / 1000;  // round up
    if (timeout_s == 0) {
      // even after division the result is 0, still wait 1s
      timeout_s = 1;
    }

#ifdef __MINGW32__
    watchdog_set_timeout(timeout_ms);
    microsemi_socket_send_unprotected(request, response);
    watchdog_disable_timeout();
#else
    alarm(timeout_s);                               // setup the watchdog to exit everything on watchdog_timeout
//    printf("sending request\n");
    microsemi_socket_send_unprotected(request, response);
//    printf("request responded\n");
    alarm(0);                                       // call finished so remove the watchdog
#endif

    return 0;
  }

}


int microsemi_socket_set_ipv4(const char *ip) {
  if (strlen(ip) >= MICROSEMI_IP_STRING_LEN) {
    return 1;
  }

  strcpy(microsemi_socket_ip, ip);
  return 0;
}


int microsemi_socket_set_port(int port) {
  if (port<0 || port >65535) {
    return 1;
  }

  microsemi_socket_port = port;
}


int microsemi_socket_set_server_path(const char *path) {
  if (strlen(path) >= MICROSEMI_SERVER_PATH_STRING_LEN) {
    return 1;
  }

  strcpy(microsemi_server_path, path);
  return 0;
}


void microsemi_server_set_autostart(bool autostart) {
  microsemi_server_set_autostart(autostart);
}


void microsemi_server_set_autokill(bool autokill) {
  microsemi_server_set_autokill(autokill);
}


void microsemi_client_settings() {
  printf("Current status of the fpClient is: \n");
  printf("socket_connected:    %d\n", microsemi_socket_connected);
  printf("socket_ip:           %s\n", microsemi_socket_ip);
  printf("socket_port:         %d\n", microsemi_socket_port);
  printf("server_autostart:    %d\n", microsemi_server_autostart);
  printf("server_autokill:     %d\n", microsemi_server_autokill);
  printf("client_dir:          %s\n", microsemi_client_path);
  printf("server_path:         %s\n", microsemi_server_path);
  printf("server_absolute_base %s\n", microsemi_server_absolute_basepath);
  printf("server_binary        %s\n", microsemi_server_binary);

  if (getcwd(microsemi_current_path, sizeof(microsemi_current_path)) != NULL) {
    printf("current_dir:         %s\n", microsemi_current_path);
  }
}