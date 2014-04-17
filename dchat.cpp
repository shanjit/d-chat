/*
to do:
1. remove busy waiting from parse function thread

*/

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

using namespace std;

#define LEADER 100
#define OTHER 101

#define SA struct sockaddr

#define BUFLEN 512

int sockfd;
char operating_mode;
char username[16];

// --- global datastructures --- //

// --- application layer packet layout --- //
struct app_packet
{
	u_char control_seq;
	u_char seq_number;
	u_char ack_number;
	char payload[256];
};

// --- list of nodes in the group --- //
class node_information
{
	public:
	struct sockaddr_in address; // stores socket information; ip, port
	bool status; // active or inactive
};
vector<node_information> nodelist;
mutex nodelist_mtx;

// --- send message queue --- //
class message_information
{
	public:
	struct sockaddr_in address;
	char packet[BUFLEN];
};
queue<message_information> send_message_queue;
mutex send_message_queue_mtx;

// --- unacknowledged message queue --- //


// --- receive message queue --- //
queue<message_information> recieve_message_queue;
mutex recieve_message_queue_mtx;

// --- display queue --- //
struct display_content
{
	char message_contents[256];
};
queue<display_content> display_message_queue;
mutex display_message_queue_mtx;

// --- thread handling sending of data --- //
void send_function()
{
	message_information message;
	for(;;)
	{
		while(send_message_queue.empty());
		send_message_queue_mtx.lock();
		message = send_message_queue.front();
		send_message_queue.pop();
		send_message_queue_mtx.unlock();
		while(sendto(sockfd, message.packet, strlen(message.packet), 0, (SA *) &message.address, sizeof(message.address)) < 0);
	}

}

// --- thread handling reciept of data --- //
void recieve_function()
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
		recieve_message_queue_mtx.lock();
		recieve_message_queue.push(message);
		recieve_message_queue_mtx.unlock();
		memset(mesg, 0, BUFLEN);
		memset(&message, 0, sizeof(class message_information));
	}
}

// --- thread handling parsing of recieved data ---//
void parse_function()
{
	message_information read_message;
	struct app_packet *read_packet;	

	for(;;)
	{
		while(recieve_message_queue.empty()); // wait until something is added
											
		
		recieve_message_queue_mtx.lock();
		read_message = recieve_message_queue.front();
		recieve_message_queue.pop();
		recieve_message_queue_mtx.unlock();
		read_packet = (struct app_packet *)read_message.packet;

		// start parsing packet based on control_seq number

		switch(read_packet->control_seq)
		{
			case 10:	if(operating_mode == LEADER)
						{
							message_information message;
							char raw_packet[BUFLEN];
							struct app_packet *packet = (struct app_packet *)raw_packet;
							memset(raw_packet, 0, BUFLEN);
							packet->control_seq = 20;
							packet->seq_number = 100;
							packet->ack_number = 100;
							sprintf(packet->payload, "%s joined chat", read_packet->payload);
							strcpy(message.packet, raw_packet);
							for(int i = 1 ; i < nodelist.size() ; i++)
							{
								message.address = nodelist[i].address;
								send_message_queue_mtx.lock();
								send_message_queue.push(message);
								send_message_queue_mtx.unlock();
							}
							display_content data;
							strcpy(data.message_contents, packet->payload);
							display_message_queue_mtx.lock();
							display_message_queue.push(data);
							display_message_queue_mtx.unlock();

							node_information node_info;
					    	node_info.address = read_message.address;
					    	node_info.status = true;
					    	nodelist_mtx.lock();
					    	nodelist.push_back(node_info); // add new member to node list
					    	nodelist_mtx.unlock();	
						}
						break;

			case 20:	display_content data;
						strcpy(data.message_contents, read_packet->payload);
						display_message_queue_mtx.lock();
						display_message_queue.push(data);
						display_message_queue_mtx.unlock();
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
		while(display_message_queue.empty());
		display_message_queue_mtx.lock();
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
		struct app_packet *packet = (struct app_packet *)raw_packet;
		memset(raw_packet, 0, BUFLEN);
		packet->control_seq = 20;
		packet->seq_number = 100;
		packet->ack_number = 100;
		sprintf(packet->payload, "%s", chat_message);
		strcpy(message.packet, raw_packet);
		message.address = nodelist[0].address;
		send_message_queue_mtx.lock();
		send_message_queue.push(message);
		send_message_queue_mtx.unlock();
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
		leaderaddr.sin_port = htons(INADDR_ANY);
		// --- bind socket --- //
		bind(sockfd, (SA *) &leaderaddr, sizeof(leaderaddr));
    	err = getsockname(sock, (SA *) &leaderaddr, &len);
    	// --- print out initialization status --- //
    	inet_ntop(AF_INET, &leaderaddr.sin_addr, servip, 20);
    	cout << argv[1] << " started a new chat, listening on " << servip << ":" << ntohs(leaderaddr.sin_port) << endl;
    	node_information node_info;
    	node_info.address = leaderaddr;
    	node_info.status = true;
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
		struct app_packet *packet = (struct app_packet *)raw_packet;
		memset(raw_packet, 0, BUFLEN);
		packet->control_seq = 10;
		packet->seq_number = 100;
		packet->ack_number = 100;
		strcpy(packet->payload, argv[1]);
		message.address = leaderaddr;
		strncpy(message.packet, raw_packet, strlen(raw_packet));
		send_message_queue_mtx.lock();
		send_message_queue.push(message);
		send_message_queue_mtx.unlock();

	}

	// --- create threads for sending and recieving data, heartbeat thread --- //
	thread send_thread (send_function);
	thread recieve_thread (recieve_function);
	thread parse_thread (parse_function);
	thread heartbeat_thread (heartbeat_function);
	thread display_thread (display_function);
	thread user_interface (user_function);

	// --- join threads to main thread --- //
	send_thread.join();
	recieve_thread.join();
	heartbeat_thread.join();
	display_thread.join();
	parse_thread.join();

	return 0;
}