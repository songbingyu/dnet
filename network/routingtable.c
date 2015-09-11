// File: network/routingtable.c
//
// Description: Contains the functions for the routing table, a hash table containing MAX_RT_ENTRY slot entries.  
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "routingtable.h"

//This is the hash function used the by the routing table
//It takes the hash key - destination node ID as input, 
//and returns the hash value - slot number for this destination node ID.
int makehash(int node)
{
	return node % MAX_ROUTINGTABLE_SLOTS;
}

//This function creates a routing table dynamically.
//All the entries in the table are initialized to NULL pointers.
//Then for all the neighbors with a direct link, create a routing entry using the neighbor itself as the next hop node, and insert this routing entry into the routing table. 
//The dynamically created routing table structure is returned.
routingtable_t* routingtable_create()
{
	routingtable_t * rTable = calloc(1, sizeof(routingtable_t));
	if (NULL == rTable) {
		fprintf(stderr, "Failed to allocate memory for routing table\n");
		return NULL;
	}

	int * nbrArray = topology_getNbrArray(); // array which includes all neighbor IDs
	int nNum = topology_getNbrNum();
	int i;

	for (i = 0; i < nNum; i++) { /* For all neighbors with a direct link */

		/* create a routing entry using the neighbor itself as the next hop node */

		routingtable_entry_t * routingEntry = calloc(1, sizeof(routingtable_entry_t));
		routingEntry->destNodeID = nbrArray[i];
		routingEntry->nextNodeID = nbrArray[i];

		/* insert this routing entry into the routing table */

		int idx = makehash(routingEntry->destNodeID);

		if (NULL == rTable->hash[idx]) {// if hash slot is empty, add routing entry as the first entry
			rTable->hash[idx] = routingEntry;
		}

		else { /* We have a collision, append to the end of the SLL */

			routingtable_entry_t * runner = rTable->hash[idx];

			while (NULL != runner->next) {
				runner = runner->next; 
			}
			runner->next = routingEntry;
		}
	}

	free(nbrArray);
	return rTable;
}

//This funtion destroys a routing table. 
//All dynamically allocated data structures for this routing table are freed.
void routingtable_destroy(routingtable_t* routingtable)
{

	int i;

	for (i = 0; i < MAX_ROUTINGTABLE_SLOTS; i++) { /* Loop through the hashtable */

		if (NULL != routingtable->hash[i]) { /* If the entry is not empty */

			routingtable_entry_t * runner = routingtable->hash[i];

			while (NULL != runner) { /* Free each entry in the SLL */
				routingtable_entry_t * second = runner->next; // save the next node in the SLL				
				free (runner);
				runner = second; 
			}			
		}
	}

	free(routingtable);
	routingtable = NULL;
	return;
}

//This function updates the routing table using the given destination node ID and next hop's node ID.
//If the routing entry for the given destination already exists, update the existing routing entry.
//If the routing entry of the given destination is not there, add one with the given next node ID.
//Each slot in routing table contains a linked list of routing entries due to conflicting hash keys (differnt hash keys (destination node ID here) may have same hash values (slot entry number here)).
//To add an routing entry to the hash table:
//First use the hash function makehash() to get the slot number in which this routing entry should be stored. 
//Then append the routing entry to the linked list in that slot.
void routingtable_setnextnode(routingtable_t* routingtable, int destNodeID, int nextNodeID)
{

	int idx = makehash(destNodeID);

	if (NULL != routingtable->hash[idx]) { /* If the entry is not empty */

		routingtable_entry_t * runner = routingtable->hash[idx];
		
		/* Loop through the SLL to see if the routing entry exists */

		while (NULL != runner) {
			if (destNodeID == runner->destNodeID) { // routing entry for given dest already exists
				/* update existing routing entry */
				runner->nextNodeID = nextNodeID;
				return;
			}
			runner = runner->next;
		}

		/* Else reached the end of the SLL, no entry was found, so append a new entry */

		routingtable_entry_t * routingEntry = calloc(1, sizeof(routingtable_entry_t));

		routingEntry->destNodeID = destNodeID;
		routingEntry->nextNodeID = nextNodeID;

		/* Find the tail of the SLL */

		runner = routingtable->hash[idx];

		while (NULL != runner->next) {
			runner = runner->next;
		}

		runner->next = routingEntry;
	}

	else { /* Entry is empty, add routing entry to empty slot */

		routingtable_entry_t * routingEntry = calloc(1, sizeof(routingtable_entry_t));

		routingEntry->destNodeID = destNodeID;
		routingEntry->nextNodeID = nextNodeID;

		routingtable->hash[idx] = routingEntry;
	}
}

//This function looks up the destNodeID in the routing table.
//Since routing table is a hash table, this operation has O(1) time complexity.
//To find a routing entry for a destination node, you should first use the hash function makehash() to get the slot number and then go through the linked list in that slot to search the routing entry.
//If the destNodeID is found, return the nextNodeID for this destination node.
//If the destNodeID is not found, return -1.
int routingtable_getnextnode(routingtable_t* routingtable, int destNodeID)
{

	int idx = makehash(destNodeID);

	if (NULL != routingtable->hash[idx]) { /* If the entry is not empty */

		routingtable_entry_t * runner = routingtable->hash[idx];
		
		/* Loop through the SLL to see if the routing entry exists */

		while (NULL != runner) {
			if (destNodeID == runner->destNodeID) { // routing entry for given dest already exists
				return runner->nextNodeID;
			}
			runner = runner->next;
		}

		return -1; /* Reached end of SLL, didn't find a matching destNodeID */
	}

	else { /* Empty slot, so return -1 */
		return -1;
	}
}

//This function prints out the contents of the routing table
void routingtable_print(routingtable_t* routingtable)
{

	int myID = topology_getMyNodeID();

	fprintf(stdout, "\n\nNode %d Routing Table:", myID);

	int i;

	for (i = 0; i < MAX_ROUTINGTABLE_SLOTS; i++) { /* Loop through the hashtable */

		if (NULL != routingtable->hash[i]) { /* If the entry is not empty */

			routingtable_entry_t * runner = routingtable->hash[i];
			fprintf(stdout, "\n");

			while (NULL != runner) { /* Print each routing entry */
				fprintf(stdout, "\tDestination Node: %d, Next Hop: %d", runner->destNodeID, runner->nextNodeID);
				runner = runner->next; 
			}			
		}
	}
}