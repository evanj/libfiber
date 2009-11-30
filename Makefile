CFLAGS=-Wall -Wextra -pedantic -Werror -g

all: basic-uc basic-sjlj example-uc example-sjlj

clean:
	rm *.o basic-clone basic-uc basic-sjlj basic-*.o example-* &> /dev/null || true
	
debug: clean
	make "CC=gcc -g -Wall -pedantic -DLF_DEBUG"

basic-clone: basic-clone.o
basic-uc: basic-uc.o
basic-sjlt: basic-sjlj.o

example-uc: libfiber-uc.o example.o
	$(CC) libfiber-uc.o example.o -o example-uc

example-clone: libfiber-clone.o example.o
	$(CC) libfiber-clone.o example.o -o example-clone
	
example-sjlj: libfiber-sjlj.o example.o
	$(CC) libfiber-sjlj.o example.o -o example-sjlj

libfiber-uc.o: libfiber.h
libfiber-clone.o: libfiber.h
libfiber-sjlj.o: libfiber.h
example.o: libfiber.h
