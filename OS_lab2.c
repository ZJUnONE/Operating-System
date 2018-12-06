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

typedef struct queue
{
	int front;
	int rear;
	int *car;
} Queue;

typedef struct car
{
	int id;
	int direction;
} car_t;

//mutex
pthread_mutex_t mutex_leave[4];
pthread_mutex_t mutex_queue[4];
pthread_mutex_t mutex_direction[4];
pthread_mutex_t mutex_right_first[4];
pthread_mutex_t mutex_cross;
pthread_mutex_t mutex_print;
//condition
pthread_cond_t cond_direction_queue[4];
pthread_cond_t cond_right_first[4]; 
//queue
Queue queue_direction[4];

const char * const direction[4] = {"NORTH", "WEST", "SOUTH", "EAST"};
int direction_count[4] = {0};

void queue_init(Queue *Q)
{
	Q->front = 0;
	Q->rear = 0;
	Q->car = (int *) malloc(MAX * sizeof(int));
}
void enqueue(Queue *Q, int id)
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
int dequeue(Queue *Q)
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
int get_first_car(Queue *Q)
{
	return Q->car[Q->front];
}
int queue_not_empty(Queue *Q)
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
	printf("[*] car %d has arrived from %s direction\n", car->id, direction[car->direction]);
	pthread_mutex_unlock(&mutex_print);
	enqueue(queue_direction+car->direction, car->id);
	pthread_mutex_unlock(&mutex_queue[car->direction]);

	//more than 1 car
	pthread_mutex_lock(&mutex_direction[car->direction]);
	while (get_first_car(queue_direction+car->direction) != car->id)
		pthread_cond_wait(&cond_direction_queue[car->direction], &mutex_direction[car->direction]);
	pthread_mutex_unlock(&mutex_direction[car->direction]);


	//check right
	pthread_mutex_lock(&mutex_right_first[car->direction]);
	if(queue_not_empty(queue_direction+(car->direction + 1) % 4))
		pthread_cond_wait(&cond_right_first[car->direction], &mutex_right_first[car->direction]);
	pthread_mutex_unlock(&mutex_right_first[car->direction]);

	
	//why only one mutex for right first? cause it's shortr
}

void car_cross(car_t *car)
{
	pthread_mutex_lock(&mutex_cross);
	pthread_mutex_lock(&mutex_print);
	printf("[*] car %d is crossing towards %s \n", car->id, direction[car->direction]);
	pthread_mutex_unlock(&mutex_print);
	pthread_mutex_unlock(&mutex_cross);
}


void car_leave(car_t *car)
{
	pthread_mutex_lock(&mutex_leave[car->direction]);

	pthread_mutex_lock(&mutex_direction[car->direction]);
	pthread_mutex_lock(&mutex_right_first[(car->direction + 3) % 4]);

	pthread_mutex_lock(&mutex_queue[car->direction]);
	pthread_mutex_lock(&mutex_print);
	printf("[*] car %d is leaving from %s direction\n", car->id, direction[car->direction]);
	pthread_mutex_unlock(&mutex_print);
	dequeue(queue_direction+car->direction);
	pthread_mutex_unlock(&mutex_queue[car->direction]);

	
	//the left car
	//pthread_mutex_lock(&mutex_right_first[(car->direction + 3) % 4]);
	pthread_cond_signal(&cond_right_first[(car->direction + 3) % 4]);
	pthread_mutex_unlock(&mutex_right_first[(car->direction + 3) % 4]);
	//signal this direction
	//pthread_mutex_lock(&mutex_direction[car->direction]);
	pthread_cond_broadcast(&cond_direction_queue[car->direction]);
	pthread_mutex_unlock(&mutex_direction[car->direction]);

	//printf("car %d done\n", car->id);
	pthread_mutex_unlock(&mutex_leave[car->direction]);
}

void* car_thread(void *argv)
{
	car_t *car = (car_t *)argv;
	car_arrive(car);
	car_cross(car);
	car_leave(car);
}

void* check(void *argv)
{
	int n, w, e, s;
	while(1)
	{
		pthread_mutex_lock(&mutex_print);
		printf("\nchecking\n");
		pthread_mutex_unlock(&mutex_print);
		if( (n = queue_not_empty(queue_direction)) && 
			(w = queue_not_empty(queue_direction+1)) &&
			(e = queue_not_empty(queue_direction+2)) &&
			(s = queue_not_empty(queue_direction+3))
		)
		{
			sleep(1);
			if( (n == queue_not_empty(queue_direction)) && 
				(w == queue_not_empty(queue_direction+1)) &&
				(e == queue_not_empty(queue_direction+2)) &&
				(s == queue_not_empty(queue_direction+3))
			)
			{	

				printf("deadlock detected!\n");
				//pthread_mutex_lock(&mutex_right_first[0]);
				pthread_cond_broadcast(&cond_right_first[0]);
				//pthread_mutex_unlock(&mutex_right_first[0]);
			}
		}
		sleep(1);
	}
}

int main(int argc, char *argv[])
{
	int i, count;
	pthread_t *threads_car;
	pthread_t thread_check;
	car_t *cars;

	if(argc != 2)
	{
		printf("Input error!\n");
		exit(-1);
	}
	else
	{
		for(i = 0; i < strlen(argv[1]); i++)
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

	count = strlen(argv[1]);
	for(i = 0; i < 4; i++)
		queue_init(queue_direction+i);

	printf("[*] totally %d cars\n", count);
	cars = (car_t *)malloc(count * sizeof(car_t));

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

	for(i = 0; i < 4; i++)
	{
		pthread_mutex_init(&mutex_direction[i], NULL);
		pthread_mutex_init(&mutex_queue[i], NULL);
		pthread_mutex_init(&mutex_right_first[i], NULL);
		pthread_mutex_init(&mutex_leave[i], NULL);	
		pthread_cond_init(&cond_right_first[i], NULL);
		pthread_cond_init(&cond_direction_queue, NULL);

	}

	pthread_mutex_init(&mutex_cross, NULL);
	pthread_mutex_init(&mutex_print, NULL);

	pthread_create(&thread_check, NULL, check, NULL);

	threads_car = (pthread_t *)malloc(count*sizeof(pthread_t));

	for(i = 0; i < count; i++)
		pthread_create(&threads_car[i], NULL, car_thread, (void *)(cars+i));

	for(i = 0; i < count; i++)
		pthread_join(threads_car[i], NULL);

	printf("Done!\n");
}