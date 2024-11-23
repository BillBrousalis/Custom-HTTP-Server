#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <regex.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT            6969
#define MAX_CONN        10
#define BUFFER_SIZE     10e7 // 10mB
#define error_exit(msg) { perror(msg); exit(EXIT_FAILURE); }

bool DEBUG;
char PWD[100];
char *greet = "Hello from the custom http server.";

char *url_decode(const char *src)
{
  size_t src_len = strlen(src);
  char *out = (char *)calloc(src_len + 1, sizeof(char));
  size_t out_len = 0;

  for (size_t i = 0; i < src_len; ++i) {
    if (src[i] == '%' && i + 2 < src_len) {
      int hex;
      sscanf(src + i + 1, "%2x", &hex);
      out[out_len++] = hex;
      i += 2;
    }
    else {
      out[out_len++] = src[i];
    }
  }
  out[out_len] = '\0';
  return out;
}

char *path_join(char *pwd, char *filepath)
{
  size_t fullpath_size = strlen(pwd) + strlen(filepath) + 1;
  char *fullpath = (char *)calloc(fullpath_size, sizeof(char));
  strcpy(fullpath, pwd);
  if (fullpath[strlen(fullpath)-1] == '/') fullpath[strlen(fullpath)-1] = '\0';
  strcat(fullpath, filepath);
  return fullpath;
}

void get_file_extension(const char *filepath, char *file_extension)
{
  const char *dot = strchr(filepath, '.');
  if (dot == NULL || dot == filepath) return;
  // potential overflow
  strncpy(file_extension, dot + 1, 16);
}

void get_mime_type(const char *file_extension, char *mime_type)
{
  if (strcasecmp(file_extension, "html") == 0 || strcasecmp(file_extension, "htm") == 0)
    strcpy(mime_type, "text/html");
  else if (strcasecmp(file_extension, "txt") == 0)
    strcpy(mime_type, "text/plain");
  else if (strcasecmp(file_extension, "jpg") == 0 || strcasecmp(file_extension, "jpeg") == 0)
    strcpy(mime_type, "image/jpeg");
  else if (strcasecmp(file_extension, "png") == 0)
    strcpy(mime_type, "image/png");
  else if (strcasecmp(file_extension, "pdf") == 0)
    strcpy(mime_type, "application/pdf");
  else // some mime types not supported for now (Java applets etc.)
    strcpy(mime_type, "text/html");
    //strcpy(mime_type, "application/octet-stream"); // -> download the file
}

char *build_http_response(const char *filepath, const char *file_extension)
{
  char mime_type[32] = {0};
  get_mime_type(file_extension, mime_type);
  if (DEBUG) printf("mime type : %s\n", mime_type);
  char *template = "HTTP/1.1 %s\r\n"
                   "Content-Type: %s\r\n"
                   "\n"
                   "%s"
                   "\r\n\n";
  FILE *file;
  char *response, *contents;
  char status[16];
  size_t response_size;
  if ((file = fopen(filepath, "r")) == NULL) {
    char *contents404 = "<h1 style=\"text-align:center;\">404 Not Found</h1>";
    contents = (char *)calloc(strlen(contents404)+1, sizeof(char));
    strcpy(contents, contents404);
    strcpy(status, "404 Not Found");
    response_size = strlen(template) + strlen(status) + strlen(mime_type) + strlen(contents);
    response = (char *)calloc(response_size, sizeof(char));
  }
  else {
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    response_size = strlen(template) + strlen(status) + strlen(mime_type) + file_size + 1;
    response = (char *)calloc(response_size, sizeof(char));
    contents = (char *)calloc(file_size + 1, sizeof(char));
    strcpy(status, "200 OK");
    fread(contents, sizeof(char), response_size, file);
    fclose(file);
  }

  snprintf(response, response_size * sizeof(char), template, status, mime_type, contents);
  free(contents);
  return response;
}

void *handle_client(void *arg)
{
  int client_fd = *(int *)arg;
  char *request = (char *)calloc(BUFFER_SIZE, sizeof(char));
  ssize_t bytes_received = recv(client_fd, request, BUFFER_SIZE * sizeof(char), 0);
  char *response;
  if (DEBUG) {
    printf("Client fd = %d\n", client_fd);
    printf("Received %ld bytes from client\n", bytes_received);
    printf("Request:\n%s", request);
  }

  // handle data
  if (bytes_received > 0) {
    char method[16] = {0};
    char route[128] = {0};
    // potential overflow
    sscanf(request, "%16s %128s", method, route);

    printf("Debug %s\n", DEBUG ? "ON" : "OFF");

    if (DEBUG) printf("method = %s\nroute = %s\n", method, route);
    if (strcmp(method, "GET") == 0) {
      char *filepath = url_decode(route);
      char file_extension[16] = {0};
      get_file_extension(filepath, file_extension);
      if (DEBUG) printf("file ext : %s\n", file_extension);
      char *fullpath = path_join(PWD, filepath);
      if (strstr("..", fullpath) != NULL) goto BadRequest;
      if (DEBUG) printf("fullpath : %s\n", fullpath);
      response = build_http_response(fullpath, file_extension);
      free(filepath);
      free(fullpath);
    }
    else {
      BadRequest:
      printf("[*] Implement method: %s\n", method);
      const char *badreq = "HTTP/1.1 400 Bad Request\r\n\n";
      response = (char *)calloc(strlen(badreq)+1, sizeof(char));
      strcpy(response, badreq);
    }
  }

  if (DEBUG) printf("Response:\n%s", response);
  /// send response
  send(client_fd, response, strlen(response), 0);
  // cleanup
  close(client_fd);
  free(request);
  free(response);
  free(arg);
  return NULL;
}

int main(int argc, const char **argv)
{
  char *dbglvl = getenv("DEBUG");
  DEBUG = dbglvl == NULL ? false : strcmp(dbglvl, "1") == 0;
  char *pwd = getenv("PWD");
  if (pwd == NULL) getcwd(PWD, 100);
  else strncpy(PWD, pwd, 100);
  if (DEBUG) printf("PWD=%s\n", PWD);

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    error_exit("setsockopt");
  
  struct sockaddr_in server_addr = {
    .sin_family = AF_INET,
    .sin_addr = INADDR_ANY,
    .sin_port = htons(PORT),
  };

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    error_exit("bind");

  if (listen(server_fd, MAX_CONN))
    error_exit("listen");
  
  for (;;) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int *client_fd = (int *)malloc(sizeof(int));
    if ((*client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) < 0)
      error_exit("accept");
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
    pthread_detach(thread_id);
  }

  return 0;
}
