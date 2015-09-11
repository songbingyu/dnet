// File: network/nbrcosttable.c
//
// Description: Contains the functions used to initialize and modify a neighbor cost table,
// which contains link costs between one node and its neighbors.
//

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//This function creates a neighbor cost table dynamically 
//and initialize the table with all its neighbors' node IDs and direct link costs.
//The neighbors' node IDs and direct link costs are retrieved from topology.dat file. 
nbr_cost_entry_t* nbrcosttable_create()
{
	/* Get my nodeID, an array of neighbor IDs, and number of neighbors */

	int myID = topology_getMyNodeID();
	int * nbrArray = topology_getNbrArray();
	int nNum = topology_getNbrNum();

	/* Calloc a neighbor cost table */

	nbr_cost_entry_t * nCostTable = calloc(nNum, sizeof(nbr_cost_entry_t));

	/* Iterate through the neighbors, and get link costs between current node and neighbor */

	int i;
	
	for (i = 0; i < nNum; i++) {

		int neighborID = nbrArray[i];
		unsigned int linkCost = topology_getCost(myID, neighborID);

		/* Add neighbor link cost to table */

		nCostTable[i].nodeID = neighborID;
		nCostTable[i].cost = linkCost;
	}

	free (nbrArray);
	return nCostTable;
}

//This function destroys a neighbor cost table. 
//It frees all the dynamically allocated memory for the neighbor cost table.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
	free(nct);
	nct = NULL;
}

//This function is used to get the direct link cost from neighbor.
//The direct link cost is returned if the neighbor is found in the table.
//INFINITE_COST is returned if the node is not found in the table.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
	int nNum = topology_getNbrNum();
	unsigned int linkCost = INFINITE_COST;

	/* Loop through all the neighbors in nbrcosttable to find a matching neighbor */

	int i;
	
	for (i = 0; i < nNum; i++) {

		if (nodeID == nct[i].nodeID) {
			linkCost = nct[i].cost;
		}
	}

	return linkCost;
}

//This function prints out the contents of a neighbor cost table.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
	int nNum = topology_getNbrNum();

	/* Loop through all the neighbors in nbrcosttable to print each entry */

	int i;	
	int myID = topology_getMyNodeID();

	fprintf(stdout, "\nNode %d neighbor cost table:\n", myID);

	for (i = 0; i < nNum; i++) {
		fprintf(stdout, "\tNbr %d: Cost: %u\n", nct[i].nodeID, nct[i].cost);
	}
}