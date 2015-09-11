// File: network/dvtable.c
//
// Description: Contains the functions used to initialize and modify the distance vector table, 
// consisting of vectors holding the min distances between a given node and all other nodes in the network.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//This function creates a dvtable(distance vector table) dynamically.
//A distance vector table contains the n+1 entries, where n is the number of the neighbors of this node, and the rest one is for this node itself. 
//Each entry in distance vector table is a dv_t structure which contains a source node ID and an array of N dv_entry_t structures where N is the number of all the nodes in the overlay.
//Each dv_entry_t contains a destination node address the the cost from the source node to this destination node.
//The dvtable is initialized in this function.
//The link costs from this node to its neighbors are initialized using direct link cost retrived from topology.dat. 
//Other link costs are initialized to INFINITE_COST.
//The dynamically created dvtable is returned.
dv_t* dvtable_create(nbr_cost_entry_t* nct)
{
	/* Create a distance vector table with n + 1 entries */

	int myID = topology_getMyNodeID();
	int nNum = topology_getNbrNum();
	int totalNodeNum = topology_getNodeNum();

	dv_t * dVectorTable = calloc(nNum + 1, sizeof(dv_t));
	if (NULL == dVectorTable) {
		fprintf(stderr, "Failed to allocate memory for dv table\n");
		return NULL;
	}

	/* Create n + 1 vectors for the vector table (one for each neighbor and the host itself) */

	int * nodeArray = topology_getNodeArray(); // array which includes all the node IDs in the network
	int * nHostArray = topology_getNbrAndHostArray(); // array which includes all neighbor IDs plus the host ID

	int i;

	for (i = 0; i < nNum + 1; i++) { // loop through every row

		dv_entry_t * vectorArray = calloc(totalNodeNum, sizeof(dv_entry_t)); // initialize a row in the vectorTable

		if (NULL == vectorArray) {
			fprintf(stderr, "Failed to allocate memory for dvector\n");
			return NULL;	
		}

		// initialize the source nodeID, and vector

		dVectorTable[i].nodeID = nHostArray[i];
		dVectorTable[i].dvEntry = vectorArray;

		int j; 

		for (j = 0; j < totalNodeNum; j++) { // for every column

			dVectorTable[i].dvEntry[j].nodeID = nodeArray[j];

			/* Set the link cost between nodeArray[j] and dVectorTable[i].nodeID as infinite if sourceNode is not hostID */

			if (myID != dVectorTable[i].nodeID) {
				dVectorTable[i].dvEntry[j].cost = INFINITE_COST;
			}

			else { /* Set link cost as what we have so far in the neighborcost table */

				if (myID == dVectorTable[i].dvEntry[j].nodeID) {// If dest node is same as host ID
					dVectorTable[i].dvEntry[j].cost = 0; // no cost to go to myself				
				}

				else {
					dVectorTable[i].dvEntry[j].cost = nbrcosttable_getcost(nct, dVectorTable[i].dvEntry[j].nodeID);
				}
			}
		}
	}

	free(nodeArray);
	free(nHostArray);
	return dVectorTable;
}

//This function destroys a dvtable. 
//It frees all the dynamically allocated memory for the dvtable.
void dvtable_destroy(dv_t* dvtable)
{
	int nNum = topology_getNbrNum();
	int i;
	for (i = 0; i < nNum + 1; i++) {
		free(dvtable[i].dvEntry);
	}

	free(dvtable);
	dvtable = NULL;
	return;
}

//This function sets the link cost between two nodes in dvtable.
//If those two nodes are found in the table and the link cost is set, return 1.
//Otherwise, return -1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
	int nNum = topology_getNbrNum();
	int totalNodeNum = topology_getNodeNum();
	int i;
	for (i = 0; i < nNum + 1; i++) { // loop through every row

		if (fromNodeID == dvtable[i].nodeID) { // found the source nodeID

			int j; 

			for (j = 0; j < totalNodeNum; j++) { // now look for the destination nodeID

				if (toNodeID == dvtable[i].dvEntry[j].nodeID) {
					dvtable[i].dvEntry[j].cost = cost; // Set the new cost
					return 1;
				}
			}
		}
	}

	/* Couldn't find the two nodes in the table */
	return -1;
}

//This function returns the link cost between two nodes in dvtable
//If those two nodes are found in dvtable, return the link cost. 
//otherwise, return INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
	int nNum = topology_getNbrNum();
	int totalNodeNum = topology_getNodeNum();

	int i;
	for (i = 0; i < nNum + 1; i++) { // loop through every row

		if (fromNodeID == dvtable[i].nodeID) { // found the source nodeID

			int j; 
			for (j = 0; j < totalNodeNum; j++) { // now look for the destination nodeID

				if (toNodeID == dvtable[i].dvEntry[j].nodeID) {
					return dvtable[i].dvEntry[j].cost; // Return the cost
				}
			}
		}
	}

	/* Couldn't find the two nodes in the table */
	return INFINITE_COST;
}

//This function prints out the contents of a dvtable.
void dvtable_print(dv_t* dvtable)
{

	int myID = topology_getMyNodeID();
	int nNum = topology_getNbrNum();
	int totalNodeNum = topology_getNodeNum();

	fprintf(stdout, "\nNode %d Distance Vector Table:", myID);

	int i;
	for (i = 0; i < nNum + 1; i++) { // loop through every row

		fprintf(stdout, "\n\tFrom: Node %d", dvtable[i].nodeID);

		int j; 
		for (j = 0; j < totalNodeNum; j++) { // for every column
			fprintf(stdout, "\tTo: Node %d, Cost: %u", dvtable[i].dvEntry[j].nodeID, dvtable[i].dvEntry[j].cost);
		}
	}
}