
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>

#include <arpa/inet.h>
#include <netinet/in.h> 


#include <netdb.h>

#include "network.h"

/***********************************************
 * Static functions
 ***********************************************/
static int socket_open(char *laddr, int port) {
	struct sockaddr_in	lsin;
	int			sock;
	int			reuse=1;
	
	memset(&lsin, 0, sizeof(struct sockaddr_in));

	sock=socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock<0)
		return sock;
		
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&reuse, sizeof(reuse)) < 0) {
		close(sock);
		return -1;
	}

	lsin.sin_family=AF_INET;
	lsin.sin_addr.s_addr=INADDR_ANY;
	if (laddr)
		inet_aton(laddr, &lsin.sin_addr);
	lsin.sin_port=htons(port);

	if (bind(sock, (struct sockaddr *) &lsin,
			sizeof(struct sockaddr_in)) != 0) {
		close(sock);
		return -1;
	}

	return sock;
}

static int socket_set_ttl(int sock, int ttl) {
	if (ttl)
		return setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	return 0;
}

/* Join the socket on a multicats group e.g. tell the kernel 
 * to send out IGMP join messages ...
 *
 * Returns 0 on success and != 0 in failure
 *
 */
static int socket_join_multicast(int sock, char *addr) {
	struct ip_mreq		mreq;

	memset(&mreq, 0, sizeof(struct ip_mreq));

	/* Its not an ip address ? */
	if (!inet_aton(addr, &mreq.imr_multiaddr))
		return -1;

	if (!IN_MULTICAST(ntohl(mreq.imr_multiaddr.s_addr)))
		return 0;

	mreq.imr_interface.s_addr=INADDR_ANY;

	return setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
}

static int socket_set_nonblock(int sock)
{
	unsigned int	flags;

	flags=fcntl(sock, F_GETFL);
	return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

static int socket_connect(int sock, char *addr, int port)
{
	struct sockaddr_in	rsin;

	memset(&rsin, 0, sizeof(struct sockaddr_in));

	/* Create remote end sockaddr_in */
	rsin.sin_family=AF_INET;
	rsin.sin_port=htons(port);
	rsin.sin_addr.s_addr=INADDR_ANY;

	if (addr)
		inet_aton(addr, &rsin.sin_addr);

	return connect(sock, (struct sockaddr *) &rsin, sizeof(struct sockaddr_in));
}



/* ***************************************************
 * Shared functions
 * ***************************************************/
int input_init_udp(char *ip, int port)
{
	int sock;
	sock=socket_open(ip, port);
	if(sock < 0)
		return sock;
	
	if(socket_set_nonblock(sock) < 0) {
		close(sock);
		return -1;
	}

	/* Join Multicast group if its a multicast source */
	if (socket_join_multicast(sock, ip) < 0) {
		close(sock);
		return -1;
	}
	return sock;
}

int output_init_udp(char *ip, int port, int ttl)
{
	int sock;
	sock=socket_open(NULL, 0);
	if (sock < 0)
		return sock;

	socket_set_nonblock(sock);

	if (socket_connect(sock, ip, port)) {
		close(sock);
		return -1;
	}

	/* Join Multicast group if its a multicast destination */
	socket_join_multicast(sock, ip);

	if (ttl)
		socket_set_ttl(sock, ttl);
	return sock;
}


int udp_read_timeout(int fd, void *buf, size_t *len, int timeout_ms)
{
    int x;
    struct pollfd fds;

    assert(timeout_ms > 0);
    fds.fd = fd;
    fds.events = POLLIN;
    fds.revents = 0;
    x = poll(&fds, 1, timeout_ms);
    if(x == 0) {
        *len = 0;
        return -ETIMEDOUT;
    }

    *len = recv(fd, buf, 8192, MSG_DONTWAIT);
    if(*len == -1)
        return errno;

    if(*len == 0)
        return -ECONNRESET;

    return 0;
}
