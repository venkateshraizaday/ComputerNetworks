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
#include <math.h>
#include <pthread.h>
#include <time.h>

//Global Variables
typedef struct packet
{
	int packet_num;
	pthread_t tid;
	time_t start;
	time_t end;
} packet;

packet *packet_info;			// A variable to store structure array for timer associated with a packet.
int sockfd;				// Socket Descriptor
struct sockaddr_in servaddr,cliaddr;	// Client and server addresses
socklen_t len;				// length of file recieved
int total_packets;			// total 1300 byte packets that will be exchanged b/w server and client
int *packet_flag;			// status of a packet if it has been sent, acknowledged or not sent right now.
int file_size;				// the size of the file that needs to be sent.
char *file_data;			// the data of the file that needs to sent.
int last_ack;				// packet number that has been acknowledged by client
int window_size;			// size of the congestion window.
float residue;				// the float overhead in congestion avoidance phase.
int ssthresh;				// threshold of the slow start phase.
double RTT;				// Estimated round trip time.
double timeout;				// Timeout duration.
double deviation;			// Estimated deviation from RTT.

// Function to convert numeric strings to numbers.
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

// Function for timer threads to sleep for period equal to timeout.
// If timeout occurs, the congestion window is altered here and same packet is retransmitted.
void* Start_Timer(void *arg)
{
	char temp_chunk[1301],msg_out[1400],temp[100];
	int *i = arg;
	int j=0;

	sleep(timeout);

	ssthresh = window_size/2;
	window_size = 1;

	bzero(msg_out,1400);
	sprintf(temp,"%d",(*i)+101);
	temp[strlen(temp)] = '\n';
	strcpy(msg_out,temp);

	int start = (*i)*1300;
	int end = (((*i)+1)*1300) -1;
	for(j=0;j<1300;j++)
		temp_chunk[j]=file_data[start+j];
	temp_chunk[j] = '\0';
	strcat(msg_out,temp_chunk);
	sendto(sockfd,msg_out,strlen(msg_out),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
	
	pthread_create(&(packet_info[*i].tid),NULL,&Start_Timer,&(packet_info[*i].packet_num));
	packet_info[*i].start = time(NULL);
}

// Main method.
int main(int argc, char**argv)
{
	char msg_out[1400],msg_in[100];		// Input output buffers
	char temp[100],ch,temp_chunk[1301];	// Temporary string buffers
	char filename[100];			// stores filename from client
	char ack_bit;				// stores the acknowledgment bit of the current packet
	int ack_num;				// stores the acknowledgment number of the current packet
	int i,j;				// counters
	int sample_RTT;				// sample RTT calculation
	FILE *fp;				// file descriptor for file i/o.
	int exit_flag = 0;			// for termination of program.
	int count_slow_start = 0;		// keeping count of packets sent in slow start phase
	int count_congestion_avoidance = 0;	// keeping count of packets sent in congestion avoidance phase
	
	// Creating socket
	sockfd=socket(AF_INET,SOCK_DGRAM,0);
	
	// Populating server information
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(6789);
	bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
	
	//Initializing congestion window variables
	ssthresh = 32;
	residue = 0;
	window_size=1;

	//Initializing timeout and respective parameters.
	RTT = 3;
	deviation = 0;
	timeout = RTT + 4*deviation;

	while(exit_flag == 0)
	{
		// Preparing input buffer to recieve file.
		bzero(msg_in,100);
		len = sizeof(cliaddr);
		recvfrom(sockfd,msg_in,1300,0,(struct sockaddr *)&cliaddr,&len);
		
		// Parsing the recieved message to get the ack bit and number.
		i=0;
		while(msg_in[i]!='\n')
		{
			temp[i] = msg_in[i];
			i++;
		}
		temp[i]='\0';
		i++;
		ack_bit = msg_in[i];
		ack_num = atoi(temp);
		
		// if ack bit is 0 that means this is the first packet sent by client and has the filename in it.
		if(ack_bit == '0')
		{
			// Extract file name
			i=i+2;
			j=0;
			while(i<strlen(msg_in))
			{
				filename[j]=msg_in[i];
				j++;
				i++;
			}
			filename[j] = '\0';

			// open file and calculate its size and retrieve the data.
			if((fp = fopen(filename,"r")) == NULL)
				printf("Couldn't open file");
			else
			{
				i=0;
				while( (ch = fgetc(fp)) != EOF )
					i++;
		
				file_size = i;		
				file_data = malloc(i * sizeof(char));
				fclose(fp);

				fp=fopen(filename,"r");

				i=0;
				while( (ch = fgetc(fp)) != EOF )
				{
					file_data[i] = ch;
					i++;
				}
				file_data[i]= '\0';
				fclose(fp);
				//printf("%s\n",file_data);
				//fflush(stdout);
			}
			
			// calculate total packets and initialize flags and timer arrays accordingly.
			// send the information to client.
			total_packets = (int)ceil(file_size/1300);
			packet_flag = malloc(total_packets * sizeof(int));
			packet_info = (packet*)malloc(total_packets * sizeof(packet));
			for(i=0;i<total_packets;i++)
			{
				packet_flag[i] = 0;
				packet_info[i].packet_num = i;
			}
			strcpy(msg_out,"100\n");
			sprintf(temp,"%d",total_packets);
			last_ack++;
			temp[strlen(temp)] = '\n';
			strcat(msg_out,temp);
			sendto(sockfd,msg_out,strlen(msg_out),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
		}
		// when ack bit is 1, make last_ack to ack_num - 1
		else
		{
			last_ack = ack_num - 1;
			i=101;
			// stop timers for all acknowledged packets.
			// calculate fresh timeout w.r.t each packet acked.
			// modify congestion window size with repect to ssthresh.
			while(i<=last_ack)
			{
				if(packet_flag[i-101] != 2)
				{
					packet_flag[i-101] = 2;
					pthread_cancel(packet_info[i-101].tid);
					packet_info[i-101].end = time(NULL);

					sample_RTT = difftime(packet_info[i].end,packet_info[i].start);
					RTT = (0.875)*RTT + (0.125)*sample_RTT;
					deviation = (0.75)*deviation + (0.25)*abs(sample_RTT-RTT);
					timeout = RTT + 4*deviation;

					if(window_size < ssthresh)
					{
						window_size++;
						printf("\nSlow Start...\n");
						count_slow_start++;
					}
					else
					{
						residue = residue + 1/(float)window_size;
						window_size = (int)floor((double)window_size + residue);
						if(residue>1)
							residue = residue -1;
						printf("\nCongestion Avoidance...\n");
						count_congestion_avoidance++;
					}
				}
				i++;
			}
			// send all packets which were not previously sent in the current window.
			// also resend the packet ack_num even if it has been sent previously, this overcomes the need for 3 duplicate acks
			for(i=last_ack+1;i<=(last_ack+window_size);i++)
			{
				if(packet_flag[i-101] == 0 || i == (last_ack+1))
				{
					bzero(msg_out,1400);
					sprintf(temp,"%d",i);
					temp[strlen(temp)] = '\n';
					strcpy(msg_out,temp);

					bzero(temp_chunk,1301);
					int start = (i-101)*1300;
					int end = ((i-100)*1300)-1;
					for(j=0;j<1300;j++)
						temp_chunk[j]=file_data[start+j];
					temp_chunk[j] = '\0';
					strcat(msg_out,temp_chunk);
					sendto(sockfd,msg_out,strlen(msg_out),0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));

					packet_flag[i-101]=1;
					//start_timer
					pthread_create(&(packet_info[i-101].tid),NULL,&Start_Timer,&(packet_info[i-101].packet_num));
					packet_info[i-101].start = time(NULL);
				}
			}
		}
		
		if (ack_num == 101+total_packets)
			exit_flag++;
	}
	printf("Number of packets in slow start:%d\nNumber of packets in congestion avoidance:%d\n",count_slow_start,count_congestion_avoidance);
}
