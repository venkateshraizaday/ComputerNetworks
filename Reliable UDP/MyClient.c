#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

// queue variables to buffer out of order packets.
typedef struct node
{
	int seq_num;
	char *data;
	struct node *next;
} node;

node *head;

// Global variables
int sockfd;				// socket file descriptor
struct sockaddr_in servaddr,cliaddr;	// client and server addresses
socklen_t len;				// length of recieved file
int last_recv;				// the packet that was last recieved
int *packet_flag;			// the current state of each packet
int total_packets;			// the total packets in which the file will be recieved
int exit_flag = 0;			// the flag to end execution

// function to convert string to int.
int my_atoi(const char* snum)
{
    int idx, strIdx = 0, accum = 0, numIsNeg = 0;
    const unsigned int NUMLEN = (int)strlen(snum);

    /* Check if negative number and flag it. */
    if(snum[0] == 0x2d)
        numIsNeg = 1;

    for(idx = NUMLEN - 1; idx >= 0; idx--)
    {
        /* Only process numbers from 0 through 9. */
        if(snum[strIdx] >= 0x30 && snum[strIdx] <= 0x39)
            accum += (snum[strIdx] - 0x30) * pow(10, idx);

        strIdx++;
    }

    /* Check flag to see if originally passed -ve number and convert result if so. */
    if(!numIsNeg)
        return accum;
    else
        return accum * -1;
}

// to insert in a queue
void insert(int n,char data[])
{
	node *ptr,*prev,*temp;
	temp = (node*)malloc(sizeof(node));
	temp -> seq_num = n;
	temp -> data = data;
	if(head == NULL)
	{
		temp->next = NULL;
		head = temp;
	}
	else if(head->next == NULL || head->seq_num > n)
	{
		if(head->seq_num < n)
		{
			head->next = temp;
			temp->next = NULL;
		}
		else
		{
			temp->next = head;
			head= temp;
		}
	}
	else
	{
		ptr = head;
		while(ptr->seq_num < n && ptr->next != NULL)
		{
			prev = ptr;
			ptr = ptr->next;
		}
		if(ptr->seq_num > n)
		{
			temp->next = prev->next;
			prev->next = temp;
		}
		else
		{			
			ptr->next = temp;
			temp->next = NULL;
		}
	}
}

// to get value from the queue
char* get()
{
	char *ch;
	node *ptr= head;
	ch = head->data;
	head = head->next;
	free(ptr);
	return ch;
}

// this function decides what to do with recieved data and sets exit flag to 1 when last packet is recieved.
// if packet is the one that was required, print it and check for susequent packets in queue. send ack for next required packet.
// if packet has been recieved previously drop it and again send ack for required packet.
// if packet is an out of order packet, store it in queue and send ack for the next required packet.
void send_file(int seq_num, char data[])
{
	char msg_out[100],temp[10];

	if(seq_num == (last_recv+1))
	{
		//printf("1\n");
		printf("%s",data);
		fflush(stdout);
		last_recv++;
		packet_flag[seq_num - 101] = 1;
		while(packet_flag[last_recv-100] == 1)
		{
			//printf("2\n");
			printf("%s",get());
			fflush(stdout);
			last_recv++;
		}
		
		sprintf(temp,"%d",(last_recv+1));
		strcpy(msg_out,temp);
		strcat(msg_out,"\n1\n");
	}
	else if(seq_num < (last_recv + 1))
	{
		sprintf(temp,"%d",(last_recv+1));
		strcpy(msg_out,temp);
		strcat(msg_out,"\n1\n");
	}
	else
	{
		packet_flag[seq_num - 101] = 1;
		insert(seq_num,data);

		sprintf(temp,"%d",(last_recv+1));
		strcpy(msg_out,temp);
		strcat(msg_out,"\n1\n");
	}
	sendto(sockfd,msg_out,strlen(msg_out),0,(struct sockaddr *)&servaddr,sizeof(servaddr));
	if(last_recv == (100 + total_packets))
		exit_flag=1;
}

int main(int argc, char**argv)
{
	char msg_in[1400],data[1301];	// buffers for input and output data
	char request[100],temp[100];	// temp string buffers
	int i,j;			// counters
	int seq_num;			// sequence number of current packet.

	if (argc != 3)
	{
		printf("usage: <IP address> <Filename>\n");
		exit(1);
	}

	// initialize global variables
	exit_flag = 0;
	head = NULL;
	
	// creating socket and populating server information.
	sockfd=socket(AF_INET,SOCK_DGRAM,0);
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=inet_addr(argv[1]);
	servaddr.sin_port=htons(6789);

	// send first packet with filename and ack bit 0.
	strcpy(request,"100\n0\n");
	strcat(request,argv[2]);
	sendto(sockfd,request,strlen(request),0,(struct sockaddr *)&servaddr,sizeof(servaddr));

	// recieve server reply and store in buffer.
	bzero(request,100);
	int n=recvfrom(sockfd,request,100,0,(struct sockaddr *)&servaddr,&len);

	// get servers sequence number and total packets that will be sent.
	i=0;
	while(request[i] != '\n')
	{
		temp[i] = request[i];
		i++;
	}
	temp[i] = '\0';
	last_recv = atoi(temp);
	
	i++;
	j=0;
	while(request[i] != '\n')
	{
		temp[j] = request[i];
		j++;
		i++;
	}
	temp[j] = '\0';

	//initialize flags for incoming packets for their status check.
	total_packets = atoi(temp);
	packet_flag = malloc(total_packets * sizeof(int));
	for(i=0;i<total_packets;i++)
		packet_flag[i] = 0;
	
	// send acknowledgment for packet 101 to commence data transfer.
	strcpy(request,"101\n1\n");
	sendto(sockfd,request,strlen(request),0,(struct sockaddr *)&servaddr,sizeof(servaddr));

	while(exit_flag == 0)
	{
		// recieve message from server
		bzero(msg_in,1400);
		recvfrom(sockfd,msg_in,1400,0,(struct sockaddr *)&servaddr,&len);
		i=0;

		// parse the message to recieve sequence number and the data.
		while(msg_in[i] != '\n')
		{
			temp[i] = msg_in[i];
			i++;
		}
		temp[i] = '\0';
		seq_num = atoi(temp);
		i++;
		j=0;
		while(i<strlen(msg_in))
		{
			data[j]=msg_in[i];
			i++;
			j++;
		}
		data[j] = '\0';
		
		// pass the info to sendfile for it to print data and take proper action.
		send_file(seq_num,data);
	}
}
