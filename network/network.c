// File: network/network.c
//
// Description: Implements the network layer process  
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "network.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//network layer waits this time for establishing the routing paths 
#define NETWORK_WAITTIME 20

/**************************************************************/
//delare global variables
/**************************************************************/
int overlay_conn; 			//connection to the overlay
int transport_conn;			//connection to the transport
nbr_cost_entry_t* nct;			//neighbor cost table
dv_t* dv;				//distance vector table
pthread_mutex_t* dv_mutex;		//dvtable mutex
routingtable_t* routingtable;		//routing table
pthread_mutex_t* routingtable_mutex;	//routingtable mutex

//declare server socket as a global variable, for re-use (subsequent SRT reconnections)
int tcpserv_sd;
struct sockaddr_in tcpserv_addr;

/**************************************************************/
//implementation network layer functions
/**************************************************************/

//This function is used to for the SNP process to connect to the local ON process on port OVERLAY_PORT.
//TCP descriptor is returned if success, otherwise return -1.
int connectToOverlay() { 

	/* Create tcp connection between myself (SNP) and the Overlay on OVERLAY_PORT */

	int out_conn;
	struct sockaddr_in servaddr;

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // connect to localhost
	servaddr.sin_port = htons(OVERLAY_PORT);

	out_conn = socket(AF_INET,SOCK_STREAM,0);  
	
	if(out_conn<0) {
		return -1;
	}
	
	if(connect(out_conn, (struct sockaddr*)&servaddr, sizeof(servaddr))<0) {
		return -1;
	}

	/* Add the tcp connection to the neighbor table */

	fprintf(stdout, "\n\nSNP just connected to Overlay!\n");
	return out_conn;
}

//This thread sends out route update packets every ROUTEUPDATE_INTERVAL time
//The route update packet contains this node's distance vector. 
//Broadcasting is done by set the dest_nodeID in packet header as BROADCAST_NODEID
//and use overlay_sendpkt() to send the packet out using BROADCAST_NODEID address.
void* routeupdate_daemon(void* arg) {

	int nNum = topology_getNbrNum();

	sleep(ROUTEUPDATE_INTERVAL);

	/* Broadcast route update packets every ROUTEUPDATE_INTERVAL time */

	while (1) {

		/* Prepare the packet */

		snp_pkt_t pkt; // declare
		int myID = topology_getMyNodeID();

		if (-1 == myID) {
			fprintf(stderr, "Failed to get my node ID from within routeupdate_daemon! Exiting!\n");
			network_stop();
		}

		pkt.header.src_nodeID = myID;
		pkt.header.dest_nodeID = BROADCAST_NODEID;
		pkt.header.type = ROUTE_UPDATE;

		/* Copy distance vector of source node into rtPacket and copy into data field of snp_pkt_t */

		pkt_routeupdate_t rtPacket;
		rtPacket.entryNum = topology_getNodeNum(); // should have 4 entries total (one for each node in the network)

		/* Find the distance vector pertaining to the host, loop through each row in the dv table */

		int i;

		for (i = 0; i < nNum + 1; i++) {

			if (myID == dv[i].nodeID) { 
				/* Initialize 4 entries */

				int j;

				for (j = 0; j < rtPacket.entryNum; j++) {
					rtPacket.entry[j].nodeID = dv[i].dvEntry[j].nodeID; /* Set destination node */
					rtPacket.entry[j].cost = dv[i].dvEntry[j].cost; /* Set cost between source and destination */
				}
			}
		}

		/* Copy this distance vector into pkt data field */

		size_t routeLength = sizeof(unsigned int) + (rtPacket.entryNum * (2 * sizeof(unsigned int)));
		memcpy(pkt.data, &rtPacket, routeLength);
		pkt.header.length = routeLength;

		/* Send the packet to the Overlay through overlay_conn */

		if (0 > overlay_sendpkt(BROADCAST_NODEID, &pkt, overlay_conn)) { // For now initialize nextNodeID as BROADCAST_NODEID
			fprintf(stderr, "Failed to broadcast packet from routeupdate_daemon! Now closing SNP!\n");
			network_stop();
		}

		/* Print what you're sending */

		fprintf(stdout, "\nSending Route Packet from SNP process to Overlay\n");

		/* Sleep */

		sleep(ROUTEUPDATE_INTERVAL);
	}

	return 0;
}

//This thread handles incoming packets from the ON process.
//It receives packets from the ON process by calling overlay_recvpkt().
//If the packet is a SNP packet and the destination node is this node, forward the packet to the SRT process.
//If the packet is a SNP packet and the destination node is not this node, forward the packet to the next hop according to the routing table.
//If this packet is an Route Update packet, update the distance vector table and the routing table. 
void* pkthandler(void* arg) {

	int nNum = topology_getNbrNum();
	int myID = topology_getMyNodeID();
	snp_pkt_t pkt;

	while(overlay_recvpkt(&pkt,overlay_conn)>0) { /* Keep receiving packets from overlay */

		if (SNP == pkt.header.type) {

			printf("\nRouting: SNP received an SNP packet from Node %d\n", pkt.header.src_nodeID);

			/* Either forward the packet or keep it */

			if (myID == pkt.header.dest_nodeID) {

				seg_t * segPtr = calloc(1, sizeof(seg_t));
				if (NULL == segPtr) {
					fprintf(stderr, "Failed to allocate memory for segPtr inside pkthandler\n");
					free(segPtr);
					network_stop();
				}

				fprintf(stdout, "\nPacket reached destination! SNP now forwarding packet to local SRT process!\n");

				memcpy(segPtr, pkt.data, pkt.header.length);

/*				fprintf(stdout, "SNP received segment, checksum in header: %d\n", segPtr.header.checksum);
*/
				if (0 > forwardsegToSRT(transport_conn, pkt.header.src_nodeID, segPtr)) {
					fprintf(stderr, "Failed to send to SRT! SRT is inactive...\n");
				}

				free(segPtr);
			}

			else { // forward the packet

				int destinationNodeID = pkt.header.dest_nodeID;
				int nextHopNode;

				if (0 > (nextHopNode = routingtable_getnextnode(routingtable, destinationNodeID))) {
					fprintf(stdout, "Can't forward packet, not in Routing Table! Packet meant for Node %d dropped!\n", destinationNodeID);
				}

				else {

					/* Send the packet to the Overlay through overlay_conn */

					if (0 > overlay_sendpkt(nextHopNode, &pkt, overlay_conn)) {
						fprintf(stderr, "Failed to send packet from SNP to overlay! Now closing SNP!\n");
						network_stop();
					}

					/* Print what you're sending */

					fprintf(stdout, "\nFowarding Packet from SNP, next Hop: %d\n", nextHopNode);
				}
			}
		}

		/* Update the distance vector table and the routing table */

		else {

			printf("\nRouting: SNP received a RouteUpdate packet from neighbor %d\n",pkt.header.src_nodeID);

			/* Step 1: Update Distance vector for S */

			int i;

			for (i = 0; i < nNum + 1; i++) {

				if (pkt.header.src_nodeID == dv[i].nodeID) { /* Found the corresponding dvector in the table */
					pthread_mutex_lock(dv_mutex);
					memcpy(dv[i].dvEntry, pkt.data + sizeof(unsigned int), pkt.header.length);
					pthread_mutex_unlock(dv_mutex);
				}
			}

			/* Step 2: Recalculate distance vector for X */

			int j;	/* For each destination node y, recalculate link cost */

			for (j = 0; j < topology_getNodeNum(); j++) { // do this for every column in x's distance vector

				int destID = dv[0].dvEntry[j].nodeID;
				int originalCost = dvtable_getcost(dv, myID, destID);
				int min = originalCost; // initialize as x's initial link cost

				int nextHopID = 0;

				int k;

				for (k = 1; k < nNum + 1; k++) { // iterate through each neighbor v

					// Dx(y) = min v { c(x, v) + D v (y)  } for each destination node y in network

					int distanceY = dvtable_getcost(dv, myID, dv[k].nodeID) + dvtable_getcost(dv, dv[k].nodeID, destID);

					if (distanceY < min) { // if found a better route to destination node Y, update X's distance vector

						// Update min
						min = distanceY;

						// keep track of nextHopID
						nextHopID = dv[k].nodeID;

						pthread_mutex_lock(dv_mutex);

						if (0 > dvtable_setcost(dv, myID, destID, distanceY)) {
							fprintf(stderr, "Couldn't set cost in distance vector table!\n");
						}

						else {
							fprintf(stdout, "Updated entry for dest node: %d in dv table\n", destID);
						}

						pthread_mutex_unlock(dv_mutex);
					}
				}

				/* If there's been a change in the link cost from myID to destID, then update routing table */

				if (originalCost != min) {
					fprintf(stdout, "Updating routing table! Setting DestNode %d Nexthop as %d!\n", destID, nextHopID);
					pthread_mutex_lock(routingtable_mutex);
					routingtable_setnextnode(routingtable, destID, nextHopID);
					pthread_mutex_unlock(routingtable_mutex);
				}
			}
		}
	}

	close(overlay_conn);
	overlay_conn = -1;
	pthread_exit(NULL);
}

//This function stops the SNP process. 
//It closes all the connections and frees all the dynamically allocated memory.
//It is called when the SNP process receives a signal SIGINT.
void network_stop() {

	fprintf(stdout, "SNP received signal SIGINT, now closing process and freeing routing memory...\n");
	close(overlay_conn);

	/* Free structures for distance vector table */

	pthread_mutex_lock(dv_mutex);
	dvtable_destroy (dv);
	pthread_mutex_unlock(dv_mutex);

	/* Free the mutex */

	if (pthread_mutex_destroy(dv_mutex) != 0)
    {
        printf("Mutex destroy failed\n");
        exit(-1);
    }

	free(dv_mutex);
	dv_mutex = NULL;

	/* Free structures for routing table */

	pthread_mutex_lock(routingtable_mutex);
	routingtable_destroy(routingtable);
	pthread_mutex_unlock(routingtable_mutex);

	/* Free the mutex */

	if (pthread_mutex_destroy(routingtable_mutex) != 0)
    {
        printf("Mutex destroy failed\n");
        exit(-1);
    }

	free(routingtable_mutex);
	routingtable_mutex = NULL;
	exit(1);
}

//This function opens a port on NETWORK_PORT and waits for the TCP connection from local SRT process.
//After the local SRT process is connected, this function keeps receiving sendseg_arg_ts which contains the segments and their destination node addresses from the SRT process. The received segments are then encapsulated into packets (one segment in one packet), and sent to the next hop using overlay_sendpkt. The next hop is retrieved from routing table.
//When a local SRT process is disconnected, this function waits for the next SRT process to connect.
void waitTransport(int iteration) {

	/* Open a TCP port on NETWORK_PORT, wait for SRT process to connect */

	int connection;
	struct sockaddr_in tcpclient_addr;
	socklen_t tcpclient_addr_len;

	/* Only create a new tcp socket and call bind and listen when it's the first time the SNP connects */

	if (0 == iteration) {

		tcpserv_sd = socket(AF_INET, SOCK_STREAM, 0); 
		if(tcpserv_sd<0) {
			fprintf(stderr, "Socket creation on SNP failed\n");
			network_stop();
		}

		memset(&tcpserv_addr, 0, sizeof(tcpserv_addr));
		tcpserv_addr.sin_family = AF_INET;
		tcpserv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		tcpserv_addr.sin_port = htons(NETWORK_PORT);

		if(bind(tcpserv_sd, (struct sockaddr *)&tcpserv_addr, sizeof(tcpserv_addr))< 0) {
			fprintf(stderr, "Bind on SNP failed\n");
			network_stop(); 
		}
		if(listen(tcpserv_sd, 1) < 0) {
			fprintf(stderr, "Listen on SNP failed\n");
			network_stop();
		}
	}

	if (0 > (connection = accept(tcpserv_sd,(struct sockaddr*)&tcpclient_addr,&tcpclient_addr_len))) {
		fprintf(stderr, "Failed to connect to SRT process!\n");
		network_stop();
	}

	fprintf(stdout, "\nSRT process connected!\n");
	
	/* Connection succeeded, assign transport_conn to the returned socket file descriptor */

	transport_conn = connection;
	int dest_nodeID;
	int myID = topology_getMyNodeID();
	seg_t * segPtr;

	while (1) {

		/* seg_t struct to receive messages from SRT */

		segPtr = calloc (1, sizeof(seg_t));

		if (NULL == segPtr) {
			free(segPtr);
			fprintf(stderr, "Failed to allocate memory for seg_t\n");
			network_stop();
		} 

		if (0 > getsegToSend(transport_conn, &dest_nodeID, segPtr)) { /* Receive failed */
			fprintf(stderr, "\nFailed to receive from SRT, going back to listening mode...\n");
			close(transport_conn); // close the socket connected to the client
			return;
		}

		/* Packet received from SRT */

		else {

			/* Encapsulate the segment in a packet, and send to the next hop */

			snp_pkt_t pkt;

			pkt.header.src_nodeID = myID;
			pkt.header.dest_nodeID = dest_nodeID;
			pkt.header.type = SNP;
			pkt.header.length = sizeof(srt_hdr_t) + segPtr->header.length;

			memcpy(pkt.data, segPtr, sizeof(srt_hdr_t) + segPtr->header.length);

			// Double check that pkt.data is filled

			int nextNodeID; 

			if (0 > (nextNodeID = routingtable_getnextnode(routingtable, dest_nodeID))) { /* Can't route to dest node */
				fprintf(stdout, "Can't send packet, not in Routing Table! Packet for Node %d dropped inside WaitTransport!\n", dest_nodeID);
			}

			else { /* Found nextHop */

				if (0 > overlay_sendpkt(nextNodeID, &pkt, overlay_conn)) {
					fprintf(stderr, "Failed to send packet from waitTransport to Overlay! Now closing SNP!\n");
					close(overlay_conn);
					network_stop();
				}

				fprintf(stdout, "\nTransport: SNP received a packet from SRT, sent to Overlay: destNode: %d, nextHop: %d\n", dest_nodeID, nextNodeID);
			}

			free(segPtr);
		}
	}
}



int main(int argc, char *argv[]) {

	printf("network layer is starting, please wait...\n");

	//initialize global variables
	nct = nbrcosttable_create();
	dv = dvtable_create(nct); // originally no parameters
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	overlay_conn = -1;
	transport_conn = -1;

	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//register a signal handler which is used to terminate the process
	signal(SIGINT, network_stop);

	//connect to local ON process 
	overlay_conn = connectToOverlay();
	if(overlay_conn<0) {
		printf("\ncan't connect to overlay process\n");
		exit(1);		
	}
	
	//start a thread that handles incoming packets from ON process 
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//start a route update thread 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("network layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(NETWORK_WAITTIME);
	routingtable_print(routingtable);

	//wait connection from SRT process
	printf("\n\nwaiting for connection from SRT process\n");

	//waiting for connection from SRT process
	int i = 0;

	while (1) {
		waitTransport(i);
		i++;
	}
}