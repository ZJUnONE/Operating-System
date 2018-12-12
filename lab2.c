#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX 1000
#define NORTH 0
#define WEST 1
#define SOUTH  2
#define EAST  3

//queue of cars
typedef struct queue
{
	int front; //front points to the first car
	int rear; //rear points to the last car's next position
	int *car; //dynamic array
} Queue;

//car structure 
typedef struct car
{
	int id;
	int direction;
} car_t;

//mutex
pthread_mutex_t mutex_leave[4]; //used in leave stage
pthread_mutex_t mutex_queue[4]; //used on queue write operation
pthread_mutex_t mutex_direction[4]; //used on specified direction
pthread_mutex_t mutex_right_first[4]; //used on right first situation
pthread_mutex_t mutex_cross; //used in cross stage
pthread_mutex_t mutex_print; //used for printing
//condition
pthread_cond_t cond_direction_queue[4]; //used in cross stage
pthread_cond_t cond_right_first[4]; //used on right first situation

//queue
//4 global queues to 4 directions' cars
//use queues to assure cars can cross crossroad sequentially
Queue queue_direction[4]; //4 directions' queues

const char * const direction[4] = {"NORTH", "WEST", "SOUTH", "EAST"}; //direction string

void queue_init(Queue *Q) // queue's initialization
{
	Q->front = 0;
	Q->rear = 0;
	Q->car = (int *) malloc(MAX * sizeof(int));
}
void enqueue(Queue *Q, int id) //enqueue operation: car => queue, then rear++
{
	if(Q->rear == MAX)
	{
		printf("memory overflow!\n");
		exit(-1);
	}
	else
	{
		Q->car[Q->rear] = id;
		Q->rear++;
	}
}
int dequeue(Queue *Q) //dequeue operation, Q[front] => return value, then front++
{
	int rv;
	if(Q->front == Q->rear)
	{
		printf("empty queue, error\n");
		exit(-1);
	}
	else
	{
		rv = Q->car[Q->front];
		Q->front++;
		return rv;
	}
}
int get_first_car(Queue *Q) //just get queue's front without modifying
{
	return Q->car[Q->front];
}
int queue_not_empty(Queue *Q) //judge if the queue is not empty, if so, return 1
{
	return Q->rear-Q->front;
}

void car_arrive(car_t *car)
{
	//pthread_mutex_lock(&mutex_direction[car->direction]);
	//direction_count[car->direction]++;
	//first, enqueue
	pthread_mutex_lock(&mutex_queue[car->direction]);
	pthread_mutex_lock(&mutex_print);
	printf("[*] car %d from %s has arrived\n", car->id, direction[car->direction]);
	pthread_mutex_unlock(&mutex_print);
	enqueue(queue_direction+car->direction, car->id);
	pthread_mutex_unlock(&mutex_queue[car->direction]);

	//if more than 1 car, wait 
	pthread_mutex_lock(&mutex_direction[car->direction]);
	while (get_first_car(queue_direction+car->direction) != car->id) //if there are cars in the front, that is Q[front] != current car
		pthread_cond_wait(&cond_direction_queue[car->direction], &mutex_direction[car->direction]); //wait for front car
	pthread_mutex_unlock(&mutex_direction[car->direction]);

	//check right, right first
	pthread_mutex_lock(&mutex_right_first[car->direction]);
	if(queue_not_empty(queue_direction+(car->direction + 1) % 4)) //if right direction queue has car, make right forward first
		pthread_cond_wait(&cond_right_first[car->direction], &mutex_right_first[car->direction]); //wait for right car
	pthread_mutex_unlock(&mutex_right_first[car->direction]);
}

void car_cross(car_t *car) //car cross
{
	pthread_mutex_lock(&mutex_cross);
	pthread_mutex_lock(&mutex_print);
	printf("[*] car %d from %s is crossing \n", car->id, direction[car->direction]);
	pthread_mutex_unlock(&mutex_print);
	pthread_mutex_unlock(&mutex_cross);
}


void car_leave(car_t *car)
{
	pthread_mutex_lock(&mutex_leave[car->direction]); //lock mutex_leave

	pthread_mutex_lock(&mutex_direction[car->direction]); //lock mutex_direction
	pthread_mutex_lock(&mutex_right_first[(car->direction + 3) % 4]); //lock mutex_right_first

	pthread_mutex_lock(&mutex_queue[car->direction]); //lock mutex_queue
	pthread_mutex_lock(&mutex_print);
	printf("[*] car %d from %s is leaving\n", car->id, direction[car->direction]);
	pthread_mutex_unlock(&mutex_print);
	dequeue(queue_direction+car->direction); //dequeue operation, update current directions' queue
	pthread_mutex_unlock(&mutex_queue[car->direction]);
	
	//the left car
	//pthread_mutex_lock(&mutex_right_first[(car->direction + 3) % 4]);
	pthread_cond_signal(&cond_right_first[(car->direction + 3) % 4]);
	pthread_mutex_unlock(&mutex_right_first[(car->direction + 3) % 4]);
	//signal this direction
	//pthread_mutex_lock(&mutex_direction[car->direction]);
	pthread_cond_broadcast(&cond_direction_queue[car->direction]);
	pthread_mutex_unlock(&mutex_direction[car->direction]);

	//unlock mutex_leave
	pthread_mutex_unlock(&mutex_leave[car->direction]);
}

void* car_thread(void *argv) //main car thread, divided into 3 components(functions)
{
	car_t *car = (car_t *)argv;
	car_arrive(car);
	car_cross(car);
	car_leave(car);
}

void* check(void *argv)
{
	int n, w, e, s;// preserve 4 directions' car numbers
	while(1)
	{
		pthread_mutex_lock(&mutex_print); //lock mutex_print
		printf("Dead checking!\n");
		//if all directions have cars
		if( (n = queue_not_empty(queue_direction)) && 
			(w = queue_not_empty(queue_direction+1)) &&
			(e = queue_not_empty(queue_direction+2)) &&
			(s = queue_not_empty(queue_direction+3))
		)
		{
			sleep(1);
			//check twice
			if( (n == queue_not_empty(queue_direction)) && 
				(w == queue_not_empty(queue_direction+1)) &&
				(e == queue_not_empty(queue_direction+2)) &&
				(s == queue_not_empty(queue_direction+3))
			)
			{	
				//deadlock happens
				printf("deadlock detected! Signalling North to go\n");
				pthread_cond_signal(&cond_right_first[0]);
			}
			else
				//No deadlock happens
				printf("No deadlock happened!\n");
		}
		else
			//No deadlock happens
			printf("No deadlock happened!\n");
		pthread_mutex_unlock(&mutex_print); //unlock mutex_print
		sleep(2);//sleep 2s
	}
}

int main(int argc, char *argv[])
{
	int i, count;
	pthread_t *threads_car; //threads array
	pthread_t thread_check; //deadlock checking thread 
	car_t *cars; //car structure

	if(argc != 2) //check parameters' number
	{
		printf("Input error!\n");
		exit(-1);
	}
	else
	{
		for(i = 0; i < strlen(argv[1]); i++) //check 2nd parameters' validity
			if( 
				argv[1][i] != 'n' &&
				argv[1][i] != 'w' &&
				argv[1][i] != 's' &&
				argv[1][i] != 'e'
				)
			{
				printf("Input error!\n");
				exit(-1);			
			}
	}

	count = strlen(argv[1]); //number of cars
	for(i = 0; i < 4; i++)
		queue_init(queue_direction+i); //queue initialization

	printf("[*] totally %d cars\n", count);
	cars = (car_t *)malloc(count * sizeof(car_t));

	//initialize car structures
	for(i = 0; i < count; i++)
	{
		cars[i].id = i;
		switch(argv[1][i])
		{
			case 'n':
				cars[i].direction = NORTH;
				break;
			case 'w':
				cars[i].direction = WEST;
				break;
			case 's':
				cars[i].direction = SOUTH;
				break;
			case 'e':
				cars[i].direction = EAST;
				break;
		}
	}

	//initialize mutex and conditional variables
	for(i = 0; i < 4; i++)
	{
		pthread_mutex_init(&mutex_direction[i], NULL);
		pthread_mutex_init(&mutex_queue[i], NULL);
		pthread_mutex_init(&mutex_right_first[i], NULL);
		pthread_mutex_init(&mutex_leave[i], NULL);	
		pthread_cond_init(&cond_right_first[i], NULL);
		pthread_cond_init(&cond_direction_queue[i], NULL);

	}
	pthread_mutex_init(&mutex_cross, NULL);
	pthread_mutex_init(&mutex_print, NULL);

	//create deadlock checking thread
	pthread_create(&thread_check, NULL, check, NULL);

	//allocate memory for car threads
	threads_car = (pthread_t *)malloc(count*sizeof(pthread_t));

	//create car threads
	for(i = 0; i < count; i++)
		pthread_create(&threads_car[i], NULL, car_thread, (void *)(cars+i));

	//wait for all car threads' end
	for(i = 0; i < count; i++)
		pthread_join(threads_car[i], NULL);

	//finally, print "Done"
	printf("Done!\n");

	return 0;
}