ALL: build

build: mlfq

mlfq: mlfq.c
	clang -Wall -Wpedantic -Wextra -Werror mlfq.c -o mlfq

clean:
	rm mlfq

