#ifndef RL_LOCK_LIBRARY_H
#define RL_LOCK_LIBRARY_H

#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <stdbool.h>

#define SIZE 526
#define NB_FILES 150
#define NB_LOCKS 100
#define NB_OWNERS 100


typedef struct{
	pid_t proc; 			/* pid du processus */
	int des; 				/* descripteur de fichier */
} owner;


typedef struct{
	int next_lock;
	off_t starting_offset;
	off_t len;
	short type;						/* F_RDLCK F_WRLCK */
	size_t nb_owners;
	owner lock_owners[NB_OWNERS];	
} rl_lock;


typedef struct{
	pthread_mutex_t mutex;			/* Mutex*/
    pthread_cond_t cond;			/* Condition */
	int first;
	rl_lock lock_table[NB_LOCKS];	/* Liste des verrous sur le fichier*/
} rl_open_file;

typedef struct{
	int d;
	rl_open_file *f;
} rl_descriptor;


typedef struct{
	int nb_files;
	rl_open_file *tab_open_files[NB_FILES]; /* Liste des fichiers ouverts par un processus */
} rl_all_files;


/*Les fonctions*/

rl_descriptor rl_open(const char *path, int oflag, ...);

int rl_close( rl_descriptor lfd);

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck);

rl_descriptor rl_dup( rl_descriptor lfd );

rl_descriptor rl_dup2( rl_descriptor lfd, int newd );

pid_t rl_fork();


#endif // RL_LOCK_LIBRARY_H
