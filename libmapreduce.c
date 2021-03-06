/** @file libmapreduce.c */
/* 
 * CS 241
 * The University of Illinois
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>

#include "libmapreduce.h"
#include "libds/libds.h"


static const int BUFFER_SIZE = 2048;  /**< Size of the buffer used by read_from_fd(). */
/**
 * Adds the key-value pair to the mapreduce data structure.  This may
 * require a reduce() operation.
 *
 * @param key
 *    The key of the key-value pair.  The key has been malloc()'d by
 *    read_from_fd() and must be free()'d by you at some point.
 * @param value
 *    The value of the key-value pair.  The value has been malloc()'d
 *    by read_from_fd() and must be free()'d by you at some point.
 * @param mr
 *    The pass-through mapreduce data structure (from read_from_fd()).
 */
static void process_key_value(const char *key, const char *value, mapreduce_t *mr)
{
	unsigned long rev;
	if (datastore_put(&(mr->data), key, value) == 0) {
		//Data already exists!
		const char * oldvalue = datastore_get(&(mr->data), key, &rev);
		const char * updated = mr->my_reduce(value, oldvalue);
		datastore_update(&(mr->data), key, updated, rev);
		free(oldvalue);
		free(updated);
	}
	free(key);
	free(value);	
}


/**
 * Helper function.  Reads up to BUFFER_SIZE from a file descriptor into a
 * buffer and calls process_key_value() when for each and every key-value
 * pair that is read from the file descriptor.
 *
 * Each key-value must be in a "Key: Value" format, identical to MP1, and
 * each pair must be terminated by a newline ('\n').
 *
 * Each unique file descriptor must have a unique buffer and the buffer
 * must be of size (BUFFER_SIZE + 1).  Therefore, if you have two
 * unique file descriptors, you must have two buffers that each have
 * been malloc()'d to size (BUFFER_SIZE + 1).
 *
 * Note that read_from_fd() makes a read() call and will block if the
 * fd does not have data ready to be read.  This function is complete
 * and does not need to be modified as part of this MP.
 *
 * @param fd
 *    File descriptor to read from.
 * @param buffer
 *    A unique buffer associated with the fd.  This buffer may have
 *    a partial key-value pair between calls to read_from_fd() and
 *    must not be modified outside the context of read_from_fd().
 * @param mr
 *    Pass-through mapreduce_t structure (to process_key_value()).
 *
 * @retval 1
 *    Data was available and was read successfully.
 * @retval 0
 *    The file descriptor fd has been closed, no more data to read.
 * @retval -1
 *    The call to read() produced an error.
 */
static int read_from_fd(int fd, char *buffer, mapreduce_t *mr)
{
	/* Find the end of the string. */
	int offset = strlen(buffer);

	/* Read bytes from the underlying stream. */
	int bytes_read = read(fd, buffer + offset, BUFFER_SIZE - offset);
	if (bytes_read == 0)
		return 0;
	else if(bytes_read < 0)
	{
		fprintf(stderr, "error in read.\n");
		return -1;
	}

	buffer[offset + bytes_read] = '\0';

	/* Loop through each "key: value\n" line from the fd. */
	char *line;
	while ((line = strstr(buffer, "\n")) != NULL)
	{
		*line = '\0';

		/* Find the key/value split. */
		char *split = strstr(buffer, ": ");
		if (split == NULL)
			continue;

		/* Allocate and assign memory */
		char *key = malloc((split - buffer + 1) * sizeof(char));
		char *value = malloc((strlen(split) - 2 + 1) * sizeof(char));

		strncpy(key, buffer, split - buffer);
		key[split - buffer] = '\0';

		strcpy(value, split + 2);

		/* Process the key/value. */
		process_key_value(key, value, mr);

		/* Shift the contents of the buffer to remove the space used by the processed line. */
		memmove(buffer, line + 1, BUFFER_SIZE - ((line + 1) - buffer));
		buffer[BUFFER_SIZE - ((line + 1) - buffer)] = '\0';
	}

	return 1;
}


/**
 * Initialize the mapreduce data structure, given a map and a reduce
 * function pointer.
 */
void mapreduce_init(mapreduce_t *mr, 
                    void (*mymap)(int, const char *), 
                    const char *(*myreduce)(const char *, const char *))
{
	
	mr->my_map = mymap;
	mr->my_reduce = myreduce;
	datastore_init(&(mr->data));
	
}


void * work(void *a)
{
	mapreduce_t *m = (mapreduce_t *)a;
	int counter = 0;
	while (counter < m->count ) {
		counter++;
		struct epoll_event curr_event;
		epoll_wait(m->ep_fd, &curr_event, 1, -1); 
		int index;		
		int i;
		for (i = 0; i < m->count; i++) {
			if (curr_event.data.fd == m->event[i].data.fd) {			
				index = i;
			}
		}
		read_from_fd(curr_event.data.fd, m->buffers[index], m);
		epoll_ctl(m->ep_fd, EPOLL_CTL_DEL, curr_event.data.fd, NULL);		
	}	
	return NULL;
}

/**
 * Starts the map() processes for each value in the values array.
 * (See the MP description for full details.)
 */
void mapreduce_map_all(mapreduce_t *mr, const char **values)
{
	int i = 0;
	int count = 0;
	pid_t pid;
	while (values[i] != NULL) {	
		count++;
		i++;
	}
	mr->count = i;
	(mr->buffers) = malloc(sizeof(char *)*i);
	mr->ep_fd = epoll_create(i);
	(mr->event) = malloc(sizeof(struct epoll_event) *i);
	(mr->fds) = malloc(sizeof(int *)*i);
	int j =0;
	for (j = 0; j < i; j++) {
		mr->buffers[j] = malloc(BUFFER_SIZE+1);
		mr->buffers[j][0] = '\0';
		mr->fds[j] = malloc(sizeof(int)*2);
		pipe(mr->fds[j]);
	}
	for (j = 0; j < i; j++) {
		pid = fork();
		int read_fd = mr->fds[j][0];
		int write_fd = mr->fds[j][1];
		if (pid == 0) {
			int k;
			for (k = 0; k < count; k++) {
				close(mr->fds[k][0]); 
			}
			mr->my_map(write_fd, values[j]);
			exit(0);
		} else if (pid > 0) {

			close(write_fd);	
		}
		mr->event[j].events = EPOLLIN;
		mr->event[j].data.fd = read_fd;
		epoll_ctl(mr->ep_fd, EPOLL_CTL_ADD, read_fd, &(mr->event[j]));
	}
	pthread_create(&(mr->pt), NULL, work, (void *)mr);
}



/**
 * Blocks until all the reduce() operations have been completed.
 * (See the MP description for full details.)
 */
void mapreduce_reduce_all(mapreduce_t *mr)
{
	pthread_join(mr->pt, NULL);
}


/**
 * Gets the current value for a key.
 * (See the MP description for full details.)
 */
const char *mapreduce_get_value(mapreduce_t *mr, const char *result_key)
{	
	return datastore_get( &(mr->data), result_key, 0); 
}


/**
 * Destroys the mapreduce data structure.
 */
void mapreduce_destroy(mapreduce_t *mr)
{
	datastore_destroy(&(mr->data));
	free(mr->event);
	int i;
	for (i = 0; i < mr->count; i++) {
		free(mr->buffers[i]);
		free(mr->fds[i]);
	}
	free(mr->buffers);
	free(mr->fds);
}
