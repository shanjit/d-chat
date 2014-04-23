/*
to do:
1. ACK + Sequencing + New Thread
2. HeartBeat + Failure detection 
3. Leader election
4. 
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
	u_char control_seq;
	u_char seq_number;
	u_char ack_number;
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
	struct sockaddr_in address;
	char packet[BUFLEN];
};

// queue data structure to store messages to send
queue<message_information> send_message_queue;

// mutex to acheive mutual exclusion when accessing send queue
mutex send_message_queue_mtx;



//----------------------------------------------------------------------------------------//

// --- unacknowledged message queue --- //

// to be filled 

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

condition_variable send_message_cv;

condition_variable receive_message_cv;

condition_variable display_message_cv;

//----------------------------------------------------------------------------------------//

// --- thread handling sending of data --- //
void send_function()
{	
	message_information message;
	for(;;)
	{
		unique_lock<mutex> send_lk(send_message_queue_mtx);
		while(send_message_queue.empty())
		{
			send_message_cv.wait(send_lk);
		}
		message = send_message_queue.front();
		send_message_queue.pop();
		send_message_queue_mtx.unlock();
		while(sendto(sockfd, message.packet, strlen(message.packet), 0, (SA *) &message.address, sizeof(message.address)) < 0);
		
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
	message_information read_message;
	app_packet *read_packet;	
	int nodelist_ptr = 0;

	for(;;)
	{
		// while(receive_message_queue.empty()); // wait until something is added
	
		unique_lock<mutex> receive_lk(receive_message_queue_mtx);
		
		while(receive_message_queue.empty())
		{
			receive_message_cv.wait(receive_lk);
		}

		read_message = receive_message_queue.front();
		receive_message_queue.pop();
		receive_message_queue_mtx.unlock();
		read_packet = (struct app_packet *)read_message.packet;

		// start parsing packet based on control_seq number

		switch(read_packet->control_seq)
		{
			case 10:	if(operating_mode == LEADER)
						{
							message_information message;
							char raw_packet[BUFLEN];
							app_packet *packet = (app_packet *)raw_packet;
							char servip[20];
							struct sockaddr_in node_address;
							memset(raw_packet, 0, BUFLEN);
							packet->control_seq = 20;
							packet->seq_number = 100;
							packet->ack_number = 100;
							node_address = read_message.address;
							inet_ntop(AF_INET, &node_address.sin_addr, servip, 20);
							sprintf(packet->payload, "NOTICE %s joined chat on %s:%d", read_packet->payload, servip, ntohs(node_address.sin_port));
							strcpy(message.packet, raw_packet);
							for(int i = 1 ; i < nodelist.size() ; i++)
							{
								message.address = nodelist[i].address;
								send_message_queue_mtx.lock();
								send_message_queue.push(message);
								send_message_queue_mtx.unlock();
								send_message_cv.notify_all();
							}
							display_content data;
							strcpy(data.message_contents, packet->payload);
							display_message_queue_mtx.lock();
							display_message_queue.push(data);
							display_message_queue_mtx.unlock();
							display_message_cv.notify_all();

							node_information node_info;
					    	node_info.address = read_message.address;
					    	node_info.status = true;
					    	strcpy(node_info.nodename, read_packet->payload);
					    	nodelist_mtx.lock();
					    	nodelist.push_back(node_info); // add new member to node list
					    	nodelist_mtx.unlock();	

					    	
					    	memset(raw_packet, 0, BUFLEN);
					    	packet->control_seq = 11;
							packet->seq_number = 100;
							packet->ack_number = 100;
							strcpy(message.packet, raw_packet);
							message.address = read_message.address;								
							send_message_queue_mtx.lock();
							send_message_queue.push(message);
							send_message_queue_mtx.unlock();
							send_message_cv.notify_all();
							
					    	for(int i = 0 ; i < nodelist.size() ; i++)
					    	{
					    		memset(raw_packet, 0, BUFLEN);
					    		packet->control_seq = 12;
								packet->seq_number = 100;
								packet->ack_number = 100;
								node_address = nodelist[i].address;
								inet_ntop(AF_INET, &node_address.sin_addr, servip, 20);
								sprintf(packet->payload, "%s:%d:%s", servip, ntohs(node_address.sin_port), nodelist[i].nodename);
								strcpy(message.packet, raw_packet);
								message.address = read_message.address;
								send_message_queue_mtx.lock();
								send_message_queue.push(message);
								send_message_queue_mtx.unlock();
								send_message_cv.notify_all();
					    	}

						}
						break;

			case 20:	if(operating_mode == LEADER)
						{
							for(int i = 1 ; i < nodelist.size() ; i++)
							{
								read_message.address = nodelist[i].address;
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

			case 11:	nodelist.clear();
						cout << "pointer set to 0" << endl;
						break;

			case 12:	char servip[20];
						char port[20];
						char nodename[NAMELEN];
						node_information node_info;
						struct sockaddr_in address;
						sscanf(read_packet->payload, "%20[^:]:%s:%s", servip, port, nodename);
						inet_pton(AF_INET, servip, &address.sin_addr);
						address.sin_port = htons(atoi(port));
						node_info.address = address;
						node_info.status = true;
						strcpy(node_info.nodename, nodename);
						nodelist_mtx.lock();
					    nodelist.push_back(node_info); // add new member to node list
					    nodelist_mtx.unlock();	
						cout << servip << ":" << port << endl;
						break;

		}

		//memset(&read_message, 0, sizeof(class message_information));
		//memset(read_packet, 0, sizeof(struct app_packet));
		/*cout << packet->control_seq << endl;
		cout << packet->seq_number << endl;
		cout << packet->ack_number << endl;
		cout << packet->payload << endl;
		//cout << packet->payload;*/
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
		//while(display_message_queue.empty());
		unique_lock<mutex> display_lk(display_message_queue_mtx);
		
		while(display_message_queue.empty())
		{
			display_message_cv.wait(display_lk);
		}
//		display_message_queue_mtx.lock();
		data = display_message_queue.front();
		display_message_queue.pop();
		cout << data.message_contents << endl;
		display_message_queue_mtx.unlock();

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
		sprintf(packet->payload, "%s: %s", username, chat_message);
		strcpy(message.packet, raw_packet);
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
		char raw_packet[BUFLEN];
		app_packet *packet = (app_packet *)raw_packet;
		memset(raw_packet, 0, BUFLEN);
		packet->control_seq = 10;
		packet->seq_number = 100;
		packet->ack_number = 100;
		strcpy(packet->payload, argv[1]);
		message.address = leaderaddr;
		strcpy(message.packet, raw_packet);
		while(sendto(sockfd, message.packet, strlen(message.packet), 0, (SA *) &message.address, sizeof(message.address)) < 0);	
	}


	// --- create threads for sending and recieving data, heartbeat thread --- //
	thread send_thread (send_function);
	thread receive_thread (receive_function);
	thread parse_thread (parse_function);
	thread heartbeat_thread (heartbeat_function);
	thread display_thread (display_function);
	thread user_interface (user_function);


	// --- join threads to main thread --- //
	send_thread.join();
	receive_thread.join();
	heartbeat_thread.join();
	display_thread.join();
	parse_thread.join();



	return 0;
}
