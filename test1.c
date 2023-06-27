#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "rl_lock_library.h"

int main(int argc, char **argv) {
    rl_descriptor fd1, fd2;
    struct flock fl;

    // Ouvre le fichier
    fd1 = rl_open(argv[1], O_RDWR|O_CREAT, 0664);
    write(fd1.d, "Bonjour Comment vas-tu?", 23);
    
    if (fd1.d == -1) {
        perror("Erreur lors de l'ouverture du fichier\n");
        exit(EXIT_FAILURE);
    }

    printf("Le premier descripteur fd1 est ouvert\n");

    // Pose le verrou en écriture sur le descripteur fd1
    fl.l_type = F_WRLCK;  // Verrou en écriture
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 3;  // Verrouille tout le fichier
    

    if (rl_fcntl(fd1, F_SETLK, &fl) == -1) {
        perror("Erreur lors de la pose du verrou");
        exit(EXIT_FAILURE);
    }

    printf("Verrou posé avec succès sur fd1.\n");

    // Ouvre un autre descripteur vers le même fichier
    fd2 = rl_open("fichier.txt", O_RDWR);
    if (fd2.d == -1) {
        perror("Erreur lors de l'ouverture du fichier\n");
        exit(EXIT_FAILURE);
    }

    printf("Le second descripteur fd2 est ouvert\n");

    // Ferme fd2 pour lever le verrou
    if (rl_close(fd2) == -1) {
        perror("Erreur lors de la fermeture du descripteur fd2\n");
        exit(EXIT_FAILURE);
    }

    printf("Fermeture du second descripteur fd2.\n");

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
        perror("Erreur lors de la fermeture du descripteur fd1\n");
        exit(EXIT_FAILURE);
    }

    /*--------------------------------------------*/
    if (fl.l_type == F_UNLCK) {
        printf("Le descripteur fd1 n'a pas de verrou actif.\n");
    } else {
        printf("Le descripteur a un verrou actif .\n");
        printf("Type de verrou : %d\n", fl.l_type);
        printf("PID du processus propriétaire du verrou : %d\n", fl.l_pid);
    }



    return EXIT_SUCCESS;
}
