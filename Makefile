CC      =gcc
CFLAGS  =-Wall -g -std=c11 -pedantic
CCO     =$(CC) -c $<
LDLIBS  =-lrt -pthread  #linux
OBJECTS =rl_lock_library.o test1.o
ALL     =  test1


all: $(OBJECTS)
	$(CC) -o $(ALL) $(CFLAGS) $(OBJECTS)

rl_lock_library.o: rl_lock_library.c rl_lock_library.h
	$(CCO)

test1.o: test1.c rl_lock_library.h
	$(CCO)

clean:
	rm -rf $(ALL) *.o