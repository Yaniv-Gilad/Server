HTTP server

=== Description ===

Program files:
threadpool.c - 
The pool is implemented by a queue. When the server gets a connection (getting back from
accept()), it puts the connection in the queue. When there will be available thread
(can be immediately), it will handle this connection (read request and write response).


server.c - 
The server should handle the connections with the clients.
The server using TCP, baecuse of this it creates a socket for each client it talks to.

Command line usage: server <port> <pool-size> <max-number-of-request>
Port is the port number your server will listen on, pool-size is the number of threads in the
pool and -number-of-request is the maximum number of request your server will handle
before it destroys the pool.


functions:
	send_error_response - handle 302, 400, 403, 404, 500 and 501 errors.
	check_command_line - check command line usage is valid.
	split_argv - split the argv to port, size and num of requests.
    split_request - split the request and also check if valid method and version.
    main_socket - the welcome socket function.
    socket_func - the thread "work" function.
    check_path_permissions - check path permissions.
    send_file - check if there is read permission to the file send it.
    send_dir_content - send directory content.

	




