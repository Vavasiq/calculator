#include "mathlib/functions.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Флаги
enum { 
    HAVE_A = 1,
    HAVE_B = 2,
    HAVE_OP = 4
};

struct Data {
    int a;
    int b;
    int result;
    char op;
    unsigned char have;
};

/* Parser */
static int parse_int(const char* s, int& out) {
    char* end = NULL; 
    long v;

    if (!s || *s == '\0') return 0;
    errno = 0;
    v = strtol(s, &end, 10); 

    if (errno != 0) return 0; 
    if (end == s) return 0;
    if (*end != '\0') return 0;
    if (v > INT_MAX || v < INT_MIN) return 0;

    out = (int)v;
    return 1;
}

static int parse_args(int argc, char** argv, Data& d) {
    static option opts[] = {
        {"help", no_argument, 0, 'h'},
        {"a", required_argument, 0, 'a'},
        {"b", required_argument, 0, 'b'},
        {"op", required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };

    int c, idx = 0;
    while ((c = getopt_long(argc, argv, "ha:b:o:", opts, &idx)) != -1) {
        if (c == 'h') return 2; 
        if (c == 'a') {
            if (!parse_int(optarg, d.a)) return 1;
            d.have |= HAVE_A;
        } else if (c == 'b') {
            if (!parse_int(optarg, d.b)) return 1;
            d.have |= HAVE_B;
        } else if (c == 'o') {
            if (!optarg || strlen(optarg) != 1) return 1;
            d.op = optarg[0];
            d.have |= HAVE_OP;
        } else {
            return 1;
        }
    }
    return 0;
}

/*  Checker  */
static int check_args(const Data& d) {
    if ((d.have & HAVE_OP) == 0) return 1; 

    if (d.op == '!') {
        if ((d.have & HAVE_A) == 0) return 1;
        return 0;
    }

    if ((d.have & HAVE_A) == 0) return 1;
    if ((d.have & HAVE_B) == 0) return 1;

    if (d.op != '+' && d.op != '-' && d.op != '*' && d.op != '/' && d.op != '^') return 1;
    return 0;
}

/*  Calculator  */
static int calculate(struct Data* d) {
    switch (d->op) {
        case '+': return safe_add(d->a, d->b, d->result);
        case '-': return safe_sub(d->a, d->b, d->result);
        case '*': return safe_mul(d->a, d->b, d->result);
        case '/': return safe_div(d->a, d->b, d->result);
        case '^': return powi(d->a, d->b, d->result);
        case '!': return fact(d->a, d->result);
        default:  return MATH_DOMAIN; 
    }
}

/*  Printer  */
static void print_help() {
    printf("Usage:\n");
    printf("calculator -a <int> -b <int> -o <op>\n");
    printf("calculator -a <int> -o '!'\n");
    printf("Operations: + - * / ^ !\n"); 
}

static void print_result(const Data& d) {
    printf("%d\n", d.result);
}

static void print_math_error(int math_err) {
    if (math_err == MATH_DIV0) fprintf(stderr, "Error: division by zero\n");
    else if (math_err == MATH_OVERFLOW) fprintf(stderr, "Error: overflow\n");
    else if (math_err == MATH_DOMAIN) fprintf(stderr, "Error: invalid input\n");
    else fprintf(stderr, "Error: math\n");
}

/* Runner */
static int run(int argc, char** argv) {
    if (argc == 1) {   
        print_help();
        return 0;
    }

    Data d{};
    
    int st = parse_args(argc, argv, d);
    if (st == 2) 
    { 
        print_help(); 
        return 0;
    }

    if (st != 0) 
    { 
        fprintf(stderr, "Parse error\n"); 
        return 1; 
    }

    if (check_args(d) != 0)
    { 
        fprintf(stderr, "Args error\n");
        return 1; 
    } 

    int math_err = calculate(&d);
    if (math_err != MATH_OK) 
    { 
        print_math_error(math_err);
        return 1;
    }
    print_result(d);
    return 0;
}

int main(int argc, char** argv) {
    return run(argc, argv);
}
