// File: overlay/neighbortable.c
//
// Description: Contains the API for the neighbor table, an array of neighbor entry nodes, each containing information about
// each neighbor node.
//

#include "neighbortable.h"
#include "../topology/topology.h"

#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

//This function first creates a neighbor table dynamically. It then parses the topology/topology.dat file and fill the nodeID and nodeIP fields in all the entries, initialize conn field as -1 .
//return the created neighbor table
nbr_entry_t* nt_create()
{

	/* Get number of neighbors */

	int nNum = topology_getNbrNum();

	/* Allocate memory for global neighbor table array */

	nbr_entry_t* nTable = (nbr_entry_t *) calloc(nNum, sizeof(nbr_entry_t)); // WTF IS WRONG WITH THIS?
    if (NULL == nTable) {
        free(nTable);
        fprintf(stderr, "Error: failed to allocate memory for initial creation of nbr_entry_t struct.");
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

		/* For each line that the given hostname appears in, add neighbor to array */

	    if ( (strcmp(hostname, str1) == 0) || (strcmp(hostname, str2) == 0)) {

	    	//fprintf(stdout, "Adding to index %d\n", i);

		    if (strcmp(hostname, str1) == 0) {
		    	/* get nodeID and IP of the other string and add to array */
		    	ID = topology_getNodeIDfromname(str2);
				char * ip = getIP(str2);
		    	nTable[i].nodeIP = inet_addr(ip);
		    	//fprintf(stdout, "IP BEFORE: %s and AFTER: %d\n", ip, nTable[i].nodeIP);
		    }

		    if (strcmp(hostname, str2) == 0) {
		    	/* get nodeID and IP of the other stirng and add to array */
		    	ID = topology_getNodeIDfromname(str1);
		    	char * ip = getIP(str1);
		    	nTable[i].nodeIP = inet_addr(ip);
		    	//fprintf(stdout, "IP BEFORE: %s and AFTER: %d\n", ip, nTable[i].nodeIP);
		    }

	    	nTable[i].nodeID = ID;
			nTable[i].conn = -1;		    	
		    i += 1;
	    }

		memset(str1, '\0', sizeof(str1));
		memset(str2, '\0', sizeof(str2));
    }

	fclose(fp);
	return nTable;
}

//This function destroys a neighbortable. It closes all the connections and frees all the dynamically allocated memory.
void nt_destroy(nbr_entry_t* nt)
{

	/* Get number of neighbors */

	int nNum = topology_getNbrNum();

	/* Loop through all the connections (with each neighbor) */

	int i;

	for (i = 0; i < nNum; i++) {
		fprintf(stdout, "Closing connections!\n");
		close(nt[i].conn);
		//shutdown(nt[i].conn, SHUT_RD);
	}

	free(nt);
	nt = NULL;
}

//This function is used to assign a TCP connection to a neighbor table entry for a neighboring node. If the TCP connection is successfully assigned, return 1, otherwise return -1
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{

	fprintf(stdout, "Adding connection with Node: %d to neighbor table\n", nodeID);

	/* Get number of neighbors */

	int nNum = topology_getNbrNum();

	/* Loop through each neighbor table entry and fill in appropriate connection field */

	int i;

	for (i = 0; i < nNum; i++) {
		if (nodeID == nt[i].nodeID) {
			nt[i].conn = conn;
		}
	}

	return 1;
}