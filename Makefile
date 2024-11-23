all:
	gcc -Wall -o test-pingpong-semaphore queue.o ppos-all.o ppos-core-aux.c pingpong-semaphore.c
	gcc -Wall -o test-pingpong-racecond  queue.o ppos-all.o ppos-core-aux.c pingpong-racecond.c
	gcc -Wall -o test-pingpong-mqueue    queue.o ppos-all.o ppos-core-aux.c pingpong-mqueue.c -lm
clean:
	rm test-pingpong-semaphore
	rm test-pingpong-racecond
	rm test-pingpong-mqueue