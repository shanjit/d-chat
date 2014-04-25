/*
to do:
1. HeartBeat + Failure detection 
2. Leader election 
*/

//----------------------------------------------------------------------------------------//

#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <mutex>
#include <string.h>
#include <condition_variable>
#include <list>
#include <typeinfo>

//----------------------------------------------------------------------------------------//

using namespace std;

//----------------------------------------------------------------------------------------//

// constants for mode identification
#define LEADER 100
#define OTHER 101

#define SA struct sockaddr

#define BUFLEN 512

#define NAMELEN 16
//----------------------------------------------------------------------------------------//

// interface socket
int sockfd; 

// mode telling LEADER or OTHER
char operating_mode; 

//username
char username[NAMELEN]; 

//----------------------------------------------------------------------------------------//

// --- global datastructures --- //

//----------------------------------------------------------------------------------------//

// --- application layer packet layout --- //
class app_packet
{
public:
	uint8_t control_seq;
	uint8_t seq_number;
	uint8_t ack_number;
	uint8_t symbol;
	char payload[256];
};

//----------------------------------------------------------------------------------------//

// --- list of nodes in the group --- //
class node_information
{
public:
	struct sockaddr_in address; // stores socket information; ip, port
	bool status; // active or inactive
	char nodename[NAMELEN];
};

// vector data structure to hold information of nodes
vector<node_information> nodelist; 

// mutex to achieve mutual exclusion when accessing nodelist
mutex nodelist_mtx;

//----------------------------------------------------------------------------------------//

// --- send message queue --- //
class message_information
{
public:
	uint8_t no_of_sent_times;
	time_t last_sent_time;
	struct sockaddr_in address;
	char packet[BUFLEN];
};

// queue data structure to store messages to send
queue<message_information> send_message_queue;

// mutex to acheive mutual exclusion when accessing send queue
mutex send_message_queue_mtx;

list<message_information> ack_message_list;

mutex ack_message_list_mtx; 

//----------------------------------------------------------------------------------------//

// --- receive message queue --- //

// queue data structure to store received messages
queue<message_information> receive_message_queue;

// mutex to achieve mutual exclusion when accessing receive queue
mutex receive_message_queue_mtx;

//----------------------------------------------------------------------------------------//

// --- display queue --- //
class display_content
{
public:
	char message_contents[256];
};

// queue data structure to store messages for display
queue<display_content> display_message_queue;

// mutex to acheive mutual exclsuion when accessing display queue
mutex display_message_queue_mtx;

// conditional variable to avoid busy waiting on the send_message_mtx
condition_variable send_message_cv;

// conditional variable to avoid busy waiting on the receive_message_mtx
condition_variable receive_message_cv;

// conditional variable to avoid busy waiting on the display_message_mtx
condition_variable display_message_cv;

// conditional variable to avoid busy waiting on the ack_message_mtx
condition_variable ack_message_cv;

// Global symbol to send with each ACK
uint8_t global_symbol=1; 

// Mutex to achieve mutual exclusion when accessing global_symbol
mutex global_symbol_mtx;

// Sequence number for each packet sent by the leader
uint8_t leader_seq_number = 1;

// Mutex to achieve mutual exclusion when accessing the leader_seq_number
mutex leader_seq_number_mtx;

// Saving the last sequence number for each packet received
uint8_t old_leader_seq_number;


//----------------------------------------------------------------------------------------//

// --- thread handling sending of data --- //
void send_function()
{	
	message_information send_message;
	char send_raw_packet[BUFLEN];
	app_packet *send_packet;
	
	for(;;)
	{	
		memset(send_raw_packet, 0, BUFLEN);
		unique_lock<mutex> send_lk_send(send_message_queue_mtx);
		while(send_message_queue.empty())
		{
			send_message_cv.wait(send_lk_send);
		}
		send_message = send_message_queue.front();
		send_message_queue.pop();
		send_lk_send.unlock();

		// Send the message
		while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);
		
		// Give timestamp to each message sent
		send_message.last_sent_time = time(0);

		// Push the message onto the ack message list
		ack_message_list_mtx.lock();
		ack_message_list.push_back(send_message);
		ack_message_list_mtx.unlock();
		ack_message_cv.notify_all();

	}


}

// --- thread handling reciept of data --- //
void receive_function()
{	
	int n;
	struct sockaddr_in nodeaddr;
	socklen_t len;
	len = sizeof(nodeaddr);
	char mesg[BUFLEN];
	for(;;)
	{
		n = recvfrom(sockfd, mesg, BUFLEN, 0, (SA *)&nodeaddr, &len);
		message_information message;
		message.address = nodeaddr;
		strncpy(message.packet, mesg, strlen(mesg));
		receive_message_queue_mtx.lock();
		receive_message_queue.push(message);
		receive_message_queue_mtx.unlock();
		receive_message_cv.notify_all();
		memset(mesg, 0, BUFLEN);
		memset(&message, 0, sizeof(class message_information));
	}
}

// --- thread handling parsing of received data ---//
void parse_function()
{	
	// Variables for each read message
	message_information read_message;
	app_packet *read_packet;
	struct sockaddr_in read_node_address;
	char read_servip[20];
	char read_port[20];
	char read_nodename[NAMELEN];

	// Variables for each to be sent message
	message_information send_message;
	char send_raw_packet[BUFLEN];
	app_packet *send_packet = (app_packet *)send_raw_packet;
	struct sockaddr_in send_node_address;
	char send_servip[20];
	char send_port[20];	
	char send_nodename[NAMELEN];

	int nodelist_ptr = 0;

	for(;;)
	{
		unique_lock<mutex> receive_lk(receive_message_queue_mtx);
		
		while(receive_message_queue.empty())
		{
			receive_message_cv.wait(receive_lk);
		}

		read_message = receive_message_queue.front();
		receive_message_queue.pop();
		receive_lk.unlock();
		read_packet = (struct app_packet *)read_message.packet;

		// start parsing packet based on control_seq number

		// change global_symbol when above 150 to avoid global_symbol = 0
		global_symbol_mtx.lock();
		if (global_symbol>150)
		{
			global_symbol = 1;
		}
		global_symbol_mtx.unlock();

		switch(read_packet->control_seq)
		{
			case 10:	if(operating_mode == LEADER)
						{
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
							memset(&send_message, 0, sizeof(class message_information));
							memset(&send_node_address, 0, sizeof(send_node_address));
							memset(&send_nodename, 0, sizeof(send_nodename));
							memset(&send_port, 0, sizeof(send_port));
							memset(&send_servip, 0, sizeof(send_servip));	

							// Sending the ACK
							send_packet->control_seq = 50;
							send_packet->seq_number = 100;
							send_packet->ack_number = read_packet->symbol+1;
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();
							send_message.address = read_message.address;
							send_message.no_of_sent_times = 1;
							strcpy(send_message.packet, send_raw_packet);
							while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);	


							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							

							// Sending new node information to all nodes
							send_packet->control_seq = 20;
							send_packet->seq_number = 100;
							send_packet->ack_number = 100;
							send_node_address = read_message.address;
							send_message.no_of_sent_times = 1;
							inet_ntop(AF_INET, &send_node_address.sin_addr, send_servip, 20);
							strcpy(send_message.packet, send_raw_packet);
							
							for(int i = 1 ; i < nodelist.size() ; i++)
							{	
								global_symbol_mtx.lock();
								send_packet->symbol = global_symbol;
								global_symbol = global_symbol + 2;
								global_symbol_mtx.unlock();
								strcpy(send_message.packet, send_raw_packet);
								send_message.address = nodelist[i].address;
								send_message.no_of_sent_times = 1;
								send_message_queue_mtx.lock();
								send_message_queue.push(send_message);
								send_message_queue_mtx.unlock();
								send_message_cv.notify_all();
							}

							display_content data;
							strcpy(data.message_contents, send_packet->payload);
							display_message_queue_mtx.lock();
							display_message_queue.push(data);
							display_message_queue_mtx.unlock();
							display_message_cv.notify_all();

							// add a new member to the node list
							node_information node_info;
					    	node_info.address = read_message.address;
					    	node_info.status = true;
					    	strcpy(node_info.nodename, read_packet->payload);
					    	nodelist_mtx.lock();
					    	nodelist.push_back(node_info);
					    	nodelist_mtx.unlock();	

					    	
					    	memset(send_raw_packet, 0, BUFLEN);
					    	send_packet->control_seq = 11;
							send_packet->seq_number = 100;
							send_packet->ack_number = 100;
							
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();

							strcpy(send_message.packet, send_raw_packet);
							send_message.no_of_sent_times = 1;
							send_message.address = read_message.address;
							send_message_queue_mtx.lock();
							send_message_queue.push(send_message);
							send_message_queue_mtx.unlock();
							send_message_cv.notify_all();							

							for(int i = 0 ; i < nodelist.size() ; i++)
					    	{
					    		memset(send_raw_packet, 0, BUFLEN);
					    		send_packet->control_seq = 12;
								send_packet->seq_number = 100;
								send_packet->ack_number = 100;
								global_symbol_mtx.lock();
								send_packet->symbol = global_symbol;
								global_symbol = global_symbol + 2;
								global_symbol_mtx.unlock();
								send_node_address = nodelist[i].address;
								inet_ntop(AF_INET, &send_node_address.sin_addr, send_servip, 20);
								sprintf(send_packet->payload, "%s:%d:%s", send_servip, ntohs(send_node_address.sin_port), nodelist[i].nodename);
								strcpy(send_message.packet, send_raw_packet);
								send_message.address = read_message.address;
								send_message.no_of_sent_times = 1;
								send_message_queue_mtx.lock();
								send_message_queue.push(send_message);
								send_message_queue_mtx.unlock();
								send_message_cv.notify_all();
					    	}

						}

						else if (operating_mode == OTHER)
						{
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
							send_packet->control_seq = 50;
							send_packet->seq_number = 100;
							send_packet->ack_number = read_packet->symbol+1;
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();
							send_message.address = read_message.address;
							send_message.no_of_sent_times = 1;								
							strcpy(send_message.packet, send_raw_packet);
							while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);	
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
					    	send_packet->control_seq = 13;
							send_packet->seq_number = 100;
							send_packet->ack_number = 100;
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();
							send_node_address = nodelist[0].address;
							inet_ntop(AF_INET, &send_node_address.sin_addr, send_servip, 20);
							sprintf(send_packet->payload, "%s:%d:%s", send_servip, ntohs(send_node_address.sin_port), read_packet->payload);
							send_message.no_of_sent_times = 1;
							strcpy(send_message.packet, send_raw_packet);
							send_message.address = read_message.address;
							send_message_queue_mtx.lock();
							send_message_queue.push(send_message);
							send_message_queue_mtx.unlock();
							send_message_cv.notify_all();
						}
						break;

			case 20:	
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
							send_packet->control_seq = 50;
							send_packet->seq_number = 101;
							send_packet->ack_number = read_packet->symbol+1;
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();
							send_message.address = read_message.address;
							send_message.no_of_sent_times = 1;
							strcpy(send_message.packet, send_raw_packet);
							while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);	

							if(operating_mode == LEADER)
							{	
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
						
							for(int i = 1 ; i < nodelist.size() ; i++)
							{
								read_message.address = nodelist[i].address;
								char *ch[512] = {read_message.packet};
								*ch = (char *) read_packet;
								global_symbol_mtx.lock();
								send_packet->symbol = global_symbol;
								global_symbol = global_symbol + 2;
								global_symbol_mtx.unlock();
								read_message.no_of_sent_times = 1;
								strcpy(send_message.packet, send_raw_packet);
								send_message_queue_mtx.lock();
								send_message_queue.push(read_message);
								send_message_queue_mtx.unlock();
								send_message_cv.notify_all();
							}
							
						}

						display_content data;
						strcpy(data.message_contents, read_packet->payload);
						display_message_queue_mtx.lock();
						display_message_queue.push(data);
						display_message_queue_mtx.unlock();
						display_message_cv.notify_all();
						break;

			case 11:	
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
							send_packet->control_seq = 50;
							send_packet->seq_number = 100;
							send_packet->ack_number = read_packet->symbol+1;
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();
							send_message.address = read_message.address;
							send_message.no_of_sent_times = 1;
							strcpy(send_message.packet, send_raw_packet);
							while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);	
							nodelist.clear();
							cout << "pointer set to 0" << endl;
							break;

			case 12:	
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
							send_packet->control_seq = 50;
							send_packet->seq_number = 100;
							send_packet->ack_number = read_packet->symbol+1;
							send_message.no_of_sent_times = 1;
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();
							send_message.address = read_message.address;
							send_message.no_of_sent_times = 1;
							strcpy(send_message.packet, send_raw_packet);
							while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);	
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
						
							node_information node_info;
							sscanf(read_packet->payload, "%20[^:]:%5s:%s", read_servip, read_port, read_nodename);
							inet_pton(AF_INET, read_servip, &read_node_address.sin_addr);
							read_node_address.sin_port = htons(atoi(read_port));
							node_info.address = read_node_address;
							node_info.status = true;
							strcpy(node_info.nodename, read_nodename);
							nodelist_mtx.lock();
					   		nodelist.push_back(node_info); // add new member to node list
					    	nodelist_mtx.unlock();	
						break;
			
			case 13:		
							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
						
							cout <<"sending ack for code 13"<<endl;
							send_packet->control_seq = 50;
							send_packet->seq_number = 100;
							send_packet->ack_number = read_packet->symbol+1;							
							global_symbol_mtx.lock();
							send_packet->symbol = global_symbol;
							global_symbol = global_symbol + 2;
							global_symbol_mtx.unlock();
							send_message.address = read_message.address;
							send_message.no_of_sent_times = 1;
							strcpy(send_message.packet, send_raw_packet);
							while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);	


							memset(&send_raw_packet, 0, sizeof(send_raw_packet));							
						sscanf(read_packet->payload, "%20[^:]:%5s:%s", read_servip, read_port, read_nodename);
						inet_pton(AF_INET, read_servip, &read_node_address.sin_addr);
						read_node_address.sin_port = htons(atoi(read_port));
						
						memset(send_raw_packet, 0, BUFLEN);
						send_packet->control_seq = 10;
						send_packet->seq_number = 100;
						send_packet->ack_number = 100;
						
						global_symbol_mtx.lock();
						send_packet->symbol = global_symbol;
						global_symbol = global_symbol + 2;
						global_symbol_mtx.unlock();
						strcpy(send_packet->payload, read_nodename);
						send_message.address = read_node_address;
						send_message.no_of_sent_times = 1;
						strcpy(send_message.packet, send_raw_packet);
						while(sendto(sockfd, send_message.packet, strlen(send_message.packet), 0, (SA *) &send_message.address, sizeof(send_message.address)) < 0);	
						break;

			case 50: 	
						message_information ack_message;
						char ack_raw_packet[BUFLEN];
						app_packet *ack_packet;
						ack_packet = (app_packet *)ack_raw_packet;
						ack_packet = (struct app_packet *)ack_message.packet;

						uint8_t read_packet_seq_number;
						uint8_t read_packet_ack_number;
						uint8_t read_packet_symbol;
						uint8_t read_packet_no_of_sent_times;

						uint8_t ack_packet_seq_number;
						uint8_t ack_packet_ack_number;
						uint8_t ack_packet_symbol;
						uint8_t ack_packet_no_of_sent_times;
						char ack_packet_payload[BUFLEN];

						ack_message_list_mtx.lock();

						for (list<message_information>::iterator it = ack_message_list.begin(); it != ack_message_list.end(); it++)
						{
							ack_packet = (struct app_packet *) (*it).packet;
							
							if(((read_packet->ack_number - ack_packet->symbol) == 1))//&&(ack_packet->symbol!=0))
							{
								it = ack_message_list.erase(it);
								break; 
							}
					}
					ack_message_list_mtx.unlock();
						break;

		}

	}
}

void ack_function()
{
	message_information ack_message;
	char ack_raw_packet[BUFLEN];
	app_packet *send_packet;
	
	for(;;)
	{	
		memset(ack_raw_packet, 0, BUFLEN);
		unique_lock<mutex> ack_lk(ack_message_list_mtx);
		while(ack_message_list.empty())
		{
			ack_message_cv.wait(ack_lk);
		}


		for (list<message_information>::iterator it = ack_message_list.begin(); it != ack_message_list.end(); it++)
			{


				if (((*it).no_of_sent_times>3))
				{
					it = ack_message_list.erase(it);
				}

				else if (((*it).no_of_sent_times<=3) && ((time(0) - (*it).last_sent_time) > 1))
				{
					ack_message = ack_message_list.front();
					ack_message.no_of_sent_times = ack_message.no_of_sent_times + 1;
					it = ack_message_list.erase(it);
					send_message_queue_mtx.lock();
					send_message_queue.push(ack_message);
					send_message_queue_mtx.unlock();
					send_message_cv.notify_all();
				}


			}


	}

}
// --- thread handling heartbeat --- //
void heartbeat_function()
{

}

// --- thread handling display --- //
void display_function()
{	
	display_content data;
	for(;;)
	{
		unique_lock<mutex> display_lk(display_message_queue_mtx);
		
		while(display_message_queue.empty())
		{
			display_message_cv.wait(display_lk);
		}

		data = display_message_queue.front();
		display_message_queue.pop();
		display_lk.unlock();
		cout << data.message_contents << endl;

	}
}

// --- thread handling user input --- //
void user_function()
{	
	char chat_message[256];
	for(;;)
	{
		cin.getline(chat_message, 256);
		message_information message;
		char raw_packet[BUFLEN];
		app_packet *packet = (app_packet *)raw_packet;
		memset(raw_packet, 0, BUFLEN);
		packet->control_seq = 20;
		packet->seq_number = 100;
		packet->ack_number = 100;
		global_symbol_mtx.lock();
		packet->symbol = global_symbol;
		global_symbol = global_symbol + 2;
		global_symbol_mtx.unlock();	
		sprintf(packet->payload, "%s: %s", username, chat_message);
		strcpy(message.packet, raw_packet);
		message.no_of_sent_times = 1;
		message.address = nodelist[0].address; 
		send_message_queue_mtx.lock();
		send_message_queue.push(message);
		send_message_queue_mtx.unlock();
		send_message_cv.notify_all();
		memset(chat_message, 0, 256);
	}
}

// --- main thread --- //
int main(int argc, char *argv[])
{
	if(argc<2)
	{
		cout << "usage: \t if initiating chat: " << argv[0] << " <username>" << endl;
		cout << " \t if connecting to group : " << argv[0] << " <usernamename> <ip>:<port>" << endl;
		return 0;
	}
	else if(argc==2)
	{
		operating_mode = LEADER;
		strncpy(username, argv[1], 16);
	}
	else if(argc == 3)
	{
		operating_mode = OTHER;
		strncpy(username, argv[1], 16);
	}
	else
	{
		cout << "usage: \t if initiating chat: " << argv[0] << " <name>" << endl;
		cout << " \t if connecting to group : " << argv[0] << " <name> <ip of member>:<port of member>" << endl;
		return 0;
	}


	// --- find out default IP used for communication ---- //
	const char* google_dns_server = "8.8.8.8";
    int dns_port = 53;
    struct sockaddr_in serv; 
    int sock = socket (AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)
    {
        perror("Socket error");
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(google_dns_server);
    serv.sin_port = htons(dns_port);
    int err = connect(sock , (const struct sockaddr*) &serv , sizeof(serv));
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    err = getsockname(sock, (struct sockaddr*) &name, &namelen); 
    char buffer[100];
    const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, 100);   
    if(p == NULL)
    {
        //Some error
        printf ("Error number : %d . Error message : %s \n" , errno , strerror(errno));
    }
    close(sock);

    // --- create and bind socket for communication --- //
	char datagram[BUFLEN];
	struct sockaddr_in cliaddr, leaderaddr;

	if(operating_mode == LEADER)
	{
		socklen_t len = sizeof(leaderaddr);
		char servip[20];
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		// --- clear out memory and assign IP parameters --- //
		memset((char *) &leaderaddr, 0, sizeof(leaderaddr));
		leaderaddr.sin_family = AF_INET;
		leaderaddr.sin_addr.s_addr = inet_addr(buffer);
		leaderaddr.sin_port = htons(40000);
		// --- bind socket --- //
		bind(sockfd, (SA *) &leaderaddr, sizeof(leaderaddr));
    	err = getsockname(sock, (SA *) &leaderaddr, &len);
    	// --- print out initialization status --- //
    	inet_ntop(AF_INET, &leaderaddr.sin_addr, servip, 20);
    	cout << argv[1] << " started a new chat, listening on " << servip << ":" << ntohs(leaderaddr.sin_port) << endl;
    	node_information node_info;
    	node_info.address = leaderaddr;
    	node_info.status = true;    	
    	strcpy(node_info.nodename, argv[1]);
		nodelist_mtx.lock();
    	nodelist.push_back(node_info); // add leader to node list
    	nodelist_mtx.unlock();
	}
	else if(operating_mode == OTHER)
	{
		char servip[20];
		char servport[20];
		char cliip[20];
		socklen_t len = sizeof(cliaddr);
		sscanf(argv[2], "%15[^:]:%s", servip, servport);

		// --- leader information --- //
		// --- clear out memory and assign IP parameters --- //
		memset((char *) &leaderaddr, 0, sizeof(leaderaddr));
		leaderaddr.sin_family = AF_INET;
		inet_pton(AF_INET, servip, &leaderaddr.sin_addr);		
		leaderaddr.sin_port = htons(atoi(servport));

		// --- client initialization --- //
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		// --- clear out memory and assign IP parameters --- //
		memset((char *) &cliaddr, 0, sizeof(cliaddr));
		cliaddr.sin_family = AF_INET;
		cliaddr.sin_addr.s_addr = inet_addr(buffer);
		cliaddr.sin_port = htons(INADDR_ANY);
		// --- bind socket --- //
		bind(sockfd, (SA *) &cliaddr, sizeof(cliaddr));
    	err = getsockname(sock, (SA *) &cliaddr, &len);
    	// --- print out initialization status --- //
    	inet_ntop(AF_INET, &leaderaddr.sin_addr, servip, 20);
    	inet_ntop(AF_INET, &cliaddr.sin_addr, cliip, 20);
    	cout << argv[1] << " joining a new chat on " << servip << ":" << ntohs(leaderaddr.sin_port) << ", listening on " << cliip << ":" << ntohs(cliaddr.sin_port) << endl;
		
		message_information message;
		message.no_of_sent_times = 1;
		char raw_packet[BUFLEN];
		app_packet *packet = (app_packet *)raw_packet;
		memset(raw_packet, 0, BUFLEN);
		packet->control_seq = 10;
		packet->seq_number = 100;
		packet->ack_number = 100;
		global_symbol_mtx.lock();
		packet->symbol = global_symbol;
		global_symbol = global_symbol + 2;
		global_symbol_mtx.unlock();
		strcpy(packet->payload, argv[1]);
		message.address = leaderaddr;
		message.no_of_sent_times = 1;
		strcpy(message.packet, raw_packet);
		send_message_queue_mtx.lock();
		send_message_queue.push(message);
		send_message_queue_mtx.unlock();
	}


	// --- create threads for sending and recieving data, heartbeat thread --- //
	thread send_thread (send_function);
	thread receive_thread (receive_function);
	thread parse_thread (parse_function);
	thread heartbeat_thread (heartbeat_function);
	thread display_thread (display_function);
	thread user_interface (user_function);
	thread ack_thread(ack_function);

	// --- join threads to main thread --- //
	send_thread.join();
	receive_thread.join();
	heartbeat_thread.join();
	display_thread.join();
	parse_thread.join();
	ack_thread.join();


	return 0;
}