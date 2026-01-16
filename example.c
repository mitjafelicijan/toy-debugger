#include <stdio.h>
#include <stdlib.h>

typedef enum {
    STATUS_IDLE,
    STATUS_RUNNING,
    STATUS_STOPPED
} Status;

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point top_left;
    Point bottom_right;
} Rectangle;

typedef struct {
    int q;
    int w;
} Bar;

int add_numbers(int a, int b) {
    int result = a + b;
    return result;
}

int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

void print_rectangle(Rectangle rect) {
    printf("> Rect: (%d, %d) to (%d, %d)\n",
           rect.top_left.x, rect.top_left.y,
           rect.bottom_right.x, rect.bottom_right.y);
}

int main(int argc, char **argv) {
    const char *myenv = getenv("MYENV");

    int a = 100;
    int b = 123;
    int c = add_numbers(a, b);

    Bar bar = { .q = 565, .w = 949 };
    Status status = STATUS_RUNNING;

    Rectangle rect = {
        .top_left = { .x = 10, .y = 20 },
        .bottom_right = { .x = 50, .y = 80 }
    };

    printf("> MYENV: %s\n", myenv ? myenv : "NULL");
    printf("> c: %d (via add_numbers)\n", c);
    printf("> bar.q: %d\n", bar.q);
    printf("> status: %d\n", status);

    print_rectangle(rect);

    int fib5 = fibonacci(5);
    printf("> fib(5): %d\n", fib5);

    int arr[] = {10, 20, 30, 40, 50};
    int *ptr = arr;
    for (int i = 0; i < 5; i++) {
        printf("> arr[%d] = %d (via ptr: %d)\n", i, arr[i], *(ptr + i));
    }

    for (int i = 0; i < 3; i++) {
        printf("> loop %d\n", i);
    }

    return 0;
}
