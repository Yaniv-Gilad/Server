#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "threadpool.h"
#define MAX_CLIENT_LINE 4000
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

void check_command_line(int argc, char *argv[]);

int isNumber(char *number);

void split_argv(char *argv[], int *port, int *pool_size, int *num_of_request);

void main_socket(threadpool **tpool, int port, int num_of_request);

int socket_func(void *arg);

int find_end_line(char *request);

int split_request(char *request, char **method, char **path, char **version);

void send_error_response(int sock, int status, char *path);

void dir_main_func(int sock, char *path, struct stat *fs_p);

void dir_second_func(int sock, char *path, struct stat *fs_p);

void send_file(int sock, char *path);

void send_dir_content(int sock, char *path);

char *get_mime_type(char *name);

int get_num_of_files_in_folder(char *path);

void insert_file_details(char *path, char *name, char **last_u, char **ori_size);

int check_path_permissions(int sock, char *path);

//////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    int port = 0;
    int pool_size = 0;
    int num_of_request = 0;
    threadpool *tpool = NULL;

    check_command_line(argc, argv);
    split_argv(argv, &port, &pool_size, &num_of_request);
    tpool = create_threadpool(pool_size);
    if (tpool == NULL)
        return EXIT_FAILURE;

    main_socket(&tpool, port, num_of_request);

    return 0;
}
/////////////////////////////////////////////////////////////////////////

// check command line usage is valid
void check_command_line(int argc, char *argv[])
{
    if (argc != 4)
    {
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_FAILURE);
    }

    if (isNumber(argv[1]) + isNumber(argv[2]) + isNumber(argv[3]) != 0)
    {
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_FAILURE);
    }
}

// return 0 if number and -1 if not
int isNumber(char *number)
{
    int len = strlen(number);
    for (int i = 0; i < len; i++)
    {
        if (number[i] != '0' && number[i] != '1' && number[i] != '2' && number[i] != '3' && number[i] != '4' && number[i] != '5' && number[i] != '6' && number[i] != '7' && number[i] != '8' && number[i] != '9')
            return -1;
    }
    return 0;
}

// split the argv to port, size and num of requests
void split_argv(char *argv[], int *port, int *pool_size, int *num_of_request)
{
    *port = atoi(argv[1]);
    *pool_size = atoi(argv[2]);
    *num_of_request = atoi(argv[3]);

    // check valid input
    if (*port <= 0 || *pool_size <= 0 || *num_of_request < 0)
    {
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_FAILURE);
    }
}

void main_socket(threadpool **tpool, int port, int num_of_request)
{
    int w_socket = 0;
    int *socket_arr = NULL;
    struct sockaddr_in serv_addr = {0};

    // the sockets_fd that returns from "acccpt"
    socket_arr = (int *)malloc(sizeof(int) * num_of_request);
    if (socket_arr == NULL)
    {
        perror("ERROR malloc\n");
        destroy_threadpool(*tpool);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < num_of_request; i++)
        socket_arr[i] = -1;

    // initiate welcome socket
    w_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (w_socket < 0)
    {
        perror("ERROR opening socket\n");
        destroy_threadpool(*tpool);
        free(socket_arr);
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    // bind
    if (bind(w_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR on binding\n");
        destroy_threadpool(*tpool);
        free(socket_arr);
        close(w_socket);
        exit(EXIT_FAILURE);
    }

    // listen
    if (listen(w_socket, 5) < 0)
    {
        perror("ERROR on binding\n");
        destroy_threadpool(*tpool);
        free(socket_arr);
        close(w_socket);
        exit(EXIT_FAILURE);
    }

    //dispatch the work
    int i = 0;
    for (i = 0; i < num_of_request; i++)
    {
        if ((socket_arr[i] = accept(w_socket, NULL, NULL)) < 0)
        {
            perror("ERROR on accept\n");
            destroy_threadpool(*tpool);
            free(socket_arr);
            close(w_socket);
            exit(EXIT_FAILURE);
        }
        dispatch(*tpool, socket_func, &socket_arr[i]);
    }

    destroy_threadpool(*tpool);
    free(socket_arr);
    close(w_socket);
}

// find "\r\n" in request and put there "\0"
int find_end_line(char *request)
{
    int i = 0;
    while (request[i] != '\0' && request[i + 1] != '\0')
    {
        if (request[i] == '\r' && request[i + 1] == '\n')
        {
            request[i] = '\0';
            return i;
        }
        i++;
    }

    return -1;
}

// split the request and also check if valid method and version. returns 0 if valid till now
int split_request(char *request, char **method, char **path, char **version)
{
    char *check = NULL;
    *method = strtok(request, " ");
    if (*method == NULL)
        return 400;

    *path = strtok(NULL, " ");
    if (*path == NULL)
        return 400;

    *version = strtok(NULL, " ");
    if (*version == NULL)
        return 400;

    check = strtok(NULL, " ");
    if (check != NULL)
        return 400;

    if (strcmp(*method, "GET") != 0)
        return 501;

    if (strcmp(*version, "HTTP/1.0") != 0 && strcmp(*version, "HTTP/1.1") != 0)
        return 400;

    return 0;
}

// handle 302, 400, 403, 404, 500 and 501
void send_error_response(int sock, int status, char *path)
{
    char message[1500] = "HTTP/1.1 ";
    char timebuf[128];
    time_t now;
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));

    if (status == 302)
        strcat(message, "302 Found\r\n");
    if (status == 400)
        strcat(message, "400 Bad Request\r\n");
    if (status == 403)
        strcat(message, "403 Forbidden\r\n");
    if (status == 404)
        strcat(message, "404 Not Found\r\n");
    if (status == 500)
        strcat(message, "500 Internal Server Error\r\n");
    if (status == 501)
        strcat(message, "501 Not supported\r\n");

    strcat(message, "Server: webserver/1.0\r\nDate: ");
    strcat(message, timebuf);
    if (status == 302)
    {
        strcat(message, "\r\nLocation: ");
        strcat(message, path);
        strcat(message, "/");
    }

    strcat(message, "\r\nContent-Type: text/html\r\nContent-Length: ");

    if (status == 302)
        strcat(message, "123\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>302 Found</TITLE></HEAD><BODY><H4>302 Found</H4>Directories must end with a slash.</BODY></HTML>");
    if (status == 400)
        strcat(message, "113\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD><BODY><H4>400 Bad request</H4>Bad Request.</BODY></HTML>");
    if (status == 403)
        strcat(message, "111\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD><BODY><H4>403 Forbidden</H4>Access denied.</BODY></HTML>");
    if (status == 404)
        strcat(message, "112\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD><BODY><H4>404 Not Found</H4>File not found.</BODY></HTML>");
    if (status == 500)
        strcat(message, "144\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD><BODY><H4>500 Internal Server Error</H4>Some server side error.</BODY></HTML>");
    if (status == 501)
        strcat(message, "129\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD><BODY><H4>501 Not supported</H4>Method is not supported.</BODY></HTML>");

    write(sock, message, strlen(message));
}

// the thread "work" function
int socket_func(void *arg)
{
    int check = 0;
    int sock = *(int *)arg;
    char request[MAX_CLIENT_LINE + 1] = "\0";
    char *method = NULL;
    char *path = NULL;
    char *version = NULL;
    struct stat fs = {0};

    // read the request
    check = read(sock, request, MAX_CLIENT_LINE);
    if (check < 0)
    {
        perror("read failed\n");
        close(sock);
        return -1;
    }
    request[check] = '\0';

    // find "\r\n" in request and put there "\0"
    check = find_end_line(request);
    if (check < 0)
    {
        close(sock);
        return -1;
    }

    int status = split_request(request, &method, &path, &version);
    if (status != 0)
    {
        send_error_response(sock, status, NULL);
        close(sock);
        return -1;
    }

    // if the path contains "//"
    if(strstr(path,"//") != NULL)
    {
        send_error_response(sock, 400, NULL);
        close(sock);
        return -1;
    }

    // if it's the current directory
    if (strcmp(path, "/") == 0)
    {
        if (stat("./", &fs) == -1)
        {
            send_error_response(sock, 500, NULL);
            close(sock);
            return -1;
        }
        dir_main_func(sock, "./", &fs);
        close(sock);
        return 1;
    }

    // if the file doesn't exist
    if (access(path + 1, F_OK) != 0)
    {
        send_error_response(sock, 404, NULL);
        close(sock);
        return -1;
    }

    // if there are no 'x' permissions for user
    if (stat(path + 1, &fs) == -1)
    {
        send_error_response(sock, 500, NULL);
        close(sock);
        return -1;
    }

    // if it's directory
    if (S_ISDIR(fs.st_mode))
    {
        dir_main_func(sock, path + 1, &fs);
        close(sock);
        return 1;
    }

    // if it's file
    if (S_ISREG(fs.st_mode))
    {
        send_file(sock, path + 1);
        close(sock);
        return 1;
    }
    else
    {
        send_error_response(sock, 403, NULL);
        close(sock);
        return -1;
    }

    close(sock);
    return -1;
}

// directory main function
void dir_main_func(int sock, char *path, struct stat *fs_p)
{
    struct stat fs = *fs_p;

    // if directory and doesnt end with '/'
    if (S_ISDIR(fs.st_mode) && path[strlen(path) - 1] != '/')
    {
        send_error_response(sock, 302, path);
        return;
    }

    if (S_ISDIR(fs.st_mode) && path[strlen(path) - 1] == '/')
    {
        dir_second_func(sock, path, fs_p);
        return;
    }
}

void dir_second_func(int sock, char *path, struct stat *fs_p)
{
    struct dirent *dentry = {0};
    int is_exist = 0;

    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        send_error_response(sock, 501, NULL);
        perror("ERROR: open dir\n");
        return;
    }

    // check if there is "index.html"
    while ((dentry = readdir(dir)) != NULL)
    {
        if (strcmp(dentry->d_name, "index.html") == 0)
        {
            is_exist = 1;
            break;
        }
    }

    // send index.html
    if (is_exist == 1)
    {
        char *full_path = (char *)malloc(sizeof(char) * (strlen(path) + 20));
        if (full_path == NULL)
        {
            send_error_response(sock, 500, NULL);
            perror("ERROR: malloc\n");
            return;
        }
        full_path[0] = '\0';
        strcat(full_path, path);
        strcat(full_path, "index.html");
        send_file(sock, full_path);
        free(full_path);
    }
    // send dir content
    else
    {
        send_dir_content(sock, path);
    }

    if (closedir(dir) == -1)
    {
        perror("ERROR: close dir\n");
        return;
    }
}

// check if there is read permission to the file send it
void send_file(int sock, char *path)
{
    // if the file doesnt exist or there are no permissions
    if (check_path_permissions(sock, path) != 1)
        return;

    struct stat fs = {0};
    if (stat(path, &fs) == -1)
    {
        send_error_response(sock, 500, NULL);
        return;
    }

    //  open the file
    int file_fd = 0;
    if ((file_fd = open(path, O_RDONLY, 0666)) < 0)
    {
        send_error_response(sock, 500, NULL);
        perror("ERROR: open\n");
        return;
    }

    // build the message
    char timebuf[128];
    char last_update[128];
    time_t now;
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    now = fs.st_mtime;
    strftime(last_update, sizeof(last_update), RFC1123FMT, gmtime(&now));
    char size[50] = "";
    sprintf(size, "%d", (int)fs.st_size);

    char message[1500] = "HTTP/1.1 ";
    strcat(message, "200 OK\r\n");
    strcat(message, "Server: webserver/1.0\r\nDate: ");
    strcat(message, timebuf);
    if (get_mime_type(path) != NULL)
    {
        strcat(message, "\r\nContent-Type: ");
        strcat(message, get_mime_type(path));
    }
    strcat(message, "\r\nContent-Length: ");
    strcat(message, size);
    strcat(message, "\r\nLast-Modified: ");
    strcat(message, last_update);
    strcat(message, "\r\nConnection: close\r\n\r\n");

    write(sock, message, strlen(message));

    unsigned char *buf[500];
    int r = 1;
    while (r > 0)
    {
        r = read(file_fd, buf, 450);
        if (r < 0)
        {
            perror("ERROR: read");
            close(file_fd);
            return;
        }
        buf[r] = '\0';
        write(sock, buf, r);
    }

    close(file_fd);
}

// send directory content
void send_dir_content(int sock, char *path)
{
    // if the folder doesnt exist or there are no permissions
    if (check_path_permissions(sock, path) != 1)
        return;

    struct stat fs = {0};
    if (stat(path, &fs) == -1)
    {
        send_error_response(sock, 500, NULL);
        return;
    }

    char *html = NULL;
    int num = get_num_of_files_in_folder(path);

    html = (char *)malloc(sizeof(char) * (1000 + num * 700));
    if (html == NULL)
    {
        send_error_response(sock, 500, NULL);
        perror("ERROR: malloc\n");
        return;
    }
    html[0] = '\0';
    strcat(html, "<HTML><HEAD><TITLE>Index of ");
    strcat(html, path);
    strcat(html, " </TITLE></HEAD>");
    strcat(html, "<BODY><H4>Index of ");
    strcat(html, path);
    strcat(html, " </H4><table CELLSPACING=8><tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\n");

    struct dirent *dentry = {0};
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        send_error_response(sock, 500, NULL);
        free(html);
        perror("ERROR: open dir\n");
        return;
    }
    char *last_u = (char *)malloc(sizeof(char) * 50);
    if (last_u == NULL)
    {
        send_error_response(sock, 500, NULL);
        closedir(dir);
        free(html);
        perror("ERROR: malloc\n");
        return;
    }
    char *orig_size = (char *)malloc(sizeof(char) * 50);
    if (orig_size == NULL)
    {
        send_error_response(sock, 500, NULL);
        closedir(dir);
        free(html);
        free(last_u);
        perror("ERROR: malloc\n");
        return;
    }
    last_u[0] = '\0';
    orig_size[0] = '\0';

    while ((dentry = readdir(dir)) != NULL)
    {
        insert_file_details(path, dentry->d_name, &last_u, &orig_size);
        strcat(html, "<tr><td><A HREF=\"");
        strcat(html, dentry->d_name);
        strcat(html, "\">");
        strcat(html, dentry->d_name);
        strcat(html, " </A></td><td>");
        strcat(html, last_u);
        strcat(html, "</td><td>");
        strcat(html, orig_size);
        strcat(html, "</td></tr>");
    }
    free(last_u);
    free(orig_size);
    strcat(html, "</table><HR><ADDRESS>webserver/1.0</ADDRESS></BODY></HTML>");

    if (closedir(dir) == -1)
    {
        free(html);
        perror("ERROR: close dir\n");
        return;
    }

    // build the message
    char timebuf[128];
    char last_update[128];
    time_t now;
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    now = fs.st_mtime;
    strftime(last_update, sizeof(last_update), RFC1123FMT, gmtime(&now));
    char size[50] = "";
    sprintf(size, "%d", (int)strlen(html));

    char message[1500] = "HTTP/1.1 ";
    strcat(message, "200 OK\r\n");
    strcat(message, "Server: webserver/1.0\r\nDate: ");
    strcat(message, timebuf);
    strcat(message, "\r\nContent-Type: text/html");
    strcat(message, "\r\nContent-Length: ");
    strcat(message, size);
    strcat(message, "\r\nLast-Modified: ");
    strcat(message, last_update);
    strcat(message, "\r\nConnection: close\r\n\r\n");

    write(sock, message, strlen(message));
    write(sock, html, strlen(html));
    free(html);
}

// get type of file
char *get_mime_type(char *name)
{
    char *ext = strrchr(name, '.');
    if (!ext)
        return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".gif") == 0)
        return "image/gif";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    if (strcmp(ext, ".au") == 0)
        return "audio/basic";
    if (strcmp(ext, ".wav") == 0)
        return "audio/wav";
    if (strcmp(ext, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0)
        return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0)
        return "audio/mpeg";
    return NULL;
}

// get num of files in directory
int get_num_of_files_in_folder(char *path)
{
    struct dirent *dentry = {0};
    int res = 0;
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        perror("ERROR: open dir\n");
        return res;
    }
    while ((dentry = readdir(dir)) != NULL)
    {
        res++;
    }

    if (closedir(dir) == -1)
    {
        perror("ERROR: close dir\n");
        return res;
    }
    return res;
}

void insert_file_details(char *path, char *name, char **last_u, char **ori_size)
{
    (*last_u)[0] = '\0';
    (*ori_size)[0] = '\0';

    int s = strlen(path) + strlen(name) + 10;
    char *full_path = (char *)malloc(sizeof(char) * s);
    if (full_path == NULL)
    {
        perror("ERROR: malloc\n");
        return;
    }
    full_path[0] = '\0';
    strcat(full_path, path);
    strcat(full_path, name);

    // if the file doesnt exist or there are no permissions
    struct stat fs = {0};
    if (stat(full_path, &fs) == -1)
    {
        free(full_path);
        return;
    }
    free(full_path);

    char size[50] = "";
    char last_update[128];
    time_t now;
    now = time(NULL);

    now = fs.st_mtime;
    strftime(last_update, sizeof(last_update), RFC1123FMT, gmtime(&now));
    sprintf(size, "%d", (int)fs.st_size);

    strcat(*last_u, last_update);
    if (S_ISDIR(fs.st_mode))
        strcat(*ori_size, "");
    else
        strcat(*ori_size, size);
}

int check_path_permissions(int sock, char *path)
{
    struct stat file_info = {0};
    // if the file doesn't exist
    if (access(path, F_OK) != 0)
    {
        send_error_response(sock, 404, NULL);
        return -1;
    }

    // check file read permission
    if (stat(path, &file_info) == -1)
    {
        send_error_response(sock, 500, NULL);
        return -1;
    }
    if (!(S_IROTH & file_info.st_mode))
    {
        send_error_response(sock, 403, NULL);
        return -1;
    }

    //  start check the path
    int i = 0;
    int num_of_slash = 0;
    char *temp = (char *)malloc((sizeof(char) * strlen(path)) + 5);
    if (temp == NULL)
    {
        send_error_response(sock, 500, NULL);
        perror("Error: malloc\n");
        return -1;
    }
    temp[0] = '\0';
    strcpy(temp, path);

    while (temp[i] != '\0')
    {
        if (temp[i] == '/')
            num_of_slash++;
        i++;
    }
    if (temp[i - 1] == '/') // if folder
        num_of_slash--;

    i = 0;
    int j = 0;
    int *slash_places = (int *)malloc(sizeof(int) * num_of_slash);
    if (slash_places == NULL)
    {
        send_error_response(sock, 500, NULL);
        free(temp);
        perror("Error: malloc\n");
        return -1;
    }

    while (temp[i] != '\0')
    {
        if (temp[i] == '/' && i < strlen(path) - 1)
        {
            slash_places[j] = i;
            j++;
        }
        i++;
    }

    for (i = num_of_slash; i > 0; i--)
    {
        temp[slash_places[i - 1]] = '\0';
        if (stat(temp, &file_info) == -1)
        {
            send_error_response(sock, 500, NULL);
            free(temp);
            free(slash_places);
            return -1;
        }

        // check path execute permission
        if (!(S_IXOTH & file_info.st_mode))
        {
            send_error_response(sock, 403, NULL);
            free(temp);
            free(slash_places);
            return -1;
        }
    }

    // succes
    free(temp);
    free(slash_places);
    return 1;
}