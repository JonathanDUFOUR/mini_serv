/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   mini_serv.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jodufour <jodufour@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/11/15 00:05:57 by jodufour          #+#    #+#             */
/*   Updated: 2022/11/15 04:48:46 by jodufour         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

//////////////
// Typedefs //
//////////////

typedef unsigned int		t_uint;
typedef struct s_client		t_client;
typedef struct s_client_lst	t_client_lst;

////////////////
// Structures //
////////////////

struct s_client
{
	int			fd;
	t_uint		id;
	t_client	*prev;
	t_client	*next;
};

struct s_client_lst
{
	t_client	*head;
	t_client	*tail;
	size_t		size;
};

//////////////////////
// Global variables //
//////////////////////

static int			g_server_sockfd = -1;
static t_client_lst	g_clients = {NULL, NULL, 0LU};

/////////////////////
// Utils functions //
/////////////////////

inline static t_client	*__client_new(int const fd, t_uint const id)
{
	t_client *const	output = calloc(1LU, sizeof(t_client));

	if (!output)
		return NULL;

	output->fd = fd;
	output->id = id;

	return output;
}

__attribute__((nonnull))
inline static void	__client_lst_push_back(t_client_lst *const lst, t_client *const node)
{
	if (!lst->size)
		lst->head = node;
	else
		lst->tail->next = node;
	node->prev = lst->tail;
	lst->tail = node;
	++lst->size;
}

__attribute__((nonnull))
inline static int	__client_lst_add_back(t_client_lst *const lst, int const fd, t_uint const id)
{
	t_client *const	node = __client_new(fd, id);

	if (!node)
		return -1;
	__client_lst_push_back(lst, node);
	return 0;
}

__attribute__((nonnull))
inline static void	__client_lst_del_one(t_client_lst *const lst, t_client *const node)
{
	if (node->prev)
		node->prev->next = node->next;
	else
		lst->head = lst->head->next;

	if (node->next)
		node->next->prev = node->prev;
	else
		lst->tail = lst->tail->prev;

	--lst->size;

	close(node->fd);
	free(node);
}

__attribute__((nonnull))
inline static void	__client_lst_clear(t_client_lst *const lst)
{
	t_client	*node;
	t_client	*next;

	node = lst->head;
	while (node)
	{
		next = node->next;
		close(node->fd);
		free(node);
		node = next;
	}
	memset(lst, 0, sizeof(t_client_lst));
}

__attribute__((nonnull))
inline static char	*__strdup(char const *const str)
{
	char *const	output = malloc((strlen(str) + 1) * sizeof(char));

	if (!output)
		return NULL;
	strcpy(output, str);
	return output;
}

__attribute__((nonnull))
inline static char	*__strjoin(char const *const str0, char const *const str1)
{
	char *const	output = malloc((strlen(str0) + strlen(str1) + 1) * sizeof(char));

	if (!output)
		return NULL;
	strcpy(output, str0);
	strcat(output, str1);
	return output;
}

__attribute__((nonnull))
inline static int	__extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int		i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

inline static int	__highest_numbered_fd(void)
{
	if (g_clients.size)
		return g_clients.tail->fd;
	return g_server_sockfd;
}

inline static void	__fatal_error(void)
{
	write(STDERR_FILENO, "Fatal error\n", 12LU);
	exit(EXIT_FAILURE);
}

////////////////////
// Core functions //
////////////////////

__attribute__((nonnull(1, 2)))
inline static int	__send_to_all_but_one(
	char const *const msg,
	fd_set const *const fds_write1,
	t_client const *const node)
{
	t_client	*curr;

	for (curr = g_clients.head ; curr ; curr = curr->next)
		if (FD_ISSET(curr->fd, fds_write1) && curr != node && !~send(curr->fd, msg, strlen(msg), 0))
			return -1;
	return 0;
}

__attribute__((nonnull))
inline static int	__accept_incoming_connections(
	fd_set *const fds_read0,
	fd_set const *const fds_read1,
	fd_set *const fds_write0,
	fd_set const *const fds_write1)
{
	static t_uint	available_id = 0U;
	int				client_sockfd;
	char			msg[40];

	if (FD_ISSET(g_server_sockfd, fds_read1))
	{
		client_sockfd = accept(g_server_sockfd, NULL, NULL);
		if (!~client_sockfd)
			return -1;
		
		FD_SET(client_sockfd, fds_read0);
		FD_SET(client_sockfd, fds_write0);

		if (!~__client_lst_add_back(&g_clients, client_sockfd, available_id))
			return -1;

		if (sprintf(msg, "server: client %u just arrived\n", available_id) < 0)
			return -1;

		if (!~__send_to_all_but_one(msg, fds_write1, g_clients.tail))
			return -1;

		++available_id;
	}
	return 0;
}

__attribute__((nonnull))
inline static int	__remove_client(
	t_client *const node,
	fd_set *const fds_read0,
	fd_set *const fds_write0,
	fd_set const *const fds_write1)
{
	t_uint const	id = node->id;
	char			msg[37LU];

	FD_CLR(node->fd, fds_read0);
	FD_CLR(node->fd, fds_write0);

	__client_lst_del_one(&g_clients, node);

	if (sprintf(msg, "server: client %u just left\n", id) < 0)
		return -1;

	if (!~__send_to_all_but_one(msg, fds_write1, NULL))
		return -1;

	return 0;
}

__attribute__((nonnull))
inline static int	__receive_client_messages(
	fd_set *const fds_read0,
	fd_set const *const fds_read1,
	fd_set *const fds_write0,
	fd_set const *const fds_write1)
{
	t_client		*curr;
	t_client		*next;
	char			buff[4097LU];
	char			*msg;
	char			*raw;
	char			*tmp;
	size_t const	buff_size = sizeof(buff) - 1;
	ssize_t			recv_ret;
	int				ret;

	raw = NULL;
	for (curr = g_clients.head ; curr ; curr = curr->next)
	{
receive_loop_start:

		if (FD_ISSET(curr->fd, fds_read1))
		{
			recv_ret = recv(curr->fd, buff, buff_size, 0);
			while (recv_ret == (ssize_t)buff_size)
			{
				buff[recv_ret] = 0;

				if (strstr(buff, "\n"))
					break ;

				if (!raw)
					raw = __strdup(buff);
				else
				{
					tmp = raw;
					raw = __strjoin(raw, buff);
					free(tmp);
				}
				if (!raw)
					return -1;

				recv_ret = recv(curr->fd, buff, buff_size, 0);
			}
			if (recv_ret <= 0)
			{
				free(raw);
				next = curr->next;
				if (!~__remove_client(curr, fds_read0, fds_write0, fds_write1))
					return -1;

				if (!next)
					break;
				curr = next;
				goto receive_loop_start;
			}
			else
			{
				buff[recv_ret] = 0;

				if (!raw)
					raw = __strdup(buff);
				else
				{
					tmp = raw;
					raw = __strjoin(raw, buff);
					free(tmp);
				}
				if (!raw)
					return -1;

				for (ret = __extract_message(&raw, &tmp) ; ret == 1 ; ret = __extract_message(&raw, &tmp))
				{
					msg = malloc((strlen(tmp) + 19 + 1) * sizeof(char));
					if (!msg)
					{
						free(tmp);
						free(raw);
						return -1;
					}

					ret = sprintf(msg, "client %u: %s", curr->id, tmp);
					free(tmp);
					if (ret < 0)
					{
						free(raw);
						free(msg);
						return -1;
					}

					ret = __send_to_all_but_one(msg, fds_write1, curr);
					free(msg);
					if (!~ret)
					{
						free(raw);
						return -1;
					}
				}
				free(tmp);
				free(raw);
				if (!~ret)
					return -1;
			}
		}
	}
	return 0;
}

int	main(int const ac, char const *const *const av)
{
	struct sockaddr_in	server_addr;
	fd_set				fds_read0;
	fd_set				fds_read1;
	fd_set				fds_write0;
	fd_set				fds_write1;

	if (ac != 2)
	{
		write(STDERR_FILENO, "Wrong number of arguments\n", 26LU);
		return EXIT_FAILURE;
	}

	// clear file desciptor sets
	FD_ZERO(&fds_read0);
	FD_ZERO(&fds_write0);

	// socket create and verification 
	g_server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (g_server_sockfd == -1)
		__fatal_error();
	FD_SET(g_server_sockfd, &fds_read0);

	// assign IP, PORT 
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET; 
	server_addr.sin_addr.s_addr = htonl(0x7F000001); //127.0.0.1
	server_addr.sin_port = htons(atoi(av[1]));
  
	// binding newly created socket to given IP and verification 
	if (!~bind(g_server_sockfd, (struct sockaddr const *)&server_addr, sizeof(server_addr)))
		__fatal_error();

	if (!~listen(g_server_sockfd, FD_SETSIZE - 3))
		__fatal_error();

	// server routine
	while (true)
	{
		fds_read1 = fds_read0;
		fds_write1 = fds_write0;
		if (!~select(__highest_numbered_fd() + 1, &fds_read1, &fds_write1, NULL, NULL))
			__fatal_error();

		if (!~__accept_incoming_connections(&fds_read0, &fds_read1, &fds_write0, &fds_write1))
			__fatal_error();

		if (!~__receive_client_messages(&fds_read0, &fds_read1, &fds_write0, &fds_write1))
			__fatal_error();
	}
	return EXIT_SUCCESS;
}

__attribute__((destructor))
inline static void	__clean_shutdown(void)
{
	__client_lst_clear(&g_clients);
	~g_server_sockfd && close(g_server_sockfd);
}
