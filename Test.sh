#!/bin/bash

# Compiler le programme de test
#gcc test.c -o test -lrl_lock_library -pthread
make

# Créer un fichier vide
#touch fichier.txt

# Lancer les tests en parallèle
#./test file1.txt 
./test1


# Attendre la fin des tests
wait

# Supprimer le fichier
rm fichier.txt