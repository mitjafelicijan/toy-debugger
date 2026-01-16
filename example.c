#include <stdio.h>
#include <stdlib.h>

typedef struct {
	int q;
	int w;
} Bar;

int main(void) {
	const char *myenv = getenv("MYENV");

	int a = 100;
	int b = 123;
	int c = a + b;

	Bar bar = { .q = 565, .w = 949 };

	printf("> MYENV: %s\n", myenv);
	printf("> c: %d\n", c);
	printf("> bar.q: %d\n", bar.q);

	for (int i=0; i<10; i++) {
		printf("> loop %d\n", i);
	}

	return 0;
}
