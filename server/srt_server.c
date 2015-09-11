// FILE: srt_server.c
//
// Description: this file contains server states' definition, some important
// data structures and the server SRT socket interface definitions. You need 
// to implement all these interfaces
//
// Date: April 18, 2008
//       April 21, 2008 **Added more detailed description of prototypes fixed ambiguities** ATC
//       April 26, 2008 **Added GBN descriptions
//
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "srt_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"
#include <sys/time.h>


//
//
//  SRT socket API for the server side application. 
//  ===================================
//
//  In what follows, we provide the prototype definition for each call and limited pseudo code representation
//  of the function. This is not meant to be comprehensive - more a guideline. 
// 
//  You are free to design the code as you wish.
//
//  NOTE: When designing all functions you should consider all possible states of the FSM using
//  a switch statement (see the Lab3 assignment for an example). Typically, the FSM has to be
// in a certain state determined by the design of the FSM to carry out a certain action. 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

// This function initializes the TCB table marking all entries NULL. It also initializes 
// a global variable for the overlay TCP socket descriptor ``conn'' used as input parameter
// for snp_sendseg and snp_recvseg. Finally, the function starts the seghandler thread to 
// handle the incoming segments. There is only one seghandler for the server side which
// handles call connections for the client.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void srt_server_init(int conn)
{

	/* No need to initialize the TCB table, already initialized in the header file as a global array */

	svrSockGlobal = conn;

	fprintf(stdout, "Initialized global socket: %d\n", svrSockGlobal);

	pthread_t segMain;

	int segRC = pthread_create(&segMain, NULL, seghandler, NULL); /* Params: fourth param is arg to function */

	if (segRC) {
	    fprintf(stderr, "pthread_create failed for server, rc=%d\n", segRC);
	    exit(segRC);
	}

	else {

		fprintf(stdout, "Successful thread creation for server! rc= %d\n\n", segRC);
	}

	return;
}


// This function looks up the client TCB table to find the first NULL entry, and creates
// a new TCB entry using malloc() for that entry. All fields in the TCB are initialized 
// e.g., TCB state is set to CLOSED and the server port set to the function call parameter 
// server port.  The TCB table entry index should be returned as the new socket ID to the server 
// and be used to identify the connection on the server side. If no entry in the TCB table  
// is available the function returns -1.

//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_sock(unsigned int port)
{

	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) {

		/* Find the first null entry in the svrConnList and allocate memory appropriately */

		if (NULL == svrConnList[i]) {

			svrConnList[i] = calloc (1, sizeof(svr_tcb_t));

			/* If allocation failed */

	        if (NULL == svrConnList[i]) {
	            free(svrConnList[i]);
	            fprintf(stderr, "Error: failed to allocate memory for initial creation of tcb struct.");
	            return -1;
	        } 

			/* Initialize state to CLOSED, and set svr_portNum */

			svrConnList[i]->svr_portNum = port;

			svrConnList[i]->svr_nodeID = topology_getMyNodeID();

			svrConnList[i]->state = CLOSED; // state value is 1

			fprintf(stdout, "Initialized new server-client entry at index %d, for server port %d\n", i, port);

			/* Dynamically initialize a mutex struct */

			svrConnList[i]->bufMutex = calloc (1, sizeof(pthread_mutex_t));

			if (pthread_mutex_init(svrConnList[i]->bufMutex, NULL) != 0)
		    {
		        printf("Mutex init failed\n");
		        return -1;
		    }

			/* Dynamically allocate recvBuf */

			svrConnList[i]->recvBuf = calloc (RECEIVE_BUF_SIZE, sizeof(char));

	        if (NULL == svrConnList[i]->recvBuf) {
	            free(svrConnList[i]->recvBuf);
	            fprintf(stderr, "Error: failed to allocate memory for initial creation of buffer struct.");
	            return -1;
	        } 

			return i;
		}
	}

	/* If the list is full (all non-null entries), then return -1 */

	return -1;
}


// This function gets the TCB pointer using the sockfd and changes the state of the connection to 
// LISTENING. It then starts a timer to ``busy wait'' until the TCB's state changes to CONNECTED 
// (seghandler does this when a SYN is received). It waits in an infinite loop for the state 
// transition before proceeding and to return 1 when the state change happens, dropping out of
// the busy wait loop. You can implement this blocking wait in different ways, if you wish.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_accept(int sockfd)
{

	/* sockfd = index in svrConnList corresponding to the appropriate tcb entry */

	svr_tcb_t *tp = svrConnList[sockfd];
	
	if (NULL == tp) {

		fprintf(stdout, "No entry in svrConnList\n");
		return -1;
	}

	tp->state = LISTENING;

	/* One busy wait loop, waiting for CONNECTED state */

	while (1) { //elapsedTime < (BUSY_WAIT_MS * 1.0)) {

		switch(tp->state) {

			case LISTENING:
				break;

			case CONNECTED: /* If receive SYN, drop out of the loop, leave packet sending to seghandler */
				fprintf(stdout, "Server just connected at serverport: %d\n\n", tp->svr_portNum);
				return 1;
		}
	}

	return -1;
}


// Receive data from a srt client. Recall this is a unidirectional transport
// where DATA flows from the client to the server. Signaling/control messages
// such as SYN, SYNACK, etc.flow in both directions. 
// This function keeps polling the receive buffer every RECVBUF_POLLING_INTERVAL
// until the requested data is available, then it stores the data and returns 1
// If the function fails, return -1 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_recv(int sockfd, void* buf, unsigned int length)
{

	/* Determine the TCP entry */

	svr_tcb_t* tp = svrConnList[sockfd];
	
	if (NULL == tp) {

		fprintf(stdout, "No entry in connectionList\n");
		return -1;
	}

	while (1) {

		/* If enough data in recv buffer, then remove from recv buffer and store in buf */

		if (CONNECTED != tp->state) {

			fprintf(stdout, "\nBreaking out of srt_recv loop, server no longer connected\n");
			return -1;
		}

		if (tp->usedBufLen >= length) {

			/* Lock the server_tcb before modifying the recv buffer */

			fprintf(stdout, "\nServer has accumulated enough data inside the Receive Buffer!\n\n");

			pthread_mutex_lock(tp->bufMutex);

			memcpy(buf, tp->recvBuf, length);

			/* Copy over the rest of the recv buffer onto its first half */

			char* receiveBuf = tp->recvBuf;

			int bytesLeft = tp->usedBufLen - length;

			memmove(tp->recvBuf, receiveBuf + length, bytesLeft);

			/* Update used buffer length */

			tp->usedBufLen -= length;

			pthread_mutex_unlock(tp->bufMutex);

			return 1;
		}

		sleep(RECVBUF_POLLING_INTERVAL);
	}

	return 1;
}


// This function calls free() to free the TCB entry. It marks that entry in TCB as NULL
// and returns 1 if succeeded (i.e., was in the right state to complete a close) and -1 
// if fails (i.e., in the wrong state).
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int srt_server_close(int sockfd)
{

	/* When the server TCB’s state transitions to CLOSED after CLOSEWAIT_TIMEOUT, the receive buffer is cleared
	by setting its usedBufLen as 0. When the server TCB is freed when srt_svr_close() is called, the mutex and 
	receive buffer is freed. */

	svr_tcb_t *tp = svrConnList[sockfd];
	
	if (NULL == tp) {

		fprintf(stdout, "No entry in svrConnList, invalid close\n");
		return -1;
	}

	/* Save the current state of the client */

	int svrState = tp->state;

	/* Set usedBufLen as 0, and free receive buffer and mutex */

	tp->usedBufLen = 0;

	free(svrConnList[sockfd]->recvBuf);

	if (pthread_mutex_destroy(svrConnList[sockfd]->bufMutex) != 0)
    {
        printf("Mutex destroy failed\n");
        return -1;
    }

	free(svrConnList[sockfd]->bufMutex);

	/* Free the TCB entry and set the pointer to NULL */

	free(svrConnList[sockfd]);
	svrConnList[sockfd] = NULL;

	/* Wrong state for closing */

	if (svrState != CLOSED) {

		return -1;
	}

	else {

		return 1;
	}
}

void* serverTimer(void* servertcb)
{
	
	sleep(CLOSEWAIT_TIMEOUT);

    svr_tcb_t* sTp = (svr_tcb_t *) servertcb; /* Cast the void* to our struct type */

	fprintf(stdout, "\nNow closing server port %d after CLOSEWAIT_TIME\n", sTp->svr_portNum);

	/* set state of that server port to CLOSED */

	sTp->state = CLOSED;
}


// This is a thread  started by srt_server_init(). It handles all the incoming 
// segments from the client. The design of seghandler is an infinite loop that calls snp_recvseg(). If
// snp_recvseg() fails then the overlay connection is closed and the thread is terminated. Depending
// on the state of the connection when a segment is received  (based on the incoming segment) various
// actions are taken. See the client FSM for more details.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void* seghandler(void* arg)
{

	/* seg_t struct to receive messages from client */

	seg_t* segPtr = calloc (1, sizeof(seg_t));

	if (NULL == segPtr) {

		free(segPtr);
		fprintf(stderr, "Failed to allocate memory for segment\n");
		exit(1);
	} 

	/* Infinite loop that receives segments */

	int src_nodeID;

	while (1) {
	
		/* For timing from CLOSEWAIT to CLOSED for server-client connection, loop through all connections */

		/* If Timer has already been started, then check for timeout */

		// int j;

		// for (j = 0; j < MAX_TRANSPORT_CONNECTIONS; j++) {

		// 	svr_tcb_t* sTp = svrConnList[j];

		// 	if ((NULL != sTp) && (sTp->timeSetBool) && (CLOSED != sTp->state)) { /* Don't want to track time if client connection already closed */

		// 		gettimeofday(&(sTp->t2), NULL); // keep checking the time

		// 		/* compute the elapsed time in milliseconds */
		// 	    sTp->elapsedTime = (sTp->t2.tv_sec - sTp->t1.tv_sec) * 1000.0;      // sec to ms
		// 	    sTp->elapsedTime += (sTp->t2.tv_usec - sTp->t1.tv_usec) / 1000.0;   // us to ms


		// 		//fprintf(stdout, "\nElapsed time: %lf\n", sTp->elapsedTime);
			
				
		// 		/* CLOSE after CLOSEWAIT period */
		// 		if (sTp->elapsedTime > (CLOSEWAIT_TIMEOUT * 1000.0)) {

		// 			fprintf(stdout, "\nNow closing server port %d after CLOSEWAIT_TIME: %f MS\n", sTp->svr_portNum, sTp->elapsedTime);

		// 			/* set state of that server port to CLOSED */

		// 			sTp->state = CLOSED;
		// 		}
		// 	}
		// }

		if (0 > snp_recvseg(svrSockGlobal, &src_nodeID, segPtr)) { /* Receive failed */
			;
		}

		/* Packet received */

		else {

			/* check the packet for bit errors */

			if (0 < checkchecksum(segPtr)) { // If the packet is valid

				/* Determine the serverPort it's meant for */

				int svrPort = segPtr->header.dest_port;
				int clientPort = segPtr->header.src_port;

				/* Determine what entry in the TCB table that serverPort corresponds to */

				int i;

				svr_tcb_t *tp;
				
				for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) {

					if ((NULL != svrConnList[i]) && (svrPort == svrConnList[i]->svr_portNum)) {

						tp = svrConnList[i];
						tp->client_portNum = clientPort;
						break;
					}
				}

				if (NULL == tp) {
					fprintf(stderr, "Bad packet, found no matching port number on server side\n");
					exit(1);
				}

				/* Switch on the type of packet */

				switch(segPtr->header.type) {

					case SYN: /* Update server state and send a SYNACK */

						fprintf(stdout, "Server at server port: %d received SYN from client NodeID: %d at port: %d\n", svrPort, src_nodeID, clientPort);

						if ((LISTENING == tp->state) || (CONNECTED == tp->state)) {

							tp->state = CONNECTED;

							tp->client_nodeID = src_nodeID;

							/* Set tp->expect_seqNum using the sequence number in the received SYN segment */

							tp->expect_seqNum = segPtr->header.seq_num;

							/* Setup a SYNACK segment and send the segment to the server */

							seg_t* segSYNACK = calloc (1, sizeof(seg_t));

							if (NULL == segSYNACK) {

								free(segSYNACK);
								fprintf(stderr, "Failed to allocate memory for SYNACK segment\n");
								exit(1);
							} 

							segSYNACK->header.type = SYNACK;

							segSYNACK->header.src_port = svrPort;

							segSYNACK->header.dest_port = clientPort;

							/* Generate a checksum and add to PacketHeader */

							unsigned short int crcSumSYNACK = checksum(segSYNACK);

							segSYNACK->header.checksum = crcSumSYNACK;

							if (0 > snp_sendseg(svrSockGlobal, tp->client_nodeID, segSYNACK)) {
								fprintf(stderr, "Overlay send failed\n");
								exit(1);
							}

							free(segSYNACK);
						}

						break;

					case FIN: /* Stay at CLOSEWAIT and keep sending FINACKs */

						/* If currently CONNECTED or in CLOSEWAIT, stay/transition to CLOSEWAIT, and send FINACK */

						fprintf(stdout, "\nServer at server port: %d received FIN from client NodeID: %d at port: %d\n", svrPort, src_nodeID, clientPort);

						if ((CONNECTED == tp->state) || (CLOSEWAIT == tp->state)) {

							/* If originally CONNECTED, then start timer for this server-client port connection */

							if (CONNECTED == tp->state) {

/*								gettimeofday(&(tp->t1), NULL);
								tp->timeSetBool = 1; // indicate that timer has been started*/

								pthread_t segMain;

								int segRC = pthread_create(&segMain, NULL, serverTimer, tp); /* Params: fourth param is arg to function */

								if (segRC) {
								    fprintf(stderr, "pthread_create failed for server, rc=%d\n", segRC);
								    exit(segRC);
								}

								else {
									fprintf(stdout, "Starting timer for CLOSE_WAIT for server port: %d\n", svrPort);
								}
							}

							tp->state = CLOSEWAIT;

							/* Setup a FINACK segment and send the segment to the server */

							seg_t* segFINACK = calloc (1, sizeof(seg_t));

							if (NULL == segFINACK) {

								free(segFINACK);
								fprintf(stderr, "Failed to allocate memory for FINACK segment\n");
								exit(1);
							} 

							segFINACK->header.type = FINACK;

							segFINACK->header.src_port = svrPort;

							segFINACK->header.dest_port = clientPort;

							/* Generate a checksum and add to PacketHeader */

							unsigned short int crcSumFINACK = checksum(segFINACK);

							segFINACK->header.checksum = crcSumFINACK;

							if (0 > snp_sendseg(svrSockGlobal, tp->client_nodeID, segFINACK)) {
								fprintf(stderr, "Overlay send failed\n");
								exit(1);
							}

							free(segFINACK);
						}

						break;


					case DATA:

						fprintf(stdout, "Server at server port: %d received DATA from client NodeID: %d at port: %d\n", svrPort, src_nodeID, clientPort);

						if (CONNECTED == tp->state) {

							/* If the sequence number of the segment matches server tcb's expected sequence number */

							if (tp->expect_seqNum == segPtr->header.seq_num) {

								pthread_mutex_lock(tp->bufMutex);

								/* If receive buffer can still accommodate for received DATA segment */

								if ((tp->usedBufLen + segPtr->header.length) < RECEIVE_BUF_SIZE) {

									/* Store the received data into the receive buffer */

									char * receiveBuffer = tp->recvBuf;

									unsigned int usedBufferLen = tp->usedBufLen;

									memcpy(receiveBuffer + usedBufferLen, segPtr->data, segPtr->header.length);

									/* Increment usedBufLen and expect_seqNum by data size of received DATA segment */

									tp->expect_seqNum += segPtr->header.length;

									tp->usedBufLen += segPtr->header.length;

								}

								else { /* Simply discard received DATA segment */

									fprintf(stdout, "Receive buffer storage exceeded!!!\n");
								}

								pthread_mutex_unlock(tp->bufMutex);

							}

							/* Send DATAACK back to client w/ updated expect_seqNum or old expect_seqNum */
							
							seg_t* segDATAACK = calloc (1, sizeof(seg_t));

							if (NULL == segDATAACK) {

								free(segDATAACK);
								fprintf(stderr, "Failed to allocate memory for DATAACK segment\n");
								exit(1);
							} 

							segDATAACK->header.type = DATAACK;

							segDATAACK->header.src_port = svrPort;

							segDATAACK->header.dest_port = clientPort;

							segDATAACK->header.ack_num = tp->expect_seqNum;

							/* Generate a checksum and add to Packet Header */

							unsigned short int crcSumDATAACK = checksum(segDATAACK);

							segDATAACK->header.checksum = crcSumDATAACK;

							if (0 > snp_sendseg(svrSockGlobal, tp->client_nodeID, segDATAACK)) {
								fprintf(stderr, "Overlay send failed\n");
								exit(1);
							}

							free(segDATAACK);
						}

						else {

							fprintf(stdout, "BAD RECEIVING STATE, NOT CONNECTED\n");
						}

						break;
				}

				free(segPtr);

				/* Allocate memory for new segPtr to receive a segment from a client */

				segPtr = calloc (1, sizeof(seg_t));

				if (NULL == segPtr) {

					free(segPtr);
					fprintf(stderr, "Failed to allocate memory for segment\n");
					exit(1);
				}
			}

			else {

				/* Packet contains a bit error, so free it and drop the packet */

				fprintf(stdout, "Packet bit corruption detected!\n");

				free (segPtr);

				segPtr = calloc (1, sizeof(seg_t));

				if (NULL == segPtr) {
					free(segPtr);
					fprintf(stderr, "Failed to allocate memory for segment\n");
					exit(1);
				}
			}
		}
	}

  	return 0;
}

