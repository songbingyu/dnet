// File: overlay/overlay.c
//
// Description: this file implements an ON process
// An ON process first connects to all the neighbors and then starts listen_to_neighbor threads 
// each of which keeps receiving the incoming packets from a neighbor and forwarding the received packets to the SNP process. 
// Then ON process waits for the connection from SNP process. After a SNP process is connected, 
// the ON process keeps receiving sendpkt_arg_t structures from the SNP process and sending 
// the received packets out to the overlay network. 
//

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>

#include "../common/seg.h"

#include "../common/constants.h"
#include "../common/pkt.h"
#include "overlay.h"
#include "../topology/topology.h"
#include "neighbortable.h"

//you should start the ON processes on all the overlay hosts within this period of time
#define OVERLAY_START_DELAY 20

/**************************************************************/
//declare global variables
/**************************************************************/

//declare the neighbor table as global variable 
nbr_entry_t* nt; 
//declare the TCP connection to SNP process as global variable
int network_conn; 
//declare server socket as a global variable, for re-use (subsequent SNP reconnections)
int tcpserv_sd;
struct sockaddr_in tcpserv_addr;


/**************************************************************/
//implementation overlay functions
/**************************************************************/

// This thread opens a TCP port on CONNECTION_PORT and waits for the incoming connection from all the neighbors that have a larger node ID than my nodeID,
// After all the incoming connections are established, this thread terminates 
void* waitNbrs(void* arg) {

	/* Determine how many neighbors I need to wait for */
	
	int nNum = topology_getNbrNum();
	int myID = topology_getMyNodeID();
	int cNum = 0;
	int i;

	for (i = 0; i < nNum; i++) {

		if (nt[i].nodeID > myID) { // if neighbor nodeID > than my ID, then increment cNum
			cNum += 1;
		}
	}

	/* If all neighbor nodeIDs are larger than my ID, then no need to wait */

	if (0 == cNum) {
		return NULL;
	}


	/* Wait for incoming connections until all incoming connections have been established */

	int tcpserv_sd;
	struct sockaddr_in tcpserv_addr;
	int connection;
	struct sockaddr_in tcpclient_addr;
	socklen_t tcpclient_addr_len;

	tcpserv_sd = socket(AF_INET, SOCK_STREAM, 0); 
	if(tcpserv_sd<0) { 
		fprintf(stderr, "Failed to initialize socket inside wait nbrs!\n");
		return NULL;
	}

	// /* Use to prevent the bind error */
	// int yes=1;
	// //char yes='1'; // use this under Solaris

	// if (setsockopt(tcpserv_sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
	//     perror("setsockopt");
	//     exit(1);
	// }

	memset(&tcpserv_addr, 0, sizeof(tcpserv_addr));
	tcpserv_addr.sin_family = AF_INET;
	tcpserv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcpserv_addr.sin_port = htons(CONNECTION_PORT);

	if(bind(tcpserv_sd, (struct sockaddr *)&tcpserv_addr, sizeof(tcpserv_addr))< 0) {
		fprintf(stderr, "Failed to bind! Exiting out of wait nbrs!\n");
		return NULL; 
	}
	if(listen(tcpserv_sd, 1) < 0) {
		fprintf(stderr, "Failed to listen! Exiting out of wait nbrs!\n");
		return NULL;
	}

	printf("waiting for connections!\n");

	/* Keep looping until connectNum == cNum */

	int connectionNum = 0;

	while (1) {
	
		if (0 > (connection = accept(tcpserv_sd,(struct sockaddr*)&tcpclient_addr,&tcpclient_addr_len))) {
			fprintf(stderr, "Failed connection!\n");
			return NULL;
		}

		/* Increment number of current connections */

		connectionNum += 1;

		in_addr_t connectNode = tcpclient_addr.sin_addr.s_addr;

		char srcIp[25];

		/* Convert ip address from in_addr_t to char */

		inet_ntop(AF_INET, &connectNode, srcIp, 25);

		/* Get the matching nodeID of the connected overlay */

		int connID = determineIdFromAddr(connectNode);
		if (connID == -1) {
			fprintf(stderr, "No appropriate matching neighbor found\n");
			break;
		}

		/* Add the newly established connection to neighbor table */

		fprintf(stdout, "\nNode: %d OR %s just connected to me\n", connID, srcIp);
		nt_addconn(nt, connID, connection);

		if (connectionNum == cNum) { // if finished waiting for neighbors to connect, break out of while loop
			break;
		}
	}

	return NULL;
}

/*
 * 
 * Function that determines the matching node ID from the neighbor table given an ip address of type in_addr_t
 *
 */
int determineIdFromAddr(in_addr_t address) {

	int oID = -1;
	int nNum = topology_getNbrNum();

	/* Determine which node connected */

	int j;
	for (j = 0; j < nNum; j++) {

		if (address == nt[j].nodeIP) {
			oID = nt[j].nodeID;
			return oID;
		}
	}

	return oID;
}



// This function connects to all the neighbors that have a smaller node ID than my nodeID
// After all the outgoing connections are established, return 1, otherwise return -1
int connectNbrs() {

	int nNum = topology_getNbrNum();
	int myID = topology_getMyNodeID();

	/* Iterate through all the neighbors */

	int i;
	for (i = 0; i < nNum; i++) {

		/* If the neighbor has a smaller nodeID than my nodeID */

		if (myID > nt[i].nodeID) {

			/* Create tcp connection between host and its neighbor */

			int out_conn;
			struct sockaddr_in servaddr;

		    servaddr.sin_family = AF_INET;
		    servaddr.sin_addr.s_addr = nt[i].nodeIP; //inet_addr(ip);
			servaddr.sin_port = htons(CONNECTION_PORT);

			out_conn = socket(AF_INET,SOCK_STREAM,0);  
			
			if(out_conn<0) {
				fprintf(stderr, "Overlay socket creation failed!\n");
				return -1;
			}
			
			if(connect(out_conn, (struct sockaddr*)&servaddr, sizeof(servaddr))<0) {
				fprintf(stderr, "Overlay failed to connect to neighbor!\n");
				return -1;
			}

			/* Add the tcp connection to the neighbor table */

			fprintf(stdout, "\nI just connected to Node: %d\n", nt[i].nodeID);
			nt_addconn(nt, nt[i].nodeID, out_conn);
		}
	}

  	return 1;
}

//Each listen_to_neighbor thread keeps receiving packets from a neighbor. It handles the received packets by forwarding the packets to the SNP process.
//all listen_to_neighbor threads are started after all the TCP connections to the neighbors are established 
void* listen_to_neighbor(void* arg) {

	/* Cast arg from pter to int */

    int idx = * (int *) arg; // corresponds to an index in the neighbor table array
    int connection = -1;
    int neighborID = -1;

    if (NULL != nt) {
    	connection = nt[idx].conn;
    	neighborID = nt[idx].nodeID;
    }

    else {
    	fprintf(stderr, "Neighbor table has been freed!\n");
    	return NULL;
    }

	snp_pkt_t* pkt = calloc (1, sizeof(snp_pkt_t));
	if (NULL == pkt) {
		free(pkt);
		fprintf(stderr, "Failed to allocate memory for snp_pkt_t\n");
		return NULL;
	} 

    while (1) {

		if (0 > recvpkt(pkt, connection)) { /* Receive failed */
			fprintf(stderr, "\nOverlay failed to receive from overlay neighbor: %d, now closing that connection!\n", neighborID);
			break;
		}

		/* Packet received from Overlay network neighbor */

		else {

			fprintf(stdout, "\nOverlay received packet from Node: %d\n", pkt->header.src_nodeID);

			/* Sending to SNP failed */
			if (0 > forwardpktToSNP(pkt, network_conn)) {
				fprintf(stderr, "\nOverlay failed to send to SNP (SNP process inactive)!\n");
			}
		}

		free(pkt);

		/* Allocate memory for new segPtr to receive a segment from a client */

		pkt = calloc (1, sizeof(snp_pkt_t));
		if (NULL == pkt) {
			free(pkt);
			fprintf(stderr, "Failed to allocate memory for snp_pkt_t\n");
			return NULL;
		}
    }

	close (nt[idx].conn);    
	nt[idx].conn = -1; /* Set the conn = -1 so that I prevent access to this socket */
	return 0;
}

//This function opens a TCP port on OVERLAY_PORT
//and waits for the incoming connection from local SNP process. 
//After the local SNP process is connected, this function keeps getting sendpkt_arg_ts from SNP process
//and sends the packets to the next hop in the overlay network. 
//If the next hop's nodeID is BROADCAST_NODEID, the packet should be sent to all the neighboring nodes.
void waitNetwork(int iteration) {

	/* Open a TCP port on OVELAY_PORT, wait for SNP process to connect */

	int connection;
	struct sockaddr_in tcpclient_addr;
	socklen_t tcpclient_addr_len;

	/* Only create a new tcp socket and call bind and listen when it's the first time the SNP connects */

	if (0 == iteration) {

		tcpserv_sd = socket(AF_INET, SOCK_STREAM, 0); 
		if(tcpserv_sd<0) {
			fprintf(stderr, "Socket creation on Overlay failed\n");
			overlay_stop();
		}

		/* Use to prevent the bind error */
/*		int yes=1;
		//char yes='1'; // use this under Solaris

		if (setsockopt(tcpserv_sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		    perror("setsockopt");
		    exit(1);
		}*/

		memset(&tcpserv_addr, 0, sizeof(tcpserv_addr));
		tcpserv_addr.sin_family = AF_INET;
		tcpserv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		tcpserv_addr.sin_port = htons(OVERLAY_PORT);

		if(bind(tcpserv_sd, (struct sockaddr *)&tcpserv_addr, sizeof(tcpserv_addr))< 0) {
			fprintf(stderr, "Bind on Overlay failed\n");
			overlay_stop(); 
		}
		if(listen(tcpserv_sd, 1) < 0) {
			fprintf(stderr, "Listen on Overlay failed\n");
			overlay_stop();
		}
	}

	if (0 > (connection = accept(tcpserv_sd,(struct sockaddr*)&tcpclient_addr,&tcpclient_addr_len))) {
		fprintf(stderr, "Failed to connect to SNP process!\n");
		overlay_stop();
	}

	fprintf(stdout, "\nSNP process connected!\n");
	
	/* Connection succeeded, assign network_conn to the returned socket file descriptor */

	network_conn = connection;

	/* After local SNP process is connected, get sendpkt_arg_t's from SNP and send to overlay network */

	/* snp_pkt_t struct to receive messages from SNP */

	snp_pkt_t* pkt = calloc (1, sizeof(snp_pkt_t));

	int nextNode;
	int nNum = topology_getNbrNum();
	int myID = topology_getMyNodeID();

	if (NULL == pkt) {
		free(pkt);
		fprintf(stderr, "Failed to allocate memory for snp_pkt_t\n");
		overlay_stop();
	} 

	while (1) {
	
		if (0 > getpktToSend(pkt, &nextNode, network_conn)) { /* Receive failed */
			fprintf(stderr, "\nFailed to receive from SNP, going back to listening mode...\n");
			close (network_conn); // close the socket connected to the client
			return;
		}

		/* Packet received from SNP */

		else {

			if (BROADCAST_NODEID == nextNode) {

				/* Broadcast the received packet to each neighbor within overlay network */

				fprintf(stdout, "\nBROADCASTING from Node %d to all neighbors!\n", myID);

				if (NULL != nt) {

					int i;
					for (i = 0; i < nNum; i++) {

						if ((0 > nt[i].conn) || (0 > sendpkt(pkt, nt[i].conn))) { /* In case one/multiple overlay nodes are offline */
							fprintf(stdout, "Failed to broadcast to Node: %d\n", nt[i].nodeID);
							//close(nt[i].conn);
						}

						else {
							fprintf(stdout, "Broadcasted to Node: %d\n", nt[i].nodeID);
						}
					}
				}

				else {
					fprintf(stderr, "Neighbor table has been deallocated!\n");
				}
			}

			/* Else forward the packet to the correct next hop neighbor */

			else {

				fprintf(stdout, "\nForwarding from Node %d to neighbor %d!\n", myID, nextNode);

				if (NULL != nt) {

					int i;
					for (i = 0; i < nNum; i++) {

						if (nt[i].nodeID == nextNode) {

							if ((0 > nt[i].conn) || (0 > sendpkt(pkt, nt[i].conn))) { /* In case one/multiple overlay nodes are offline */
								fprintf(stdout, "Failed to forward to Node: %d\n", nt[i].nodeID);
								//close(nt[i].conn);
							}

							else {
								fprintf(stdout, "Forwarded to Node: %d\n", nt[i].nodeID);
							}
						}
					}
				}

				else {
					fprintf(stderr, "Neighbor table has been deallocated!\n");
				}
			}

			free(pkt);

			/* Allocate memory for new segPtr to receive a segment from a client */

			pkt = calloc (1, sizeof(snp_pkt_t));
			if (NULL == pkt) {
				free(pkt);
				fprintf(stderr, "Failed to allocate memory for snp_pkt_t\n");
				overlay_stop();
			}
		}
	}
}

//this function stops the overlay
//it closes all the connections and frees all the dynamically allocated memory
//it is called when receiving a signal SIGINT
void overlay_stop() {

	fprintf(stdout, "Received SIGINT from user, closing the Overlay!\n");
	nt_destroy(nt);
	exit(1);
}

int main() {

	//start overlay initialization
	printf("Overlay: Node %d initializing...\n",topology_getMyNodeID());	

	//create a neighbor table
	nt = nt_create();
	//initialize network_conn to -1, means no SNP process is connected yet
	network_conn = -1;
	
	//register a signal handler which is sued to terminate the process
	signal(SIGINT, overlay_stop);

	//print out all the neighbors
	int nbrNum = topology_getNbrNum();
	int i;
	for(i=0;i<nbrNum;i++) {
		printf("Overlay: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//start the waitNbrs thread to wait for incoming connections from neighbors with larger node IDs
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//wait for other nodes to start
	sleep(OVERLAY_START_DELAY);
	
	//connect to neighbors with smaller node IDs
	if (0 > connectNbrs()) {
		fprintf(stderr, "Failed to connect with neighbor(s)! Now exiting program!\n");
		exit(-1);
	}

	//wait for waitNbrs thread to return
	pthread_join(waitNbrs_thread,NULL);	

	//at this point, all connections to the neighbors are created
	
	//create threads listening to all the neighbors
	for(i=0;i<nbrNum;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("\nOverlay: node initialized...\n");
	printf("\nOverlay: waiting for connection from SNP process...\n");

	//waiting for connection from  SNP process
	i = 0;

	while (1) {
		waitNetwork(i);
		i++;
	}
}