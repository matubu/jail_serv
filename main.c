#pragma GCC optimize("O3")

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#define REQ_BUF_SIZE 1024
#define FILE_BUF_SIZE (1024 * 1024)

void	error(char *s) {
	printf("\x1b[101m ERROR \x1b[0m %s", s);
	if (errno) {
		printf(": %s\n", strerror(errno));
	} else {
		printf("\n");
	}
}

void	die(char *s) {
	error(s);
	exit(1);
}

void	usage(char *s) {
	error(s);
	printf(
		"\n"
		"\x1b[94mserv\x1b[0m [path] [...flags]\n"
		"\n"
		"\x1b[104m Flags \x1b[0m\n"
		"  --port <port>  \x1b[90mSpecify the listening port.\x1b[0m\n"
		"         --host  \x1b[90mExpose the server to external network.\x1b[0m\n"
	);
	exit(1);
}

void	drop_root_privileges(void) {
	gid_t	gid;

	if ((gid = getgid()) == 0) {
		char *sgid = getenv("SUDO_GID");

		if (sgid == NULL) {
			die("failed to getenv `SUDO_GID`");
		}

		if (setgid(atoi(sgid)) < 0) {
			die("failed to setgid");
		}
	}

	uid_t	uid;

	if ((uid = getuid()) == 0) {
		char *suid = getenv("SUDO_UID");

		if (suid == NULL) {
			die("failed to getenv `SUDO_UID`");
		}

		if (setuid(atoi(suid)) < 0) {
			die("failed to setuid");
		}
	}

	if (getuid() == 0 || geteuid() == 0
		|| getgid() == 0 || getegid() == 0
		|| setuid(0) == 0 || seteuid(0) == 0
		|| setgid(0) == 0 || setegid(0) == 0) {
		die("failed to drop privileges");
	}
}

char	*find_public_ip() {
	struct ifaddrs *addrs, *addr;

	getifaddrs(&addrs);
	addr = addrs;

	while (addr) 
	{
		if (addr->ifa_addr && addr->ifa_addr->sa_family == AF_INET)
		{
			char *ip = inet_ntoa(((struct sockaddr_in *)addr->ifa_addr)->sin_addr);

			if (strcmp(ip, "127.0.0.1")) {
				freeifaddrs(addrs);
				return (ip);
			}
		}

		addr = addr->ifa_next;
	}

	freeifaddrs(addrs);
	return (NULL);
}

void	http_log(int status, char *request) {
	if (status >= 100 && status <= 199) {
		printf(" \x1b[104m");
	}
	else if (status >= 200 && status <= 299) {
		printf(" \x1b[102m");
	}
	else if (status >= 300 && status <= 399) {
		printf(" \x1b[103m");
	}
	else {
		printf(" \x1b[101m");
	}
	printf(" %d \x1b[0m %s\n", status, request);
}

void	http_error(int fd, int status, char *reason, char *request) {
	http_log(status, request);
	dprintf(fd, "HTTP/1.1 %d %s\r\n\r\n", status, reason);
	close(fd);
}

int		main(int ac, const char **av) {
	if (ac < 2 || av == NULL) {
		usage("invalid number of arguments");
	}

	const char			*path = av[1];
	unsigned long int	port = 80;
	int					host = 0;

	for (int i = 2; i < ac; ++i) {
		if (strcmp(av[i], "--port") == 0) {
			++i;
			if (av[i] == NULL || !isdigit(*av[i])) {
				usage("expected port number");
			}
			port = atoi(av[i]);
		}
		else if (strcmp(av[i], "--host") == 0) {
			host = 1;
		}
		else {
			usage("unrecognized flag");
		}
	}

	if (chdir(path) != 0) {
		die("failed to chdir");
	}

	char	cwd[1024];
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		die("failed to getcwd");
	}

	int					sock, client;
	socklen_t			addrlen;
	struct sockaddr_in	addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = host ? INADDR_ANY : inet_addr("127.0.0.1"),
		.sin_port = htons(port)
	};

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		die("socket");
	}
	int optval = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
		error("failed to set socket to reuse port");
	}
	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		die("bind");
	}
	if (listen(sock, 10) < 0){
		die("listen");
	}

	if (chroot(".") < 0) {
		die("failed to create chroot jail");
	}

	drop_root_privileges();

	printf("\n");
	printf(" \x1b[90m┃\x1b[0m Local      \x1b[1;94mhttp://localhost:%lu/\x1b[0m\n", port);
	if (host) {
		char	*public_ip = find_public_ip();
		if (public_ip == NULL) {
			printf(" \x1b[90m┃\x1b[0m Network    \x1b[1;91merror\x1b[0m\n");
		} else {
			printf(" \x1b[90m┃\x1b[0m Network    \x1b[1;94mhttp://%s:%lu/\x1b[0m\n", public_ip, port);
		}
	} else {
		printf(" \x1b[90m┃\x1b[0m Network    \x1b[90muse --host to expose\x1b[0m\n");
	}
	printf(" \x1b[90m┃\x1b[0m User id   \x1b[0m \x1b[90m(real %d) (effective %d)\x1b[0m\n", getuid(), geteuid());
	printf(" \x1b[90m┃\x1b[0m Group id  \x1b[0m \x1b[90m(real %d) (effective %d)\x1b[0m\n", getgid(), getegid());
	printf(" \x1b[90m┃\x1b[0m Real cwd  \x1b[0m \x1b[90m%s\x1b[0m\n", cwd);
	printf(" \x1b[90m┃\x1b[0m Jail cwd  \x1b[0m \x1b[90m%s\x1b[0m\n", getcwd(cwd, sizeof(cwd)));
	printf("\n");

	char	buf[REQ_BUF_SIZE + 64];
	char	*file_buf = malloc(FILE_BUF_SIZE);

	if (file_buf == NULL) {
		die("allocation error");
	}

	while (1) {
		if ((client = accept(sock, (struct sockaddr *) &addr, &addrlen)) < 0)
		{
			error("accept");
			continue ;
		}

		int		ret = recv(client, buf, REQ_BUF_SIZE, 0);
		if (ret < 0) {
			error("recv");
			close(client);
			continue ;
		}

		buf[ret] = '\0';
		buf[strcspn(buf, "\r\n")] = '\0';

		char	*version = strrchr(buf, ' ');
		if (!version) {
			http_error(client, 400, "Bad Request", buf);
			continue ;
		}
		if (strcmp(version + 1, "HTTP/1.1")) {
			http_error(client, 505, "HTTP Version Not Supported", buf);
			continue ;
		}
		*version = '\0';
		if (strncmp(buf, "GET ", 4)) {
			http_error(client, 405, "Method Not Allowed", buf);
			continue ;
		}

		char		*path = buf + 4;
		struct stat	statbuf;

		if (stat(path, &statbuf) < 0 || S_ISDIR(statbuf.st_mode)) {
			strcat(path, "/index.html");
			if (stat(path, &statbuf) < 0 || S_ISDIR(statbuf.st_mode)) {
				http_error(client, 404, "Not Found", buf);
			}
		}
		
		int	fd = open(path, O_RDONLY);
		if (fd < 0) {
			http_error(client, 404, "Not Found", buf);
			continue ;
		}

		if (statbuf.st_size < 0 || statbuf.st_size > FILE_BUF_SIZE) {
			http_error(client, 500, "Internal Server Error", buf);
			continue ;
		}

		int file_len = read(fd, file_buf, statbuf.st_size);

		if (file_len < 0) {
			http_error(client, 500, "Internal Server Error", buf);
			continue ;
		}

		http_log(200, buf);
		dprintf(client, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", file_len);
		write(client, file_buf, file_len);

		close(client);
	}
}
