#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

char nodes[20][20];					// Saves ip address's of the nodes
int node_neighbour[20];					// Tells wheather a node i is a neighbour
int total_nodes;					// total number of nodes
int adj_list[20][20];					// adjacency list for implementing graph
typedef struct 						// structure for a routing table entry
{
	int Destination;
	int NextHop;
	int Cost;
	int TTL;
} Route_entry;
Route_entry *RoutingTable[20];				// routing table

int sockfd;						// socket file descriptor for sending and recieving messages
struct sockaddr_in servaddr,cliaddr;			// client and server structures

pthread_t ttl_thread[20];				// array of threads for each neighbour
pthread_t adv_thread;					// single thread for send_advertisement call
int pid_ttl,pid_adv;					// thread id's

int port_no,Period,infinity,TTL;			// variables to store command line parameters

// This function prepares the skeleton of the routing process
void initialize(char *filename)
{
	FILE *fp;					// read from config file
	char buffer[100],temp[5];			// temporary buffers
	char *pch;					// temporary buffer for tokenizer
	int i=1,j;					// counters
	Route_entry *ptr;				// pointer to routing table data structure
	struct ifreq ifr;				// structure to get one's ip

	// Creating socket
	sockfd=socket(AF_INET,SOCK_DGRAM,0);
	
	// Populating server information
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(port_no);
	bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

	// Getting ip address of the node setting variables wrt node.
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "em1", IFNAMSIZ-1);
	ioctl(sockfd, SIOCGIFADDR, &ifr);
	strcpy(nodes[0],inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
	total_nodes = 1;
	node_neighbour[0] = 1;

	// Reading from config file to see which node is neighbour.
	if((fp = fopen(filename,"r")) == NULL)
	{
		printf("Invalid File");
		exit(0);
	}
	else
	{
		while((fgets(buffer, 100, fp)) != NULL)
		{
			total_nodes++;
			pch = strtok (buffer," ");
			strcpy(nodes[i],pch);
			pch = strtok (NULL, " ");
			strcpy(temp,pch);

			if(temp[0] == 'y')
				node_neighbour[i] = 1;
			else
				node_neighbour[i] = 0;
			i++;
		}
	}

	// Creating graph data structure
	adj_list[0][0] = 0;

	for(i=1;i<total_nodes;i++)
		for(j=0;j<total_nodes;j++)
			adj_list[i][j] = infinity;

	for(i=1;i<total_nodes;i++)
		if(node_neighbour[i] == 1)
		{
			adj_list[0][i] = 1;
			adj_list[i][0] = 1;
		}
		else
			adj_list[0][i] = infinity;

	// Creating routing table
	for(i=0;i<total_nodes;i++)
	{
		ptr = (Route_entry*)malloc(sizeof(Route_entry));
		ptr->Destination = i;
		if(node_neighbour[i] ==1)
		{
			ptr->NextHop = i;
			ptr->TTL = TTL;
			if(i==0)
				ptr->Cost = 0;
			else
				ptr->Cost = 1;
		}	
		else
		{
			ptr->NextHop = -1;
			ptr->Cost = infinity;
			ptr->TTL = -1;
		}
		RoutingTable[i] = ptr;
	}
	// Calling update table to apply Bellman Ford algo to the initial configuration
	update_table();
	// Sending current routing table to all neighbours
	send_advertisement();
}

// The function which is called by a thread to keep sending adverstisements after sleeping for period time.
void *manage_advertisements(void *ptr)
{
	while(1)
	{
		sleep(Period);
		send_advertisement();
	}
}

// The function called by a thread to see if each neighbour is still alive.
// When TTL becomes 0 for a neighbour its cost becomes infinity and is sent to all remaining neighbours.
void *manage_TTL(void *ptr)
{
	int i;
	i = (int) ptr;
	RoutingTable[i]->TTL = TTL;
	while(RoutingTable[i]->TTL > 0)
	{
		sleep(1);
		//printf("Thread %d time:%d\n",i,RoutingTable[i]->TTL);
		RoutingTable[i]->TTL = RoutingTable[i]->TTL - 1;
	}
	RoutingTable[i]->Cost = infinity;
	adj_list[0][i] = infinity;
	printf("%s went down.\nSending Update to all nodes\n",nodes[i]);
	send_advertisement();
}
// This subroutine is called to check if the entry for a non neighbour is consistent in both adj_list and routing table
void correct_table(int index)
{
	int i;
	for(i=0;i<total_nodes;i++)
	{
		if(RoutingTable[i]->NextHop == index && index != i)
			if(adj_list[index][i] == infinity)
			{
				adj_list[0][i] = infinity;
				RoutingTable[i]->Cost = infinity;
			}
			else
			{
				adj_list[0][i] = adj_list[0][index]+adj_list[index][i];
				RoutingTable[i]->Cost = adj_list[0][index]+adj_list[index][i];
			}
	}
}

// Update the routing table using Bellman ford algorithm
// Print the adj_list and routing table
// Returns 1 if changes are made to the routing table so that advertisements can be sent to all neighbours.
int update_table()
{
	int i,j;
	int flag = 0;
	for(i=0;i<total_nodes;i++)
	{
		for(j=0;j<total_nodes;j++)
		{
			if(adj_list[0][i] + adj_list[i][j] < adj_list[0][j])
			{
				RoutingTable[j]->Cost = adj_list[0][i] + adj_list[i][j];
				adj_list[0][j] = RoutingTable[j]->Cost;
				RoutingTable[j]->NextHop = i;
				flag = 1;
			}
		}
	}

	for(i=0;i<total_nodes;i++)
	{
		for(j=0;j<total_nodes;j++)
			printf("%d ",adj_list[i][j]);
		printf("\n");
	}
	
	printf("             Node          NextHop Cost TTL\n");
	for(i=0;i<total_nodes;i++)
	{
		printf("%17s%17s%5d%4d\n",nodes[RoutingTable[i]->Destination],nodes[RoutingTable[i]->NextHop],RoutingTable[i]->Cost,RoutingTable[i]->TTL);
	}
	return flag;
}

// Routing table is sent to all the neighbouring nodes.
// Split horizon has been implemented
void send_advertisement()
{
	int i,j;
	char sendline[1000],temp[5];
	struct sockaddr_in temp_servaddr;
	for(i=1;i<total_nodes;i++)
	{
		if(node_neighbour[i] == 1)
		{
			for(j=0;j<total_nodes;j++)
			{
				if(RoutingTable[j]->NextHop != i)
				{
					strcat(sendline,nodes[j]);
					strcat(sendline," ");
					sprintf(temp, "%d", RoutingTable[j]->Cost);
					strcat(sendline,temp);
					strcat(sendline," ");
				}
			}
			bzero(&temp_servaddr,sizeof(temp_servaddr));
			temp_servaddr.sin_family = AF_INET;
			temp_servaddr.sin_addr.s_addr=inet_addr(nodes[i]);
			temp_servaddr.sin_port=htons(port_no);
			sendto(sockfd,sendline,strlen(sendline),0,(struct sockaddr *)&temp_servaddr,sizeof(temp_servaddr));
		}
		bzero(sendline,1000);
	}
}

// Returns the index of a node given its ip address
int returnIndex(char *ip_addr)
{
	int i;
	for(i=1;i<total_nodes;i++)
		if(strcmp(ip_addr,nodes[i])==0)
		{
			return i;
		}
	return -1;
}

int main(int argc, char**argv)
{
	socklen_t len;
	char *addr;
	char msg_in[1000],temp[100];
	char *token;
	int n,index1,index2;
	int i,j;

	// Check if all command line parameters have been given
	if (argc != 6)
	{
		printf("usage: <Config> <Port Number> <TTL> <Infinity> <Period>\n");
		exit(1);
	}

	// Set values to their respective variables
	sscanf(argv[2], "%d", &port_no);
	sscanf(argv[3], "%d", &TTL);
	sscanf(argv[4], "%d", &infinity);
	sscanf(argv[5], "%d", &Period);

	// Call intialize
	initialize(argv[1]);

	// Start timer for send advertisement.
	pid_adv = pthread_create( &adv_thread, NULL, manage_advertisements, NULL);

	// Start TTL timers for neighbouring nodes.
	for(i=1;i<total_nodes;i++)
		if(node_neighbour[i]==1)
		{
			pid_ttl = pthread_create( &ttl_thread[i], NULL, manage_TTL, (void*) i);
		}

	while(1)
	{
		// Preparing buffer to recieve message
		bzero(msg_in,1000);
		len = sizeof(cliaddr);
		n = recvfrom(sockfd,msg_in,1000,0,(struct sockaddr *)&cliaddr,&len);

		// Getting senders ip address and converting it to index
		addr = inet_ntoa(cliaddr.sin_addr);
		index1 = returnIndex(addr);
		printf("Recieved update from %s\n",nodes[index1]);

		// If the message is from a neigbour then kill its thread and restart it to reset the TTL to TTL
		if(node_neighbour[index1] == 1)
		{
			pthread_cancel(ttl_thread[index1]);
			pid_ttl = pthread_create( &ttl_thread[index1], NULL, manage_TTL, (void*) index1);
		}
		
		// Tokenize the incoming message and update the adj_list accordingly
		token = strtok(msg_in, " ");
		while(token != NULL)
		{
			index2 = returnIndex(token);
			token = strtok(NULL," ");
			sscanf(token, "%d", &n);
			adj_list[index1][index2] = n;
			token = strtok(NULL," ");
		}

		// Check consistency of the adj_list to the routing table
		correct_table(index1);

		// update the table and check if changes have been made to the routing table
		// if yes, send updated table to the neighbours immediately and reset send advertisement thread
		if(update_table()==1)
		{
			printf("Changes Made to table!!!\nUpdate sent to all other nodes!!!\n");
			send_advertisement();
			pthread_cancel(adv_thread);
			pid_adv = pthread_create( &adv_thread, NULL, manage_advertisements, NULL);
		}
	}
}

