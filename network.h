/*
 * network.h
 *
 *  Created on: 28 окт. 2009
 *      Author: tipok
 */

#ifndef NETWORK_H_
#define NETWORK_H_

/* Network             MTU (bytes)
 * -------------------------------
 * 16 Mbps Token Ring        17914
 * 4 Mbps Token Ring          4464
 * FDDI                       4352
 * Ethernet                   1500
 * IEEE 802.3/802.2           1492
 * PPPoE (WAN Miniport)       1480
 * X.25                        576 */

#define UDP_PAYLOAD 	1480
int output_init_udp(char *ip, int port, int ttl);
int input_init_udp(char *ip, int port);
int udp_read_timeout(int fd, void *buf, size_t *len, int timeout_ms);


#endif /* NETWORK_H_ */
