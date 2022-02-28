#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define READER 0
#define WRITER !READER
#define LAMDA 0.9
#define READER_COUNT semctl(semid, 3 * entries_num, GETVAL, 0)
#define INCREASE_READER_COUNT Sem_V(semid, 3 * entries_num)
#define ACTIVATION_COUNT semctl(semid, 3 * entries_num + 1, GETVAL, 0)
#define INCREASE_ACTIVATION_COUNT Sem_V(semid, 3 * entries_num + 1)

typedef struct Entry_Type {
	int data;  /* Data part of this entry */
	int read_count;  /* Reads done from this entry */
	int write_count;  /* Writes done to this entry */
} Entry;

int Read(Entry *entry) {
	entry->read_count += 1;  /* Increase reads done by 1 */
	return entry->data;  /* Return the data part */
}

void Write(Entry *entry, int new_data) {
	entry->data = new_data;  /* Update data part using the new value */
	entry->write_count += 1;  /* Increase writes done by 1 */
}

typedef struct Peer_Information {
	float average_waiting;  /* Average waiting time to access an entry */
	int read_count;  /* Reads done by this peer */
	int write_count;  /* Writes done by this peer */
} Peer_Info;

int Sem_Create(int num_semaphores) {  /* Procedure to create a given number of semaphores */
	int semid;
	if ((semid = semget (IPC_PRIVATE, num_semaphores, IPC_CREAT | 0600)) < 0) {  /* Create new set of semaphores, 0600 = read/alter by user */
		perror ("Error in creating semaphore\n");
		exit (1);
	}
	return semid;
}

void Sem_Init(int semid, int index, int value) {  /* Procedure to initialize a semaphore to a given value */
	if (semctl (semid, index, SETVAL, value) < 0) {  /* Set the value of the semaphore to the given one */
		perror ("Error in initializing first semaphore\n");
		exit (1);
	}
}

void Sem_P(int semid, int index) {  /* Procedure to perform a P or wait operation on a semaphore */
	struct sembuf sops[1];  /* Only one semaphore operation to be executed */
	sops[0].sem_num = index;  /* Define operation on semaphore with the given index */
	sops[0].sem_op  = -1;  /* Subtract 1 to value for P operation */
	sops[0].sem_flg = 0;  /* Set sem_flg to zero */
	if (semop (semid, sops, 1) == -1) {  /* If an error occured */
		perror ("Error in semaphore operation\n");
		exit (1);
	}
}

void Sem_V(int semid, int index) { /* procedure to perform a V or signal operation on semaphore of given index */
	struct sembuf sops[1];  /* define operation on semaphore with given index */
	sops[0].sem_num = index;/* define operation on semaphore with given index */
	sops[0].sem_op  = 1;    /* add 1 to value for V operation */
	sops[0].sem_flg = 0;    /* type "man semop" in shell window for details */
	if (semop (semid, sops, 1) == -1) {  /* If an error occured */
		perror ("Error in semaphore operation\n");
		exit (1);
	}
}

int main(int argc, char *argv[]) {
	/* Check if input from command line is invalid */
	if (argc != 5) {  /* If the given arguments are not as many as they should be */
		printf("To execute: ./ergasia1 <peers_num> <iterations> <readers_percent> <entries_num>\n");
		return -1;
	}
	int peers_num = atoi(argv[1]);  /* Number of peers that take part in each iteration */
	int iterations = atoi(argv[2]);  /* Number of iterations in which peers are getting activated */
	int readers_percent = atoi(argv[3]);  /* Readers percentage is a number between 0 and 100 */
	int entries_num = atoi(argv[4]);  /* Number of entries that are accessible */
	int total_activations = peers_num * iterations;  /* Numbers of activations of every peer in all iterations */
	int total_readers = round((readers_percent / 100.0) * total_activations);  /* The required number of activations as reader */
	
	printf("\nRequirements:\n");
	printf("Peers in each iteration: %d\n", peers_num);
	printf("Number of iterations: %d\n", iterations);
	printf("Readers are %d%% of %d total activations: %d readers\n", readers_percent, total_activations, total_readers);
	printf("Number of entries: %d\n", entries_num);
	printf("\nSimulation:\n");
	
	int shared_memory_size = entries_num * sizeof(Entry) + peers_num * sizeof(Peer_Info);  /* Fits both the entries and the peers' info */
	/* Allocate a shared memory segment with the necessary size */
	int segment_id = shmget (IPC_PRIVATE, shared_memory_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	/* Attach the shared memory segment */
	Entry *shared_memory = (Entry *) shmat(segment_id, 0, 0);
	/* Initialize each entry of shared memory */
	for (int entry = 0; entry < entries_num; entry++) {  /* For each entry */
		shared_memory[entry].data = 0;  /* Initialize the data part as zero */
		shared_memory[entry].read_count = 0;  /* Initially zero reads have been done from this entry */
		shared_memory[entry].write_count = 0;  /* Initially zero writes have been done to this entry */
	}
	/* Initialize peers' information in shared memory */
	Peer_Info *peer_info = (Peer_Info *)(shared_memory + entries_num);
	for (int peer = 0; peer < peers_num; peer++) {  /* For each peer */
		peer_info[peer].average_waiting = 0.0;  /* Initially the average waiting time is zero */
		peer_info[peer].read_count = 0;  /* Initially zero reads have been done by this peer */
		peer_info[peer].write_count = 0;  /* Initially zero writes have been done by this peer */
	}
	
	/* Create semaphores */
	int semid = Sem_Create(3 * entries_num + 2);  /* Save the returned identifier for the semaphore set */
	/* Initialize each semaphore */
	for (int entry = 0; entry < entries_num; entry++) {  /* For each entry */
		Sem_Init(semid, 3 * entry, 1);  /* The first semaphore is for readers' critical section */
    	Sem_Init(semid, 3 * entry + 1, 1);  /* The second semaphore is mutex */
    	Sem_Init(semid, 3 * entry + 2, 0);  /* The third is a counting semaphore that counts the number of active readers in this entry */
	}
	Sem_Init(semid, 3 * entries_num, 0);  /* This additional semaphore is counting the number of readers so far */
	Sem_Init(semid, 3 * entries_num + 1, 0);  /* And this semaphore is counting the number of activations so far (readers and writers) */
	
	pid_t pid;  /* Variable that stores a process id */
	pid_t proc[peers_num];  /* Record of spawned processes */
	/* Iterations */
	for (int i = 0; i < iterations; i++) {  /* Until the required number of iterations is reached */
		printf("Iteration %d:\n", i);
		/* Activate every peer in each iteration */
		for (int peer = 0; peer < peers_num; peer++) {  /* For each peer */
			if ((pid = fork()) == -1) {  /* Spawn child process and check if there was an error */
        		perror ("Error in fork\n");
          		exit (1);
        	}
	    	if (pid == 0) {  /* This is the child process */
				time_t t;  /* Variable to initialize seed */
				srand((int)time(&t) % getpid());  /* Initialize seed using PIDs so rand() does not create the same "random" number for every child process */
				int entry = rand() % entries_num;  /* Pick a random entry */
				INCREASE_ACTIVATION_COUNT;  /* An activation is happening so increase that semaphore */
				/* Randomly choose whether it is going to be a reader or a writer, but the readers cannot exceed the required number */
				if ((rand() % 2 == READER && READER_COUNT < total_readers) \
				|| (total_readers - READER_COUNT == total_activations - ACTIVATION_COUNT + 1)) {  /* If the activations left are just enough
				to fit the rest of the readers (meaning that number of writers is already the required) then this peer must be reader */
		        	Sem_P(semid, 3 * entry);  /* Wait semaphore before executing a critical section */
		        	INCREASE_READER_COUNT;  /* This peer is reader so increase their count */
		        	Sem_V(semid, 3 * entry + 2);  /* Increase semaphore that counts active readers */
		        	if (semctl(semid, 3 * entry + 2, GETVAL, 0) == 1)  /* If this is the first active reader */
		        		Sem_P(semid, 3 * entry + 1);  /* Wait the mutex */
		        	Sem_V(semid, 3 * entry);  /* A critical section is over, so signal that semaphore */
		        	float T = - log ((float)rand() / RAND_MAX) / LAMDA;  /* Logarithmic formula for waiting time */
		        	float waiting = 1.5 * T;  /* Multiply it by a constant so the waiting is reasonable */
		        	sleep(waiting);  /* Simulate the waiting time between the request and the access of the entry */
					Sem_P(semid, 3 * entry);  /* Wait semaphore before executing another critical section */
					int data = Read(&shared_memory[entry]);  /* Read the data from the entry */
		        	printf("Peer %d is READING from entry %d the following data: %d\n", peer, entry, data);
		        	peer_info[peer].read_count += 1;  /* Increase by 1 the reads done by this peer */
					float average_waiting = peer_info[peer].average_waiting;  /* Average waiting time of this peer so far */
		        	int reads_done_by_this_peer = peer_info[peer].read_count;  /* Reads done by this peer so far (this included) */
		        	int writes_done_by_this_peer = peer_info[peer].write_count; /* Writes done by this peer so far */
		        	peer_info[peer].average_waiting = (average_waiting + waiting) / (reads_done_by_this_peer + writes_done_by_this_peer);  /* Update the average waiting time */
		        	Sem_P(semid, 3 * entry + 2);  /* This reader is about to finish, so decrease by 1 the number of active readers */
		        	if (semctl(semid, 3 * entry + 2, GETVAL, 0) == 0)  /* If there are not any active readers left */
		        		Sem_V(semid, 3 * entry + 1);  /* Signal mutex */
		        	Sem_V(semid, 3 * entry);  /* Another critical section is over, so signal that semaphore */
					exit(0);  /* Child process that implements reader finished */
				}
				else {
					Sem_P(semid, 3 * entry + 1);  /* Wait the mutex */
					int new_data = rand() % 1000;  /* Generate a random integer that will be written */
					float T = - log ((float)rand() / RAND_MAX) / LAMDA;  /* Logarithmic formula for waiting time */
		        	float waiting = 1.5 * T;  /* Multiply it by a constant so the waiting is reasonable */
		        	sleep(waiting);  /* Simulate the waiting time between the request and the access of the entry */
					Write(&shared_memory[entry], new_data);  /* Write new data to the entry*/
					printf("Peer %d is WRITING to entry %d the following data: %d\n", peer, entry, new_data);
					peer_info[peer].write_count += 1;  /* Increase by 1 the writes done by this peer */
					float average_waiting = peer_info[peer].average_waiting;  /* Average waiting time of this peer so far */
		        	int reads_done_by_this_peer = peer_info[peer].read_count;  /* Reads done by this peer so far (this included) */
		        	int writes_done_by_this_peer = peer_info[peer].write_count; /* Writes done by this peer so far */
		        	peer_info[peer].average_waiting = (average_waiting + waiting) / (reads_done_by_this_peer + writes_done_by_this_peer);  /* Update the average waiting time */
					Sem_V(semid, 3 * entry + 1);  /* Signal mutex */
					exit(0);  /* Child process that implements writer finished */
				}
	    	}
	    	else {  /* This is the parent process */
				proc[peer - 1] = pid;  /* Save process ids */
				for (int p = 0; p < peer; p++)  /* For each created process so far */
     				waitpid(proc[p], NULL, 0);  /* Parent is waiting for this process to finish */
			}
		}
	}
	
	printf("\nResults based on peers:\n");
	int total_reads_done_by_all_peers = 0;  /* Reads done by all peers */
	int total_writes_done_by_all_peers = 0;  /* Writes done by all peers */
	float sum_average_waiting = 0.0;  /* Sum the average waiting time of all peers */
	for (int peer = 0; peer < peers_num; peer++) {  /* For each peer */
		int reads_by_this_peer = peer_info[peer].read_count;  /* Reads done by this peer */
		int writes_by_this_peer = peer_info[peer].write_count;  /* Writes done by this peer */
		float average_waiting_of_this_peer = peer_info[peer].average_waiting;  /* Average waiting time of this peer */
		printf("Peer %d: %d reads, %d writes and average waiting time is %f\n", peer, reads_by_this_peer, writes_by_this_peer, average_waiting_of_this_peer);
		total_reads_done_by_all_peers += reads_by_this_peer;  /* Accumulate the reads done by all peers */
		total_writes_done_by_all_peers += writes_by_this_peer;  /* Accumulate the writes done by all peers */
		sum_average_waiting += average_waiting_of_this_peer;  /* Accumulate the sum of average waiting time of all peers */
	}
	float average_waiting_of_all_peers = sum_average_waiting / peers_num;  /* Calculate the overall average waiting time */
	printf("Total reads done: %d\n", total_reads_done_by_all_peers);
	printf("Total writes done: %d\n", total_writes_done_by_all_peers);
	printf("Average waiting time: %f\n", average_waiting_of_all_peers);
	
	printf("\nResults based on entries:\n");
	int total_reads_done_from_all_entries = 0;  /* Reads done from all entries */
	int total_writes_done_to_all_entries = 0;  /* Writes done to all entries */
	for (int entry = 0; entry < entries_num; entry++) {  /* For each entry */
		int data_in_this_entry = shared_memory[entry].data;  /* Data that this entry end up having */
		int reads_from_this_entry = shared_memory[entry].read_count;  /* Reads done to this entry */
		int writes_to_this_entry = shared_memory[entry].write_count;  /* Writes done from this entry */
		printf("Entry %d: %d reads, %d writes and current data is %d\n", entry, reads_from_this_entry, writes_to_this_entry, data_in_this_entry);
		total_reads_done_from_all_entries += reads_from_this_entry;  /* Accumulate the reads done from all entries */
		total_writes_done_to_all_entries += writes_to_this_entry;  /* Accumulate the writes done to all entries */
	}
	printf("Total reads done: %d\n", total_reads_done_from_all_entries);
	printf("Total writes done: %d\n", total_writes_done_to_all_entries);
	
	/* Remove the set of semaphores */
	if (semctl (semid, 0, IPC_RMID) < 0) {  /* Check if there was an error */
		perror ("Error in removing semaphore from the system\n");
		exit (1);
	}
	//printf ("\nSemaphore cleanup completed\n");
	
	/* Detach the shared memory segment */ 
	shmdt (shared_memory);
	/* Deallocate the shared memory segment */ 
	shmctl (segment_id, IPC_RMID, 0);
	//printf("Allocated shared memory released\n");
	
	exit (0);
}
