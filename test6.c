/* 
 * CS 241
 * The University of Illinois
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include "libmapreduce.h"

#define CHARS_PER_INPUT 30000
#define INPUTS_NEEDED 10


/* Takes input string and output the following key-value pairs if find any:
 * wonder: matched_line
 * Wonder: matched_line
 * WONDER: matched_line
 */
void map(int fd, const char *data) {

}

/* Takes two lines and combine them into one string as the following:
 * value1\nvalue2
*/
const char *reduce(const char *value1, const char *value2) {
	return NULL;   
}


int main()
{
    FILE *file = fopen("alice.txt", "r");
    char s[1024];
    int i;

    char **values = malloc(INPUTS_NEEDED * sizeof(char *));
    int values_cur = 0;

    values[0] = malloc(CHARS_PER_INPUT + 1);
    values[0][0] = '\0';

    while (fgets(s, 1024, file) != NULL)
    {
        if (strlen(values[values_cur]) + strlen(s) < CHARS_PER_INPUT)
            strcat(values[values_cur], s);
        else
        {
            values_cur++;
            values[values_cur] = malloc(CHARS_PER_INPUT + 1);
            values[values_cur][0] = '\0';
            strcat(values[values_cur], s);
        }
    }

    values_cur++;
    values[values_cur] = NULL;

    fclose(file);

    mapreduce_t mr;
    mapreduce_init(&mr, map, reduce);

    mapreduce_map_all(&mr, (const char **)values);
    mapreduce_reduce_all(&mr);

   
    char * keywords[3];
    
    keywords[0] = "wonder";
    keywords[1] = "Wonder";
    keywords[2] = "WONDER";


    for(i = 0; i < 3; i ++)
    {
        const char * result = mapreduce_get_value(&mr, keywords[i]);
        printf("=============%s==============\n", keywords[i]);
        if (result == NULL)
	    printf("NOT FOUND\n");
	else
	{
	    printf("%s\n", result);
            free((void*)result);
	}
    }


    for (i = 0; i < values_cur; i++)
        free(values[i]);
    free(values);

    mapreduce_destroy(&mr);

    return 0;
}
