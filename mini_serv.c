/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   mini_serv.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jodufour <jodufour@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/11/12 19:48:26 by jodufour          #+#    #+#             */
/*   Updated: 2022/11/14 23:09:20 by jodufour         ###   ########.fr       */
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

/******************************************************************************/
/*                                  TYPEDEFS                                  */
/******************************************************************************/

typedef struct s_client_lst	t_client_lst;
typedef struct s_client		t_client;
typedef unsigned int		t_uint;

/******************************************************************************/
/*                                 STRUCTURES                                 */
/******************************************************************************/

struct s_client_lst
{
	t_client	*head;
	t_client	*tail;
	size_t		size;
};

struct s_client
{
	int			fd;
	t_uint		id;
	t_client	*next;
	t_client	*prev;
};

/******************************************************************************/
/*                              GLOBAL VARIABLES                              */
/******************************************************************************/

static int			g_server_sockfd = -1;
static t_client_lst	g_clients = {NULL, NULL, 0LU};

/*****************************************************************************/
/*                                 FUNCTIONS                                 */
/*****************************************************************************/

inline static t_client	*__client_new(int const fd, t_uint const id)
{
	t_client *const	node = calloc(1LU, sizeof(t_client));

	if (!node)
		return NULL;

	node->fd = fd;
	node->id = id;

	return node;
}

__attribute__((nonnull))
inline static void	__client_lst_push_back(t_client_lst *const lst, t_client *const node)
{
	if (!lst->size)
		lst->head = node;
	else
	{
		lst->tail->next = node;
		node->prev = lst->tail;
	}
	lst->tail = node;
	++lst->size;
}

__attribute__((nonnull))
inline static int	__client_lst_add_back(t_client_lst *const lst, int const fd, t_uint const id)
{
	t_client *const	node = __client_new(fd, id);

	if (!node)
		return EXIT_FAILURE;

	__client_lst_push_back(lst, node);

	return EXIT_SUCCESS;
}

__attribute__((nonnull))
inline static void	__client_lst_del_one(t_client_lst *const lst, t_client *const node)
{
	if (lst->head == node)
		lst->head = node->next;
	else
		node->prev->next = node->next;

	if (lst->tail == node)
		lst->tail = node->prev;
	else
		node->next->prev = node->prev;

	--lst->size;

	free(node);
}

__attribute__((nonnull))
inline static void	__client_lst_clear(t_client_lst *const lst)
{
	t_client	*node;
	t_client	*next;

	for (node = lst->head ; node ; node = next)
	{
		next = node->next;
		free(node);
	}
	lst->head = NULL;
	lst->tail = NULL;
	lst->size = 0LU;
}

__attribute__((nonnull))
inline static int	__extract_message(char **const raw, char **const msg)
{
	char	*new_raw;
	size_t	idx;

	*msg = NULL;

	if (!*raw)
		return 0;

	for (idx = 0LU ; (*raw)[idx] ; ++idx)
		if ((*raw)[idx] == '\n')
		{
			new_raw = malloc((strlen(*raw + idx + 1) + 1) * sizeof(char));
			if (!new_raw)
				return -1;

			strcpy(new_raw, *raw + idx + 1);
			*msg = *raw;
			(*msg)[idx + 1] = 0;
			*raw = new_raw;
			return 1;
		}

	return 0;
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
inline static int	__send_to_all(char const *const msg, fd_set const *const fds_write1)
{
	t_client	*node;

	for (node = g_clients.head ; node ; node = node->next)
		if (FD_ISSET(node->fd, fds_write1))
			if (send(node->fd, msg, strlen(msg), 0) == -1)
				return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

__attribute__((nonnull))
inline static int	__send_to_all_but_one(char const *const msg, t_uint const id, fd_set const *const fds_write1)
{
	t_client	*node;

	for (node = g_clients.head ; node ; node = node->next)
		if (FD_ISSET(node->fd, fds_write1) && node->id != id)
			if (send(node->fd, msg, strlen(msg), 0) == -1)
				return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

__attribute__((nonnull))
inline static int	__accept_incoming_connection(
	fd_set *const fds_read0,
	fd_set const *const fds_read1,
	fd_set *const fds_write0,
	fd_set const *const fds_write1)
{
	static t_uint	available_id = 0U;
	char			buff[40LU];
	int				sockfd;

	if (FD_ISSET(g_server_sockfd, fds_read1))
	{
		sockfd = accept(g_server_sockfd, NULL, NULL);
		if (sockfd == -1)
			return EXIT_FAILURE;

		FD_SET(sockfd, fds_read0);
		FD_SET(sockfd, fds_write0);

		sprintf(buff, "server: client %u just arrived\n", available_id);

		if (__client_lst_add_back(&g_clients, sockfd, available_id))
			return EXIT_FAILURE;

		if (__send_to_all_but_one(buff, g_clients.tail->id, fds_write1))
			return EXIT_FAILURE;

		++available_id;
	}

	return EXIT_SUCCESS;
}

__attribute__((nonnull))
inline static int	__remove_client(
	t_client *const node,
	fd_set *const fds_read0,
	fd_set *const fds_write0,
	fd_set const *const fds_write1)
{
	char	buff[37LU];

	close(node->fd);

	FD_CLR(node->fd, fds_read0);
	FD_CLR(node->fd, fds_write0);

	sprintf(buff, "server: client %u just left\n", node->id);

	__client_lst_del_one(&g_clients, node);

	if (__send_to_all(buff, fds_write1))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

__attribute__((nonnull))
inline static int	__receive_messages_from_clients(
	fd_set *const fds_read0,
	fd_set const *const fds_read1,
	fd_set *const fds_write0,
	fd_set const *const fds_write1)
{
	t_client		*node;
	t_client		*next;
	ssize_t			recv_ret;
	char			buff[4097LU];
	size_t const	buff_size = sizeof(buff) - 1;
	char			*raw;
	char			*tmp;
	char			*msg;
	int				ret0;
	int				ret1;

	raw = NULL;
	for (node = g_clients.head ; node ; node = node->next)
	{
receive_loop_start:

		if (FD_ISSET(node->fd, fds_read1))
		{
			recv_ret = recv(node->fd, buff, buff_size, 0);
			while (recv_ret == (ssize_t)buff_size)
			{
				buff[recv_ret] = 0;

				if (strstr(buff, "\n"))
					break;

				if (!raw)
					raw = __strdup(buff);
				else
				{
					tmp = raw;
					raw = __strjoin(raw, buff);
					free(tmp);
				}
				if (!raw)
					return EXIT_FAILURE;

				recv_ret = recv(node->fd, buff, buff_size, 0);
			}
			if (recv_ret <= 0)
			{
				next = node->next;
				if (__remove_client(node, fds_read0, fds_write0, fds_write1))
					return EXIT_FAILURE;
				if (!next)
					break;
				node = next;
				goto receive_loop_start;
			}
			else
			{
				buff[recv_ret] = 0;

				if (!raw)
					raw = __strdup(buff);
				else
				{
					tmp = __strjoin(raw, buff);
					free(raw);
					if (!tmp)
						return EXIT_FAILURE;

					raw = tmp;
				}

				for (ret0 = __extract_message(&raw, &tmp) ; ret0 == 1 ; ret0 = __extract_message(&raw, &tmp))
				{
					msg = malloc((strlen(tmp) + 19LU + 1LU) * sizeof(char));
					sprintf(msg, "client %u: %s", node->id, tmp);
					free(tmp);
					ret1 = __send_to_all_but_one(msg, node->id, fds_write1);
					free(msg);
					if (ret1)
					{
						free(raw);
						return EXIT_FAILURE;
					}
				}
				if (ret0 == -1)
				{
					free(raw);
					return EXIT_FAILURE;
				}
				else
				{
					ret0 = __send_to_all_but_one(raw, node->id, fds_write1);
					free(raw);
					if (ret0)
						return EXIT_FAILURE;
				}
			}
		}
	}
	return EXIT_SUCCESS;
}

inline static int	__highest_numbered_fd(void)
{
	if (g_clients.size)
		return g_clients.tail->fd;
	return g_server_sockfd;
}

inline static void	__fatal_error(void)
{
	write(STDERR_FILENO, "Fatal error\n", 12);
	exit(EXIT_FAILURE);
}

int	main(int const ac, char const *const *const av)
{
	struct sockaddr_in	server_address;
	fd_set				fds_read0;
	fd_set				fds_read1;
	fd_set				fds_write0;
	fd_set				fds_write1;

	if (ac != 2)
	{
		write(STDERR_FILENO, "Wrong number of arguments\n", 26);
		return EXIT_FAILURE;
	}

	memset(&server_address, 0, sizeof(server_address));
	FD_ZERO(&fds_read0);
	FD_ZERO(&fds_write0);
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(atoi(av[1]));
	server_address.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

	g_server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (g_server_sockfd == -1)
		__fatal_error();
	FD_SET(g_server_sockfd, &fds_read0);

	if (bind(g_server_sockfd, (struct sockaddr const *)&server_address, sizeof(server_address)) == -1)
		__fatal_error();

	if (listen(g_server_sockfd, FD_SETSIZE - 3) == -1)
		__fatal_error();

	while (true)
	{
		fds_read1 = fds_read0;
		fds_write1 = fds_write0;
		if (select(__highest_numbered_fd() + 1, &fds_read1, &fds_write1, NULL, NULL) == -1)
			__fatal_error();

		if (__accept_incoming_connection(&fds_read0, &fds_read1, &fds_write0, &fds_write1))
			__fatal_error();

		if (__receive_messages_from_clients(&fds_read0, &fds_read1, &fds_write0, &fds_write1))
			__fatal_error();
	}

	return EXIT_SUCCESS;
}

__attribute__((destructor))
static void	__clean_shutdown(void)
{
	__client_lst_clear(&g_clients);
	if (g_server_sockfd != -1)
		close(g_server_sockfd);
}
