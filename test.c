#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "rl_lock_library.h"

int main() {
    rl_descriptor fd1, fd2;
    struct flock fl;

    // Ouvre le fichier
    fd1 = rl_open("fichier.txt", O_RDWR|O_CREAT, 0664);
    
    if (fd1.d == -1) {
        perror("Erreur lors de l'ouverture du fichier");
        exit(EXIT_FAILURE);
    }

    // Pose le verrou en écriture sur le descripteur fd1
    fl.l_type = F_WRLCK;  // Verrou en écriture
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // Verrouille tout le fichier
    

    if (rl_fcntl(fd1, F_SETLK, &fl) == -1) {
        perror("Erreur lors de la pose du verrou");
        exit(EXIT_FAILURE);
    }

    printf("Verrou posé avec succès sur fd1.\n");

    // Ouvre un autre descripteur vers le même fichier
    fd2 = rl_open("fichier.txt", O_RDWR);
    if (fd2.d == -1) {
        perror("Erreur lors de l'ouverture du fichier");
        exit(EXIT_FAILURE);
    }

    // Ferme fd2 pour lever le verrou
    if (rl_close(fd2) == -1) {
        perror("Erreur lors de la fermeture du descripteur fd2");
        exit(EXIT_FAILURE);
    }

    printf("Verrou levé avec succès en fermant fd2.\n");

    if (rl_fcntl(fd1, F_GETLK, &fl) == -1) {
        perror("Erreur lors de la récupération des informations de verrou");
        exit(EXIT_FAILURE);
    }

    // Vérifie si le descripteur a fd1 un verrou actif
    if (fl.l_type == F_UNLCK) {
        printf("Le descripteur fd1 n'a pas de verrou actif.\n");
    } else {
        printf("Le descripteur a un verrou actif .\n");
        printf("Type de verrou : %d\n", fl.l_type);
        printf("PID du processus propriétaire du verrou : %d\n", fl.l_pid);
    }

    // Fermeture du fd1
    if (rl_close(fd1) == -1) {
        perror("Erreur lors de la fermeture du descripteur fd1");
        exit(EXIT_FAILURE);
    }



    return EXIT_SUCCESS;
}