// File: client/srt_client.c
//
// Description: this file contains client states' definition, some important data structures
// and the client SRT socket interface definitions.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <assert.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "../topology/topology.h"
#include "srt_client.h"
#include "../common/seg.h"

//  SRT socket API for the client side application. 
//  ===================================

// This function initializes the TCB table marking all entries NULL. It also initializes 
// a global variable for the overlay TCP socket descriptor ``conn'' used as input parameter
// for snp_sendseg and snp_recvseg. Finally, the function starts the seghandler thread to 
// handle the incoming segments. There is only one seghandler for the client side which
// handles call connections for the client.
//
void srt_client_init(int conn)
{
	/* No need to initialize the TCB table, already initialized in the header file as a global array */

	sockConnGlobal = conn;

	fprintf(stdout, "Initialized global client socket: %d\n", sockConnGlobal);

	pthread_t segMaster;

	int segReturn = pthread_create(&segMaster, NULL, seghandler, NULL); /* Params: fourth param is arg to function */
	if (segReturn) {
	    fprintf(stderr, "pthread_create failed for client, rc=%d\n", segReturn);
	    exit(segReturn);
	}

	else {
		fprintf(stdout, "Successful thread creation for client! rc= %d\n\n", segReturn);
	}

  	return;
}

// This function looks up the client TCB table to find the first NULL entry, and creates
// a new TCB entry using malloc() for that entry. All fields in the TCB are initialized 
// e.g., TCB state is set to CLOSED and the client port set to the function call parameter 
// client port.  The TCB table entry index should be returned as the new socket ID to the client 
// and be used to identify the connection on the client side. If no entry in the TC table  
// is available the function returns -1.
//
int srt_client_sock(unsigned int client_port)
{
	/* 

	Each client TCB maintains a unique send buffer. 
	When a client TCB is initialized in srt_client_sock(), the send buffer is empty and a 
	mutex structure is dynamically created.
	
	*/
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) {

		/* Find the first null entry in the connectionList and allocate memory appropriately */

		if (NULL == connectionList[i]) {

			connectionList[i] = calloc (1, sizeof(client_tcb_t));
	        if (NULL == connectionList[i]) {
	            free(connectionList[i]);
	            fprintf(stderr, "Error: failed to allocate memory for initial creation of tcb struct.");
	            return -1;
	        } 

			/* Initialize state to CLOSED, and set client_portNum */
			
			connectionList[i]->client_nodeID = topology_getMyNodeID();
			connectionList[i]->client_portNum = client_port;
			connectionList[i]->state = CLOSED; // state value is 1

			fprintf(stdout, "Initialized new client entry at index %d, for portNumber %d\n", i, client_port);

			/* Dynamically initialize a mutex struct */

			connectionList[i]->bufMutex = calloc (1, sizeof(pthread_mutex_t));
			if (pthread_mutex_init(connectionList[i]->bufMutex, NULL) != 0)
		    {
		        printf("Mutex init failed\n");
		        return -1;
		    }

			return i;
		}
	}

	/* If the list is full (all non-null entries), then return -1 */

	return -1;
}


// This function is used to connect to the server. It takes the socket ID and the 
// server's port number as input parameters. The socket ID is used to find the TCB entry.  
// This function sets up the TCB's server port number and a SYN segment to send to
// the server using snp_sendseg(). After the SYN segment is sent, a timer is started. 
// If no SYNACK is received after SYNSEG_TIMEOUT timeout, then the SYN is 
// retransmitted. If SYNACK is received, return 1. Otherwise, if the number of SYNs 
// sent > SYN_MAX_RETRY,  transition to CLOSED state and return -1.
//
int srt_client_connect(int sockfd, int nodeID, unsigned int server_port)
{
	/* sockfd = index in connectionList corresponding to the appropriate tcb entry */

	client_tcb_t *tp = connectionList[sockfd];
	if (NULL == tp) {
		fprintf(stdout, "No entry in connectionList\n");
		return -1;
	}

	/* Setup TCB's server port number based on the server_port parameter */

	tp->svr_nodeID = nodeID;
	tp->svr_portNum = server_port;

	/* Setup a SYN segment and send the segment to the server */

	seg_t* segPtr = calloc (1, sizeof(seg_t));
	if (NULL == segPtr) {
		free(segPtr);
		fprintf(stdout, "Failed to allocate memory for SYN segment\n");
		return -1;
	} 

	segPtr->header.type = SYN;
	segPtr->header.src_port = tp->client_portNum;
	segPtr->header.dest_port = server_port;
	segPtr->header.seq_num = 0;

	/* Generate a checksum and add to PacketHeader */

	unsigned short int crcSum = checksum(segPtr);
	segPtr->header.checksum = crcSum;

/*	fprintf(stdout, "Sending SYN: checksum: %d\n", segPtr->header.checksum);
*/
	fprintf(stdout, "Sending SYN from client to server!\n\n");
	if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, segPtr)) {
		return -1;
	}

	/* Update state to SYNSENT */

	tp->state = SYNSENT;

	/* 

		Start timer after packet is sent, used the below resource (code in C++) to implement my timing method
		using gettimeofday c library function

		http://www.songho.ca/misc/timer/timer.html

	*/

	struct timeval t1, t2;
    double elapsedTime;
    gettimeofday(&t1, NULL);

	int synSentCount = 0; /* Number of retry SYNs currently sent */

	while(synSentCount <= SYN_MAX_RETRY) { /* breaks out once synSentCount > SYN_MAX_RETRY */
	
		switch(tp->state) {

			case SYNSENT: /* If no SYNACK is received after SYNSEG_TIMEOUT, then the SYN is retransmitted */

				gettimeofday(&t2, NULL); // keep checking the time

				/* compute the elapsed time in milliseconds */
			    elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
			    elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms

				/* if timed out, then resend segPtr and iterate synSentCount. */

				if (elapsedTime > (SYN_TIMEOUT / 1000000.0)) { 

					synSentCount++;

					if (synSentCount > SYN_MAX_RETRY) { /* Timed while waiting for response from 5th packet sent */
						break; /* break out of switch and while */
					}

					fprintf(stdout, "Resending SYN packet after timeout %f MS, retry number: %d\n", elapsedTime, synSentCount);

					if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, segPtr)) {
						return -1;
					}

    				gettimeofday(&t1, NULL); // re-update t1, after resending segPtr
			    }

				break;

			case CONNECTED: /* If SYNACK is received, return 1. */
				return 1;
		}
	}

	/* Otherwise, if the number of SYNs sent > SYN_MAX_RETRY,  transition to CLOSED state and return -1. */

	fprintf(stdout, "Number of SYN retries exceeded\n");
	tp->state = CLOSED;
	return -1;
}


// Send data to a srt server. This function should use the socket ID to find the TCP entry. 
// Then it should create segBufs using the given data and append them to send buffer linked list. 
// If the send buffer was empty before insertion, a thread called sendbuf_timer 
// should be started to poll the send buffer every SENDBUF_POLLING_INTERVAL time
// to check if a timeout event should occur. If the function completes successfully, 
// it returns 1. Otherwise, it returns -1.
//
int srt_client_send(int sockfd, void* data, unsigned int length)
{
	/* Determine the TCP entry */

	client_tcb_t *tp = connectionList[sockfd];
	if (NULL == tp) {
		fprintf(stdout, "No entry in connectionList\n");
		return -1;
	} 

	/* Lock the client_tcb before modifying the send buffer */

	pthread_mutex_lock(tp->bufMutex);

	/* If send buffer is currently empty, start sendbuf_timer thread */

	if (NULL == tp->sendBufHead) {

		pthread_t bufTimer;

		int segReturn = pthread_create(&bufTimer, NULL, sendBuf_timer, tp); /* Params: fourth param is arg to function */
		
		if (segReturn) {
		    fprintf(stderr, "pthread_create failed for client, rc=%d\n", segReturn);
		    pthread_mutex_unlock(tp->bufMutex);
		    return -1;
		}
		else {
			fprintf(stdout, "Successfully started sendBuf_Timer for client: %d\n\n", tp->client_portNum);
		}

		/* Allocate and initialize send buffer head */

		segBuf_t* head = calloc (1, sizeof(segBuf_t));

		//memset(head->seg.data, '\0', MAX_SEG_LEN); // POTENTIAL AREA OF PROBLEM

		memcpy(head->seg.data, data, length);

		/* Initialize packet header information */

		head->seg.header.src_port = tp->client_portNum;
		head->seg.header.dest_port = tp->svr_portNum;
		head->seg.header.seq_num = tp->next_seqNum;
		head->seg.header.length = length;	
		head->seg.header.type = DATA;

		/* Update tp send buffer variables */

		tp->sendBufHead = head;
		tp->sendBufunSent = head;
		tp->sendBufTail = head;
		tp->next_seqNum += length; // increment next_seqNum by new DATA segment's data length

		/* Generate a checksum and add to PacketHeader */

		unsigned short int crcSum = checksum(&(head->seg));
		head->seg.header.checksum = crcSum;

		/* Now send the segment to the server, add in sendTime */

		fprintf(stdout, "Just sent sequence #: %d\n", head->seg.header.seq_num);

		if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, &(head->seg))) {
			return -1;
		}

		struct timeval t1;
	    gettimeofday(&t1, NULL);

		/* sendTime in microseconds */

	    head->sentTime = t1.tv_sec * 1000000; // sec to us (microseconds)
	    head->sentTime += t1.tv_usec;

	    /* Update unSent pointer */

		tp->sendBufunSent = head->next;

		/* Update unAck_segNum in tp */

	    tp->unAck_segNum += 1;

		pthread_mutex_unlock(tp->bufMutex);

		return 1;
	}

	/* If send buffer is not currently empty */

	/* Break the data into segments based on the length and append to send buffer */

	int bytesRead = 0;
	int segLength = 0;

	while (bytesRead < length) { // so once == length, we exit out

		segBuf_t* node = calloc (1, sizeof(segBuf_t));
		memset(node->seg.data, '\0', MAX_SEG_LEN);

		/* determine how many bytes to read in */

		if ((length - bytesRead) > MAX_SEG_LEN) { // if there is still space for a full MAX_SEG_LEN packet
			memcpy(node->seg.data, data + bytesRead, MAX_SEG_LEN - 1); // copy MAX_SEG_LEN - 1 bytes, leave one byte for null terminated character
			bytesRead += MAX_SEG_LEN - 1;
			segLength = MAX_SEG_LEN - 1;
		}

		else { // not enough room for a full MAX_SEG_LEN packet
			int bytesLeft = length - bytesRead;//actualLength - bytesRead;
			memcpy(node->seg.data, data + bytesRead, bytesLeft);//strncpy(node->seg.data, data + bytesRead, bytesLeft); 
			bytesRead += bytesLeft;
			segLength = bytesLeft;
		}

		/* 
	
		next_seqNum inside TP, contains the next sequence number of a new DATA segment. 
		When a new DATA segment is added to send buffer, it uses next_seqNum as its sequence number then the next_seqNum 
		is incremented by the new DATA segment’s data length.

		*/

		/* Fill in packet header information */

		node->seg.header.src_port = tp->client_portNum;
		node->seg.header.dest_port = tp->svr_portNum;
		node->seg.header.seq_num = tp->next_seqNum;
		node->seg.header.length = segLength;//strlen(node->seg.data);		
		node->seg.header.type = DATA;		

		/* Generate a checksum and add to PacketHeader */

		unsigned short int crcSum = checksum(&(node->seg));
		node->seg.header.checksum = crcSum;

		/* Now append the node to send buffer by updating tail */

		tp->sendBufTail->next = node;
		tp->sendBufTail = node;
		tp->next_seqNum += segLength; // increment next_seqNum by new DATA segment's length

		/* The next unsent segment is the newly appended node, sent all previous nodes */

		if (NULL == tp->sendBufunSent) {
			tp->sendBufunSent = tp->sendBufTail; 
		}
	}

	/* Send segments starting from the first unsent segment until the number of sent-but-not-Acked segments reaches GBN_WINDOW. */

	while (tp->unAck_segNum < GBN_WINDOW && NULL != tp->sendBufunSent) {

		if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, &(tp->sendBufunSent->seg))) {
			return -1;
		}

		fprintf(stdout, "Just sent sequence #: %d\n", tp->sendBufunSent->seg.header.seq_num);

		struct timeval t2;
	    gettimeofday(&t2, NULL);

		/* sendTime in microseconds */

	    tp->sendBufunSent->sentTime = t2.tv_sec * 1000000; // sec to us (microseconds)
	    tp->sendBufunSent->sentTime += t2.tv_usec;

		/* Advance sendBufunSent */

		tp->sendBufunSent = tp->sendBufunSent->next;

		/* Iterate unAck_segNum */

		tp->unAck_segNum += 1;
	}

	pthread_mutex_unlock(tp->bufMutex);

	return 1;
}


// This function is used to disconnect from the server. It takes the socket ID as 
// an input parameter. The socket ID is used to find the TCB entry in the TCB table.  
// This function sends a FIN segment to the server. After the FIN segment is sent
// the state should transition to FINWAIT and a timer started. If the 
// state == CLOSED after the timeout the FINACK was successfully received. Else,
// if after a number of retries FIN_MAX_RETRY the state is still FINWAIT then
// the state transitions to CLOSED and -1 is returned.
//
int srt_client_disconnect(int sockfd)
{

	/* sockfd = index in connectionList corresponding to the appropriate tcb entry */

	client_tcb_t *tp = connectionList[sockfd];
	if (NULL == tp) {
		fprintf(stdout, "No entry in connectionList\n");
		return -1;
	}

	/* Setup a FIN segment and send the segment to the server */

	//srt_hdr_t packetHeader; // no need to calloc memory for the packetHeader, simply declare

	seg_t* segPtr = calloc (1, sizeof(seg_t));
	if (NULL == segPtr) {
		free(segPtr);
		fprintf(stdout, "Failed to allocate memory for SYN segment\n");
		return -1;
	} 

	segPtr->header.type = FIN;
	segPtr->header.src_port = tp->client_portNum;
	segPtr->header.dest_port = tp->svr_portNum;

	/* Generate a checksum and add to PacketHeader */

	unsigned short int crcSum = checksum(segPtr);
	segPtr->header.checksum = crcSum;

	/* Send the segment */

	fprintf(stdout, "Sending FIN from client to server!\n\n");

	if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, segPtr)) {
		return -1;
	}

	/* After the FIN segment is sent the state should transition to FINWAIT and a timer started. */

	tp->state = FINWAIT;

	/* Start timer after packet is sent */
	
	struct timeval t1, t2;
    double elapsedTime;
    gettimeofday(&t1, NULL);

	int finSentCount = 0; /* Number of retry FINs currently sent */

	while(finSentCount <= FIN_MAX_RETRY) { /* breaks out once finSentCount > FIN_MAX_RETRY */

		switch(tp->state) {

			case FINWAIT: /* If no FINACK is received after FINSEG_TIMEOUT_NS, then the FIN is retransmitted */

				gettimeofday(&t2, NULL); // keep checking the time

				/* compute the elapsed time in milliseconds */
			    elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
			    elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms

				/* if timed out, then resend segPtr and iterate finSentCount. */

				if (elapsedTime > (FIN_TIMEOUT / 1000000.0)) { 

					finSentCount++;

					if (finSentCount > FIN_MAX_RETRY) { /* Timed while waiting for response from 5th packet sent */
						break; /* break out of switch and while */
					}

					fprintf(stdout, "Resending FIN packet after timeout %f MS, try number: %d\n", elapsedTime, finSentCount);

					if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, segPtr)) {
						return -1;
					}

    				gettimeofday(&t1, NULL); // re-update t1, after resending segPtr
			    }

				break;

			case CLOSED: /* If FINACK is received, return 1. */

				/* When transition to CLOSED state, clear send buffer to an empty linked list */

				pthread_mutex_lock(tp->bufMutex);

				if (NULL != tp->sendBufHead) {

					while (NULL != tp->sendBufHead) {
						segBuf_t * nextNode = tp->sendBufHead->next;
						free(tp->sendBufHead->next);
						tp->sendBufHead = nextNode;
					}
				}

				tp->sendBufunSent = NULL;
				tp->sendBufTail = NULL;

				pthread_mutex_unlock(tp->bufMutex);

				return 1;
		}
	}

	/* Otherwise, if the number of FINs sent > FIN_MAX_RETRY,  transition to CLOSED state and return -1. */

	fprintf(stdout, "Number of FIN retries exceeded\n");

	tp->state = CLOSED;

	return -1;
}


// This function calls free() to free the TCB entry. It marks that entry in TCB as NULL
// and returns 1 if succeeded (i.e., was in the right state to complete a close) and -1 
// if fails (i.e., in the wrong state).
//
int srt_client_close(int sockfd)
{
	/* When the client TCB is freed in srt_client_close(), the mutex for send buffer is also freed. */

	client_tcb_t *tp = connectionList[sockfd];	
	if (NULL == tp) {
		fprintf(stdout, "No entry in connectionList, invalid close\n");
		return -1;
	}

	/* Save the current state of the client */

	int clientState = tp->state;

	/* Free the mutex */

	if (pthread_mutex_destroy(connectionList[sockfd]->bufMutex) != 0)
    {
        printf("Mutex destroy failed\n");
        return -1;
    }

	free(connectionList[sockfd]->bufMutex);
	connectionList[sockfd]->bufMutex = NULL;

	/* Free the TCB entry and set the pointer to NULL */

	free(connectionList[sockfd]);
	connectionList[sockfd] = NULL;

	/* Wrong state for closing */

	if (clientState != CLOSED) {
		return -1;
	}

	else {
		return 1;
	}
}


// This is a thread  started by srt_client_init(). It handles all the incoming 
// segments from the server. The design of seghanlder is an infinite loop that calls snp_recvseg(). If
// snp_recvseg() fails then the overlay connection is closed and the thread is terminated. Depending
// on the state of the connection when a segment is received  (based on the incoming segment) various
// actions are taken. See the client FSM for more details.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void *seghandler(void* arg)
{
	/* Allocate memory for a new segment */
	
	seg_t* segPtr = calloc (1, sizeof(seg_t));
	if (NULL == segPtr) {
		free(segPtr);
		fprintf(stderr, "Failed to allocate memory for segment\n");
		return NULL;
	}

	int src_nodeID;

	/* Infinite loop that receives segments from the server */

	while (1) {

		if (0 > snp_recvseg(sockConnGlobal, &src_nodeID, segPtr)) { /* Receiving failed */
			;
		}

		/* Packet received */

		else {

			/* check the packet for bit errors */

			if (0 < checkchecksum(segPtr)) { // If the packet is valid

				/* Determine the client port it's meant for */

				int svrPort = segPtr->header.src_port;
				int clientPort = segPtr->header.dest_port;

				/* Determine what entry in the TCB table that clientPort corresponds to */

				int i;
				
				client_tcb_t *tp;
				
				for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) { /* WILL HAVE NULL ENTRIES */

					if ((NULL != connectionList[i]) && (clientPort == connectionList[i]->client_portNum)) {
						tp = connectionList[i];
						break;
					}
				}

				if (NULL == tp) {
					fprintf(stderr, "Bad packet, found no matching port number on client side\n");
					return NULL;
				}

				/* Switch on type of packet */

				switch(segPtr->header.type) {

					case SYNACK:
			
						if (tp->state == SYNSENT) {
							fprintf(stdout, "Client at port %d received SYNACK from server NodeID: %d at port: %d\n", clientPort, src_nodeID, svrPort);
							tp->state = CONNECTED;						
						}

						else {
							fprintf(stderr, "Bad ack!\n");
						}
						
						break;

					case FINACK: /* if FINACK, then set client state to CLOSED */


						if (tp->state == FINWAIT) {
							fprintf(stdout, "Client at port %d received FINACK from server NodeID: %d at port: %d\n", clientPort, src_nodeID, svrPort);
							tp->state = CLOSED;
						}

						else {
							fprintf(stderr, "Bad ack!\n");
						}
						
						break;


					/*

					When a DATAACK segment is received by the client’s seghandler, the Acked segments are removed from send buffer 
					and freed. Note that the sequence number in DATAACK is the expected sequence number from the server for the 
					next DATA segment. So all the segments with a smaller sequence number should be freed since the server 
					has the segments.

					*/

					case DATAACK: 

						pthread_mutex_lock(tp->bufMutex);

						if (tp->state == CONNECTED) {

							int serverAck = segPtr->header.ack_num;

							fprintf(stdout, "\nClient at port %d received DATAACK from server NodeID: %d at port: %d, with ack #: %d\n", clientPort, src_nodeID, svrPort, serverAck);

							segBuf_t* runner = tp->sendBufHead;

							/* Drop the acked packets from send buffer, always popping from head */

							while ((NULL != runner) && (runner->seg.header.seq_num < serverAck)) {
								/* server has received all segments upto but not including the sequence number. */
								segBuf_t* nextNode = runner->next;
								
								fprintf(stdout, "FREEING FROM SENDBUF HEAD: SEQ NUM = %d\n", runner->seg.header.seq_num);
								
								free(runner);
								
								tp->unAck_segNum -= 1; /* Decrement # of sent-but-not-Acked packets */
								
								runner = nextNode;
								tp->sendBufHead = runner;
							}

							/* send segs until sent-but-not-Acked segments' number = GBN_WIN */

							while (tp->unAck_segNum < GBN_WINDOW && NULL != tp->sendBufunSent) {

								if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, &(tp->sendBufunSent->seg))) {
									fprintf(stderr, "Failed to send from seghandler!\n");
									return NULL;
								}

								fprintf(stdout, "Just sent sequence #: %d\n", tp->sendBufunSent->seg.header.seq_num);

								struct timeval t2;
							    gettimeofday(&t2, NULL);

								/* sendTime in microseconds */

							    tp->sendBufunSent->sentTime = t2.tv_sec * 1000000; // sec to us (microseconds)
							    tp->sendBufunSent->sentTime += t2.tv_usec;

							    /* Slide unSent node pointer down */

								tp->sendBufunSent = tp->sendBufunSent->next;
								tp->unAck_segNum += 1;
							}
						}

						else {
							fprintf(stderr, "Bad ack!\n");
						}

						pthread_mutex_unlock(tp->bufMutex);
						break;
				}

				free (segPtr);

				segPtr = calloc (1, sizeof(seg_t));
				if (NULL == segPtr) {
					free(segPtr);
					fprintf(stderr, "Failed to allocate memory for segment\n");
					return NULL;
				}
			}

			else { /* Packet contains a bit error, so free it and drop the packet */

				fprintf(stdout, "Packet bit corruption detected!\n");

				free (segPtr);

				segPtr = calloc (1, sizeof(seg_t));
				if (NULL == segPtr) {
					free(segPtr);
					fprintf(stderr, "Failed to allocate memory for segment\n");
					return NULL;
				}
			}
		}
	}

	return NULL;
}



// This thread continuously polls send buffer to trigger timeout events
// It should always be running when the send buffer is not empty
// If the current time -  first sent-but-unAcked segment's sent time > DATA_TIMEOUT, a timeout event occurs
// When timeout, resend all sent-but-unAcked segments
// When the send buffer is empty, this thread terminates
//
void* sendBuf_timer(void* clienttcb)
{
    client_tcb_t* tp = (client_tcb_t *) clienttcb; /* Cast the void* to our struct type */

	struct timespec t1, t2;
    t1.tv_sec = 0;
    t1.tv_nsec = SENDBUF_POLLING_INTERVAL;

	nanosleep(&t1 , &t2);
	
	struct timeval currTime;

	unsigned int cTime;

	int client_portNum = tp->client_portNum;

	/* Poll the first sent-but-not-Acked segment (sendBufHead) every SENDBUF_POLLING-INTERVAL time */

	while (1) {

		segBuf_t* head = tp->sendBufHead;

		pthread_mutex_t* lock = tp->bufMutex;

		if (NULL == head || NULL == lock) { /* All data segments in send buffer are Acked, so send buffer is empty */
			fprintf(stdout, "\nExiting timer, send buffer/lock for client_tcb port: %d is empty\n\n", client_portNum);
			return NULL; /* Terminate thread */
		}

		pthread_mutex_lock(lock);

		/* get current time in microseconds */

		gettimeofday(&currTime, NULL);

	    cTime = currTime.tv_sec * 1000000; // sec to us (microseconds)
	    cTime += currTime.tv_usec;

		if ((cTime - head->sentTime) > DATA_TIMEOUT) { /* If timed out waiting for an ack, resend in batch */

    		/* Resend all the sent-but-not-Acked segments in send buffer */

	    	/* Essentially all the nodes except for the node pointed to by sendBufunSent */

	    	int i;	
	    	for (i = 0; i < tp->unAck_segNum; i++) {

				if (0 > snp_sendseg(sockConnGlobal, tp->svr_nodeID, &(head->seg))) {
					fprintf(stderr, "Failed to send from segBufTimer!\n");
					return NULL;
				}

				fprintf(stdout, "Resending sequence #: %d\n", head->seg.header.seq_num);

				struct timeval timer;
			    gettimeofday(&timer, NULL);

				/* sendTime in microseconds */

			    head->sentTime = timer.tv_sec * 1000000; // sec to us (microseconds)
			    head->sentTime += timer.tv_usec;

			    /* Advance pointer */

				head = head->next;
	    	}
		} 

		pthread_mutex_unlock(lock);

		nanosleep(&t1 , &t2);
	}

	return NULL;
}