#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <semaphore.h>

struct message
{
    struct sockaddr_in sender_addr;
    char msg[500];
    struct message *next;
};

typedef struct message Message;
Message *head;

void do_send();
void do_receive();
void do_wrap_up();

sem_t lock;

int r1 = 0, r2 = 0;

int main()
{
    pthread_t thread1, thread2;
    sem_init(&lock, 1, 1);

    pthread_create(&thread1, NULL,(void *) do_send,(void *) &r1);
    printf("Created SENDING thread\n");

    pthread_create(&thread2, NULL,(void *) do_receive,(void *) &r2);
    printf("Created RECEIVE thread\n");

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
	    do_wrap_up();
}

void do_send()
{
    while(1){
        while(head==NULL);

        sem_wait(&lock);
            /*TAKE MESSAGE FROM QUEUE*/
        sem_post(&lock);

        /*HERE CHECK IF MESSAGE IS:
        * JOIN: Then add sender to IP List and send NEW USER ADDED MESSAGE TO EVERYONE
        * MESSAGE: Broadcast Message to all people on the list
        */
    }
}

void do_receive()
{
    while(1){
        //RECEIVE MESSAGE FROM SOCKET

        sem_wait(&lock);
        //ADD MESSAGE TO QUEUE
        sem_post(&lock);
    }
}

void do_wrap_up()
{
}
