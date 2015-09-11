// File: topology/topology.c
//
// Description: Implements some helper functions used to parse the topology file 
//

#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../common/constants.h"
#include "topology.h"

//this function returns node ID of the given hostname
//the node ID is an integer of the last 8 digit of the node's IP address
//for example, a node with IP address 202.120.92.3 will have node ID 3
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromname(char* hostname) 
{

    struct hostent *he;
    char *ip;
    
    /* Get a hostent struct from gethostbyname and store the ip address*/

    he = gethostbyname(hostname);
    ip = inet_ntoa(*((struct in_addr *)he->h_addr_list[0]));

    int nodeID;
    
    /* Scan the last integer field for the Node ID */

	if (1 != (sscanf(ip, "%*d.%*d.%*d.%d", &nodeID))) {
		return -1;
	}

	return nodeID;
}

//this function returns node ID from the given IP address
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromip(struct in_addr* addr)
{
  return 0;
}

//this function returns my node ID
//if my node ID can't be retrieved, return -1
int topology_getMyNodeID()
{

	char hostname[128];
	if (0 != gethostname(hostname, sizeof(hostname))) {
		return -1;
	}

	int nodeID = topology_getNodeIDfromname(hostname);
	return nodeID;
}

//this functions parses the topology information stored in topology.dat
//returns the number of neighbors
int topology_getNbrNum()
{

	/* Get the hostname */

	char hostname[128];
	if (0 != gethostname(hostname, sizeof(hostname))) {
		return -1;
	}

	/* Open topology.dat for reading */

	FILE * fp = NULL;
	fp = fopen("./topology/topology.dat", "r");    
    if (NULL == fp) {
        fprintf(stderr, "Error opening file");
        return -1;
    }

	int nNum = 0;
    int r = 0;

    char str1[128];
    char str2[128];

    /* Read each line from topology.dat */

	while (EOF != (r = fscanf(fp, "%s %s %*d\n", str1, str2))) {

		/* For each line that the given hostname appears in, increment nNum (neighbor number) by 1 */
	    if ( (strcmp(hostname, str1) == 0) || (strcmp(hostname, str2) == 0)) {
	    	nNum += 1;
	    }

		memset(str1, '\0', sizeof(str1));
		memset(str2, '\0', sizeof(str2));
    }

	fclose(fp);
	return nNum;
}

/* Gets ip address from a given hostname */
char* getIP(char* hostname) {

	struct hostent *he;
    char *ip;
    
    he = gethostbyname(hostname);
    ip = inet_ntoa(*((struct in_addr *)he->h_addr_list[0]));

    return ip;
} 

//this functions parses the topology information stored in topology.dat
//returns the number of total nodes in the overlay 
int topology_getNodeNum()
{ 

	int nNum = 0;
	int nArray[MAX_NODE_NUM];

	/* Open topology.dat for reading */

	FILE * fp = NULL;
	fp = fopen("./topology/topology.dat", "r");    
    if (NULL == fp) {
        fprintf(stderr, "Error opening file");
        return 0;
    }

    int r = 0;

    char str1[128];
    char str2[128];

    /* Read each line from topology.dat */

    int ID1;
    int ID2;

	while (EOF != (r = fscanf(fp, "%s %s %*d\n", str1, str2))) {

		/* For each unique nodeID not in our array, add node ID to array */

		ID1 = topology_getNodeIDfromname(str1);
		ID2 = topology_getNodeIDfromname(str2);

		int i;
		int bool1 = 0;

		for (i = 0; i < MAX_NODE_NUM; i++) { // check if first ID is in the array

			if (ID1 == nArray[i]) {
				bool1 = 1;
				break;
			}
		}

		if (bool1 == 0) {
			nArray[nNum] = ID1;
			nNum += 1;
		}

		int j;
		int bool2 = 0;

		for (j = 0; j < MAX_NODE_NUM; j++) { // check if first ID is in the array

			if (ID2 == nArray[j]) {
				bool2 = 1;
				break;
			}
		}

		if (bool2 == 0) {
			nArray[nNum] = ID2;
			nNum += 1;
		}

		memset(str1, '\0', sizeof(str1));
		memset(str2, '\0', sizeof(str2));
    }

	fclose(fp);
	return nNum;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the nodes' IDs in the overlay network  
int* topology_getNodeArray()
{

	/* Calloc an integer array */

	int nodeNum = topology_getNodeNum();

	int* nodeArray = (int *) calloc(nodeNum, sizeof(int));
	if (NULL == nodeArray) {
		fprintf(stderr, "Error allocating nodeArray");
        return NULL;
	}

	/* Open topology.dat for reading */

	FILE * fp = NULL;
	fp = fopen("./topology/topology.dat", "r");    
    if (NULL == fp) {
        fprintf(stderr, "Error opening file");
        return NULL;
    }

    int r = 0;

    char str1[128];
    char str2[128];

    /* Read each line from topology.dat */

    int ID1;
    int ID2;

    int i = 0; // var to keep track of which node we are on

	while (EOF != (r = fscanf(fp, "%s %s %*d\n", str1, str2))) {

		/* For each unique nodeID not in our array, add node ID to array */

		ID1 = topology_getNodeIDfromname(str1);
		ID2 = topology_getNodeIDfromname(str2);

	    if (0 == is_InArray(nodeArray, ID1)) { // if the first ID is not in the array
	    	nodeArray[i] = ID1;
		    i += 1;
	    }

	    if (0 == is_InArray(nodeArray, ID2)) { // if the second ID is not in the array
	    	nodeArray[i] = ID2;
	    	i += 1;
	    }

		memset(str1, '\0', sizeof(str1));
		memset(str2, '\0', sizeof(str2));
    }

	fclose(fp);
	return nodeArray;
}

/* 
 *
 * Function returns 1 if the nodeID is in the nodeArray, 0 otherwise
 *
 */	
int is_InArray (int* nodeArray, int nodeID) {

	int nodeNum = topology_getNodeNum();

	/* Iterate through the nodeArray to check if nodeID is in the array */

	int i;

	for (i = 0; i< nodeNum; i++) {
		if (nodeID == nodeArray[i]) { // Found the ID in our array, return 1
			return 1;
		}
	}

	return 0; // Didn't find the ID in the array
}


//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the neighbors'IDs  
int* topology_getNbrArray()
{

	/* Calloc an integer array */

	int nNum = topology_getNbrNum();

	int* nbrArray = (int *) calloc(nNum, sizeof(int));
	if (NULL == nbrArray) {
		fprintf(stderr, "Error allocating nbrarray");
        return NULL;
	}

	/* Get the hostname */

	char hostname[128];
	if (0 != gethostname(hostname, sizeof(hostname))) {
		return NULL;
	}

	/* Open topology.dat for reading */

	FILE * fp = NULL;
	fp = fopen("./topology/topology.dat", "r");    
    if (NULL == fp) {
        fprintf(stderr, "Error opening file");
        return NULL;
    }

    int r = 0;

    char str1[128];
    char str2[128];

    /* Read each line from topology.dat */

    int ID;

    int i = 0; // var to keep track of which neighbor we are on

	while (EOF != (r = fscanf(fp, "%s %s %*d\n", str1, str2))) {

		/* For each line that the given hostname appears in, add neighbor ID to array */

	    if ( (strcmp(hostname, str1) == 0) || (strcmp(hostname, str2) == 0)) {

		    if (strcmp(hostname, str1) == 0) {

		    	/* get nodeID of the other string and add to array */

		    	ID = topology_getNodeIDfromname(str2);
		    }

		    if (strcmp(hostname, str2) == 0) {

		    	/* get nodeID of the other string and add to array */

		    	ID = topology_getNodeIDfromname(str1);
		    }

	    	nbrArray[i] = ID;
		    i += 1;
	    }

		memset(str1, '\0', sizeof(str1));
		memset(str2, '\0', sizeof(str2));
    }

	fclose(fp);
	return nbrArray;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the neighbors'IDs + the host ID  
int* topology_getNbrAndHostArray()
{

	/* Calloc an integer array */

	int nNum = topology_getNbrNum();

	int* nbrArray = (int *) calloc(nNum + 1, sizeof(int));
	if (NULL == nbrArray) {
		fprintf(stderr, "Error allocating nbrarray");
        return NULL;
	}

	/* Get the hostname */

	char hostname[128];
	if (0 != gethostname(hostname, sizeof(hostname))) {
		return NULL;
	}

	/* Open topology.dat for reading */

	FILE * fp = NULL;
	fp = fopen("./topology/topology.dat", "r");    
    if (NULL == fp) {
        fprintf(stderr, "Error opening file");
        return NULL;
    }

    int r = 0;

    char str1[128];
    char str2[128];

    /* Add host ID to the array */

	nbrArray[0] = topology_getMyNodeID();

    /* Read each line from topology.dat */

    int ID;
    int i = 1; // var to keep track of which neighbor we are on

	while (EOF != (r = fscanf(fp, "%s %s %*d\n", str1, str2))) {

		/* For each line that the given hostname appears in, add neighbor ID to array */

	    if ( (strcmp(hostname, str1) == 0) || (strcmp(hostname, str2) == 0)) {

		    if (strcmp(hostname, str1) == 0) {
		    	/* get nodeID of the other string and add to array */
		    	ID = topology_getNodeIDfromname(str2);
		    }

		    if (strcmp(hostname, str2) == 0) {
		    	/* get nodeID of the other string and add to array */
		    	ID = topology_getNodeIDfromname(str1);
		    }

	    	nbrArray[i] = ID;
		    i += 1;
	    }

		memset(str1, '\0', sizeof(str1));
		memset(str2, '\0', sizeof(str2));
    }

	fclose(fp);
	return nbrArray;
}

//this functions parses the topology information stored in topology.dat
//returns the cost of the direct link between the two given nodes 
//if no direct link between the two given nodes, INFINITE_COST is returned
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{

	/* Open topology.dat for reading */

	FILE * fp = NULL;
	fp = fopen("./topology/topology.dat", "r");    
    if (NULL == fp) {
        fprintf(stderr, "Error opening file");
        return -1;
    }

	unsigned int cost = 0;
    int r = 0;

    char str1[128];
    char str2[128];

    /* Read each line from topology.dat */

	while (EOF != (r = fscanf(fp, "%s %s %u\n", str1, str2, &cost))) {

		int id1 = topology_getNodeIDfromname(str1);
		int id2 = topology_getNodeIDfromname(str2);

		/* If found a link between the two nodes in topology, break out of loop and return that link cost */
	    if ((fromNodeID == id1 && toNodeID == id2) || (fromNodeID == id2 && toNodeID == id1)) {	    	
	    	break;
	    }

		memset(str1, '\0', sizeof(str1));
		memset(str2, '\0', sizeof(str2));
    }

	fclose(fp);

	/* If no link between two nodes exists, return infinite cost */
	if (0 == cost) {
		return INFINITE_COST;
	}

	else {
		return cost;
	}
}