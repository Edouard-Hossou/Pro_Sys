/* SAMAKE     Moussa                     71708254            @msamak54

   HOSSOU     Edouard Patrick            22119390            @hossou
*/

/* ------------------------------------------------------------------------------------------------------------*/

#include "rl_lock_library.h"

/* ------------------------------------------------------------------------------------------------------------*/


static rl_all_files fichiers;


/* ------------------------------------------------------------------------------------------------------------*/

/* afficher le message d'erreur d'une fonction de la bibliothèque pthread
 * code - le code d'erreur 
 * txt - texte supplémentaire à afficher ou NULL
 * file : le nom du fichier source
 * line : le numéro de ligne du fichier source 
 */
static void thread_error(const char *file, int line, int code, char *txt){
  if( txt != NULL )
    fprintf( stderr,  "[%s] in file %s in line %d :  %s\n",
	     txt, file , line, strerror( code ) );
  else
    fprintf( stderr,  "in file %s in line %d :  %s\n",
	     file , line, strerror( code ) );
  exit(EXIT_FAILURE);
}



/* ------------------------------------------------------------------------------------------------------------*/
// Initialisation des mutex
static int initialiser_mutex(pthread_mutex_t *pmutex){
    pthread_mutexattr_t mutexattr;
    int code; // code retour de la fonction pthread_mutex_init
    if( ( code = pthread_mutexattr_init(&mutexattr) ) != 0)	    
        return code;

    if( ( code = pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED) ) != 0)	    
        return code;
    code = pthread_mutex_init(pmutex, &mutexattr);
    return code;
}


/* ------------------------------------------------------------------------------------------------------------*/
// Initialisation de cond
static int initialiser_cond(pthread_cond_t *pcond){
    pthread_condattr_t condattr;
    int code;
    if( ( code = pthread_condattr_init(&condattr) ) != 0 )
        return code;
    if( ( code = pthread_condattr_setpshared(&condattr,PTHREAD_PROCESS_SHARED) ) != 0 )
        return code;
    code = pthread_cond_init(pcond, &condattr);
    return code;
}


/* ------------------------------------------------------------------------------------------------------------*/
// Formation du nom du shared memory object
static char *nom_objet(const int f){//
    static char nom[SIZE];
    char nom_p[SIZE];
    char *pref = getenv("USERNAME"); // Nom d'utilisateur à la place de "f"
    struct stat buf;
    if(fstat(f, &buf) == -1){
        exit(1);
    }
    sprintf(nom_p, "%s_%d_%d", pref, (int)buf.st_dev, (int)buf.st_ino);
    nom[0] = '/';
    strncpy(nom+1, nom_p, sizeof(nom_p));
    nom[SIZE-1]='\0'; 	
    return nom;
}

/* ------------------------------------------------------------------------------------------------------------*/

int rl_init_library(){
    fichiers.nb_files = 0; // Le tableau est vide
    return EXIT_SUCCESS;
}


/* ------------------------------------------------------------------------------------------------------------*/

rl_descriptor rl_open(const char *path, int oflag, ...){

    mode_t mode;
    int fd = 0;
    rl_descriptor desc;

    va_list liste;
    va_start(liste, oflag);
    mode = va_arg(liste, mode_t);
    va_end(liste);
	fd = open(path, oflag, mode);
    if(fd == -1){ //Si le open échoue
        desc.d = -1;
        desc.f = NULL;
        return desc; // Le rl_descriptor en cas d'échec d'une fonction retournant un rl-descriptor
    }
    char *shm_name = nom_objet(fd);

    bool shm = true; // Pour savoir si le fichier est déja ouvert par le processus ou pas

    int shm_fd = shm_open(shm_name, oflag, mode);
    if(shm_fd >= 0){ // Création réussie
        if(ftruncate(shm_fd, sizeof(rl_open_file)) <0)
            return (rl_descriptor){-1, NULL};
    }
    else if(shm_fd < 0 && errno == EEXIST){ // L'objet mémoire existe
        shm_fd = shm_open(shm_name, O_RDWR, 0);
        if(shm_fd < 0){
            close(fd);
            return (rl_descriptor){-1, NULL};
        }
        shm = false;
    }
    else{ // Echec et L'objet mémoire existe
        return (rl_descriptor){-1, NULL};
    }

    // Projection de l'objet dans la mémoire partagée
    rl_open_file *open_f = (rl_open_file *)mmap( NULL, sizeof(rl_open_file), PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0 );
    if(open_f == MAP_FAILED){
        close(fd);
        return (rl_descriptor){-1, NULL};
    }

    //Initialisation des éléments du nouveau objet mémoire ;

    if(shm){ // Le fichier n'est pas encore ouvert par le processus courant, l'ajouter à la liste
        // Vérifier si le fichier est déjà ouvert par le processus courant

        int code;
        if(( code = initialiser_mutex(&(open_f->mutex))) != 0)
            thread_error(__FILE__, __LINE__, code, "init_mutex");

        if(( code = initialiser_cond(&(open_f->cond))) != 0 )
            thread_error(__FILE__, __LINE__, code, "init_cond");

		open_f->first = -2;
        rl_lock lk;    //Initialisation des éléments de tableau lock_table ;
        lk.next_lock = -2;
        lk.nb_owners = 0;
        for(int k=0; k < NB_LOCKS; k++)
            open_f->lock_table[k] = lk;
        fichiers.nb_files += 1;
        fichiers.tab_open_files[fichiers.nb_files - 1] = open_f; //Ajouter le rl_open_file au tableau des fichiers 
    }
	// Fichier déja ouvert
    desc.d = fd;
    desc.f = open_f;


    //printf("A la fin de rl_open\n");

    return desc;
}

/* ------------------------------------------------------------------------------------------------------------*/
int rl_close(rl_descriptor lfd) {
    // Supprimer les verrous associés au descripteur et au processus appelant
    owner lfd_owner = { .proc = getpid(), .des = lfd.d };
    
    // Fermer le descripteur lfd.d
    int n = close(lfd.d);    // Fermer le descripteur du fichier dans le processus   

    int code;
    if(( code = pthread_mutex_lock(&(lfd.f->mutex))) != 0)
            thread_error(__FILE__, __LINE__, code, "lock");

    // Itérer sur les verrous du descripteur
    printf("Je suis au debut de close\n");

    if(lfd.f->first != -2){ // Il y a au moins un verrou sur le fichier

        int i = lfd.f->first;
     
        while(i != -2){
            rl_lock lock = lfd.f->lock_table[i];

            // Vérifier si lfd_owner est présent dans le tableau des propriétaires du verrou
            for (size_t j = 0; j < lock.nb_owners; j++) {
                if (lock.lock_owners[j].proc == lfd_owner.proc && lock.lock_owners[j].des == lfd_owner.des) {
                    // Supprimer lfd_owner du tableau des propriétaires
                    if(j!= lock.nb_owners - 1){ //Suppression d'un owner différent du dernier
                        for (size_t k = j; k < lock.nb_owners - 1; k++){                    
                            lock.lock_owners[k] = lock.lock_owners[k+1];//Réarrangement des élement d'un pas vers la gauche supprimant le owner
                            lock.nb_owners--;
                            break;
                        }
                    }else{ //Suppression du dernier owner
                        lock.nb_owners--;
                        break;
                    }
                }
            }
            // Vérifier s'il s'agissait de l'unique propriétaire du verrou
            if (lock.nb_owners == 0) {
                // Supprimer le verrou lui-même
                lfd.f->lock_table[i].next_lock = -2;

                if (lfd.f->lock_table[i].next_lock == -1) {//dernier vérrou
                    for(int k=0; i<NB_LOCKS; k++){
                        lfd.f->first = -2;
                        break;
                    }                    
                            
                }
            }
            //Sortir de la boucle après avoir traversé les cases contenant des rl_lock
            if(lfd.f->lock_table[i].next_lock == -1){
                break;  
            }

            i = lfd.f->lock_table[i].next_lock;
          
        }
    }

    //Réduire le nombre de fichier ouvert par le processus
    fichiers.nb_files -=1;
    // Réduire le nombre de descripteur
    char *shm_name = nom_objet(lfd.d);
    printf("%s\n", shm_name);


   if(shm_unlink(shm_name) != 0){ // Supprimer l'objet mémoire s'il s'agit du dernier fichier
        printf("Je suis ici unlink\n");
        exit(1);
   }
    if(munmap(lfd.f, sizeof(rl_open_file))!= 0){ // Libérer la mémoire partagé créé a la ouverture du fichier par rl_fcntl
    	printf("Je suis ici au Unmap\n");
        exit(1);
    }

	if(( code = pthread_mutex_unlock(&(lfd.f->mutex))) != 0)
            thread_error(__FILE__, __LINE__, code, "unlock");


    printf("Fin de close\n");

    
    return n;
}


/* ------------------------------------------------------------------------------------------------------------*/

// Définition de la fonction rl_fcntl

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck) {
    if (cmd != F_SETLK && cmd != F_SETLKW && cmd != F_GETLK) {
        perror("Mauvaise Commande");
        exit(EXIT_FAILURE);
    }

    owner lfd_owner = {.proc = getpid(), .des = lfd.d};

    int code;
    if ((code = pthread_mutex_lock(&(lfd.f->mutex))) != 0)
        thread_error(__FILE__, __LINE__, code, "lock");

    if (cmd == F_SETLK) {
        switch (lck->l_type) {
            case F_UNLCK:
                // Supprimer le verrou pour lfd_owner s'il existe
                for (int i = 0; i < NB_LOCKS; i++) {
                    rl_lock lock = lfd.f->lock_table[i];
                    if (lock.next_lock == -2) // Fin de la liste des verrous
                        break;

                    // Vérifier si lfd_owner est propriétaire du verrou
                    for (size_t j = 0; j < lock.nb_owners; j++) {
                        if (lock.lock_owners[j].proc == lfd_owner.proc && lock.lock_owners[j].des == lfd_owner.des) {
                            // Supprimer lfd_owner du tableau des propriétaires
                            if (j != lock.nb_owners - 1) { // Suppression d'un owner différent du dernier
                                for (size_t k = j; k < lock.nb_owners - 1; k++) {
                                    lock.lock_owners[k] = lock.lock_owners[k + 1];
                                }
                            } else { // Suppression du dernier owner
                                lock.nb_owners--;
                            }

                            // Vérifier s'il s'agissait de l'unique propriétaire du verrou
                            if (lock.nb_owners == 0) {
                                // Supprimer le verrou lui-même
                                lock.next_lock = -2;

                                if (lock.next_lock == -1) { // Dernier verrou
                                    lfd.f->first = -2;
                                }
                            }

                            lfd.f->lock_table[i] = lock;
                            break;
                        }
                    }
                }
                break;

            case F_RDLCK:
                //Si la table des vérrous est vide
                if(lfd.f->first = -2){
                    //Ajouter le vérrou
                    lfd.f->first = 0;
                    lfd.f->lock_table[0].len = lck->l_len;
                    lfd.f->lock_table[0].lock_owners[0] = lfd_owner;
                    lfd.f->lock_table[0].nb_owners = 1;
                    lfd.f->lock_table[0].next_lock = -1;
                    lfd.f->lock_table[0].starting_offset = lck->l_start + lck->l_whence;
                    lfd.f->lock_table[0].type = lck->l_type;

                }else{
                    int max = 0;
                    for(int i=0; i!=-2; i=lfd.f->lock_table[i].next_lock){
                        if(lck->l_len == 0){
                            if(lfd.f->lock_table[max].starting_offset > lfd.f->lock_table[i].starting_offset){//Indice pour sauvégarder les verrous par ordre croissant
                                    max = i;
                            }
                            if(lfd.f->lock_table[i].len == 0 || lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){
                                if(lfd.f->lock_table[i].type == F_WRLCK){
                                    exit(EXIT_FAILURE);
                                }
                                if(i = -1){//Dernier vérrou
                                    //Tous les vérrous sont compatible
                                    for(int j = 0; j<NB_LOCKS; i++){
                                        if(lfd.f->lock_table[j].next_lock = -2){//Case Vide
                                            lfd.f->lock_table[j].len = lck->l_len;
                                            lfd.f->lock_table[j].lock_owners[lfd.f->lock_table[j].nb_owners] = lfd_owner;
                                            lfd.f->lock_table[j].nb_owners +=1;
                                            lfd.f->lock_table[j].starting_offset = lck->l_start + lck->l_whence;
                                            lfd.f->lock_table[j].type = lck->l_type;                                     
                                            lfd.f->lock_table[max].next_lock = j;
                                            lfd.f->lock_table[j].next_lock = lfd.f->lock_table[max].next_lock;
                                        }

                                    }

                                }
                            }else if(lck->l_start + lck->l_whence > lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){//Aucun Chevauchement
                                if(i = -1 ){
                                    for(int j = 0; j<NB_LOCKS; i++){
                                        if(lfd.f->lock_table[j].next_lock = -2){//Case Vide
                                            lfd.f->lock_table[j].len = lck->l_len;
                                            lfd.f->lock_table[j].lock_owners[lfd.f->lock_table[j].nb_owners] = lfd_owner;
                                            lfd.f->lock_table[j].nb_owners +=1;
                                            lfd.f->lock_table[j].starting_offset = lck->l_start + lck->l_whence;
                                            lfd.f->lock_table[j].type = lck->l_type;                                     
                                            lfd.f->lock_table[max].next_lock = j;
                                            lfd.f->lock_table[j].next_lock = lfd.f->lock_table[max].next_lock;
                                        }

                                    }
                                }
                            }
                        }
                    }
                }
            case F_WRLCK:
                if(lfd.f->first = -2){
                    //Ajouter le vérrou
                    lfd.f->first = 0;
                    lfd.f->lock_table[0].len = lck->l_len;
                    lfd.f->lock_table[0].lock_owners[0] = lfd_owner;
                    lfd.f->lock_table[0].nb_owners = 1;
                    lfd.f->lock_table[0].next_lock = -1;
                    lfd.f->lock_table[0].starting_offset = lck->l_start + lck->l_whence;
                    lfd.f->lock_table[0].type = lck->l_type;

                }else{
                    int max = 0;
                    for(int i=0; i!=-2; i=lfd.f->lock_table[i].next_lock){
                        if(lck->l_len == 0){
                            if(lfd.f->lock_table[max].starting_offset > lfd.f->lock_table[i].starting_offset){//Indice pour sauvégarder les verrous par ordre croissant
                                    max = i;
                            }
                            if(lfd.f->lock_table[i].len == 0 || lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){
                                exit(EXIT_FAILURE);
                    
                            }else if(lck->l_start + lck->l_whence > lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){//Aucun Chevauchement
                                if(i = -1 ){
                                    for(int j = 0; j<NB_LOCKS; i++){
                                        if(lfd.f->lock_table[j].next_lock = -2){//Case Vide
                                            lfd.f->lock_table[j].len = lck->l_len;
                                            lfd.f->lock_table[j].lock_owners[lfd.f->lock_table[j].nb_owners] = lfd_owner;
                                            lfd.f->lock_table[j].nb_owners +=1;
                                            lfd.f->lock_table[j].starting_offset = lck->l_start + lck->l_whence;
                                            lfd.f->lock_table[j].type = lck->l_type;                                     
                                            lfd.f->lock_table[max].next_lock = j;
                                            lfd.f->lock_table[j].next_lock = lfd.f->lock_table[max].next_lock;
                                        }

                                    }
                                }


                            }
                        }
                    }
                }
            }
        
    } else if (cmd == F_SETLKW) {
        switch (lck->l_type) {
            case F_UNLCK:
                // Supprimer le verrou pour lfd_owner s'il existe
                for (int i = 0; i < NB_LOCKS; i++) {
                    rl_lock lock = lfd.f->lock_table[i];
                    if (lock.next_lock == -2) // Fin de la liste des verrous
                        break;

                    // Vérifier si lfd_owner est propriétaire du verrou
                    for (size_t j = 0; j < lock.nb_owners; j++) {
                        if (lock.lock_owners[j].proc == lfd_owner.proc && lock.lock_owners[j].des == lfd_owner.des) {
                            // Supprimer lfd_owner du tableau des propriétaires
                            if (j != lock.nb_owners - 1) { // Suppression d'un owner différent du dernier
                                for (size_t k = j; k < lock.nb_owners - 1; k++) {
                                    lock.lock_owners[k] = lock.lock_owners[k + 1];
                                }
                            } else { // Suppression du dernier owner
                                lock.nb_owners--;
                            }

                            // Vérifier s'il s'agissait de l'unique propriétaire du verrou
                            if (lock.nb_owners == 0) {
                                // Supprimer le verrou lui-même
                                lock.next_lock = -2;

                                if (lock.next_lock == -1) { // Dernier verrou
                                    lfd.f->first = -2;
                                }
                            }

                            lfd.f->lock_table[i] = lock;
                            break;
                        }
                    }
                }
                break;

            case F_RDLCK:
                //Si la table des vérrous est vide
                if(lfd.f->first = -2){
                    //Ajouter le vérrou
                    lfd.f->first = 0;
                    lfd.f->lock_table[0].len = lck->l_len;
                    lfd.f->lock_table[0].lock_owners[0] = lfd_owner;
                    lfd.f->lock_table[0].nb_owners = 1;
                    lfd.f->lock_table[0].next_lock = -1;
                    lfd.f->lock_table[0].starting_offset = lck->l_start + lck->l_whence;
                    lfd.f->lock_table[0].type = lck->l_type;

                }else{
                    int max = 0;
                    for(int i=0; i!=-2; i=lfd.f->lock_table[i].next_lock){
                        if(lck->l_len == 0){
                            if(lfd.f->lock_table[max].starting_offset > lfd.f->lock_table[i].starting_offset){//Indice pour sauvégarder les verrous par ordre croissant
                                    max = i;
                            }
                            if(lfd.f->lock_table[i].len == 0 || lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){
                                if(lfd.f->lock_table[i].type == F_WRLCK){
                                    exit(EXIT_FAILURE);
                                }
                                if(i = -1){//Dernier vérrou
                                    //Tous les vérrous sont compatible
                                    for(int j = 0; j<NB_LOCKS; i++){
                                        if(lfd.f->lock_table[j].next_lock = -2){//Case Vide
                                            lfd.f->lock_table[j].len = lck->l_len;
                                            lfd.f->lock_table[j].lock_owners[lfd.f->lock_table[j].nb_owners] = lfd_owner;
                                            lfd.f->lock_table[j].nb_owners +=1;
                                            lfd.f->lock_table[j].starting_offset = lck->l_start + lck->l_whence;
                                            lfd.f->lock_table[j].type = lck->l_type;                                     
                                            lfd.f->lock_table[max].next_lock = j;
                                            lfd.f->lock_table[j].next_lock = lfd.f->lock_table[max].next_lock;
                                        }

                                    }

                                }
                            }else if(lck->l_start + lck->l_whence > lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){//Aucun Chevauchement
                                if(i = -1 ){
                                    for(int j = 0; j<NB_LOCKS; i++){
                                        if(lfd.f->lock_table[j].next_lock = -2){//Case Vide
                                            lfd.f->lock_table[j].len = lck->l_len;
                                            lfd.f->lock_table[j].lock_owners[lfd.f->lock_table[j].nb_owners] = lfd_owner;
                                            lfd.f->lock_table[j].nb_owners +=1;
                                            lfd.f->lock_table[j].starting_offset = lck->l_start + lck->l_whence;
                                            lfd.f->lock_table[j].type = lck->l_type;                                     
                                            lfd.f->lock_table[max].next_lock = j;
                                            lfd.f->lock_table[j].next_lock = lfd.f->lock_table[max].next_lock;
                                        }

                                    }
                                }
                            }
                        }
                    }
                }
            case F_WRLCK:
                if(lfd.f->first = -2){
                    //Ajouter le vérrou
                    lfd.f->first = 0;
                    lfd.f->lock_table[0].len = lck->l_len;
                    lfd.f->lock_table[0].lock_owners[0] = lfd_owner;
                    lfd.f->lock_table[0].nb_owners = 1;
                    lfd.f->lock_table[0].next_lock = -1;
                    lfd.f->lock_table[0].starting_offset = lck->l_start + lck->l_whence;
                    lfd.f->lock_table[0].type = lck->l_type;

                }else{
                    int max = 0;
                    for(int i=0; i!=-2; i=lfd.f->lock_table[i].next_lock){
                        if(lck->l_len == 0){
                            if(lfd.f->lock_table[max].starting_offset > lfd.f->lock_table[i].starting_offset){//Indice pour sauvégarder les verrous par ordre croissant
                                    max = i;
                            }
                            if(lfd.f->lock_table[i].len == 0 || lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){
                                exit(EXIT_FAILURE);
                    
                            }else if(lck->l_start + lck->l_whence > lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len){//Aucun Chevauchement
                                if(i = -1 ){
                                    for(int j = 0; j<NB_LOCKS; i++){
                                        if(lfd.f->lock_table[j].next_lock = -2){//Case Vide
                                            lfd.f->lock_table[j].len = lck->l_len;
                                            lfd.f->lock_table[j].lock_owners[lfd.f->lock_table[j].nb_owners] = lfd_owner;
                                            lfd.f->lock_table[j].nb_owners +=1;
                                            lfd.f->lock_table[j].starting_offset = lck->l_start + lck->l_whence;
                                            lfd.f->lock_table[j].type = lck->l_type;                                     
                                            lfd.f->lock_table[max].next_lock = j;
                                            lfd.f->lock_table[j].next_lock = lfd.f->lock_table[max].next_lock;
                                        }

                                    }
                                }


                            }
                        }
                    }
                }
            }
       
    } else { // cmd == F_GETLK
        // Vérifier si un verrou existe pour la région spécifiée par lck
       // Rechercher le verrou sur le fichier
        for (int i = 0; i < NB_LOCKS; i++) {
            rl_lock lock = lfd.f->lock_table[i];
            if (lock.next_lock == -2) {
                lck->l_type = F_UNLCK;
                break;
            } else if (lock.starting_offset == lck->l_start && lock.len == lck->l_len) {
                if (lock.nb_owners > 0) {
                    lck->l_type = lock.type;
                    lck->l_pid = lock.lock_owners[0].proc;
                } else {
                    lck->l_type = F_UNLCK;
                }
                break;
            }
        }
    }

    if ((code = pthread_mutex_unlock(&(lfd.f->mutex))) != 0)
        thread_error(__FILE__, __LINE__, code, "unlock");

    return 0;
}


/* ------------------------------------------------------------------------------------------------------------*/

rl_descriptor rl_dup( rl_descriptor lfd ){

    // Duplication du descripteur
    int newd = dup(lfd.d);
    if(newd == -1) // En cas d'échec
        return (rl_descriptor){-1, NULL};
    owner lfd_owner = { .proc = getpid(), .des = lfd.d };

	// Verrouiller le mutex du fichier d'origine
    pthread_mutex_lock(&(lfd.f->mutex));

	// Créer un nouveau rl_descriptor avec le nouveau descripteur
    rl_descriptor new_rl_descriptor = {newd, lfd.f};  // 

	// Déverrouiller le mutex du fichier d'origine
    pthread_mutex_unlock(&(lfd.f->mutex));

	// Retourner le nouveau rl_descriptor
    return new_rl_descriptor;
}

/* ------------------------------------------------------------------------------------------------------------*/

rl_descriptor rl_dup2(rl_descriptor lfd, int newd){
    // Vérifier si les descripteurs sont identiques
    if (lfd.d == newd) {
        return lfd; // Les descripteurs sont déjà identiques, retourner le rl_descriptor d'origine
    }

    // Verrouiller le mutex du fichier d'origine
    pthread_mutex_lock(&(lfd.f->mutex));

    // Dupliquer le descripteur de fichier
    int fd = dup2(lfd.d, newd);
    if (fd == -1) {
        // Erreur lors de la duplication du descripteur
        pthread_mutex_unlock(&(lfd.f->mutex));
        return (rl_descriptor){-1, NULL};
    }

    // Créer un nouveau rl_descriptor avec le nouveau descripteur
    rl_descriptor new_rl_descriptor = {newd, lfd.f};

    // Déverrouiller le mutex du fichier d'origine
    pthread_mutex_unlock(&(lfd.f->mutex));

    // Retourner le nouveau rl_descriptor
    return new_rl_descriptor;
}

/* ------------------------------------------------------------------------------------------------------------*/

pid_t rl_fork(){
    // Effectuer la duplication de processus avec fork()
    pid_t pid = fork();
    // Dans les cas pid > 0 et cas d'erreur, on fait les mêmes choses que pid_t fork()

    if (pid == 0) {
        // Code exécuté par le nouvel enfant

        // Parcourir tous les fichiers ouverts par le processus
        for (int i = 0; i < fichiers.nb_files; i++) {
            rl_open_file *file = fichiers.tab_open_files[i];

            // Verrouiller le mutex du fichier
            pthread_mutex_lock(&(file->mutex));

            // Dupliquer tous les verrous du parent pour le nouvel enfant
            for (int j = 0; j < NB_LOCKS; j++) {
                rl_lock lock = file->lock_table[j];

                // Vérifier si le verrou est utilisé
                if (lock.next_lock != -2) {
                    // Dupliquer les propriétaires du verrou
                    for (int k = 0; k < lock.nb_owners; k++) {
                        owner old_owner = lock.lock_owners[k];
                        owner new_owner = {old_owner.proc, old_owner.des};

                        // Ajouter le nouvel owner au verrou du nouvel enfant
                        lock.lock_owners[k] = new_owner;
                    }
                }
            }

            // Déverrouiller le mutex du fichier
            pthread_mutex_unlock(&(file->mutex));
        }
    }

    // Retourner l'ID du processus enfant
    return pid;
}

/* ------------------------------------------------------------------------------------------------------------*/
