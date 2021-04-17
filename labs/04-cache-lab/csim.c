#define  _GNU_SOURCE
#include "cachelab.h"
#include <getopt.h> 
#include <stdlib.h> 
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

void printUsage() {
    printf("Usage: ./csim-ref [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n");
    printf("    -h: Optional help flag that prints usage info\n");
    printf("    -v: Optional verbose flag that displays trace info\n");
    printf("    -s <s>: Number of set index bits (S = 2s is the number of sets)\n");
    printf("    -E <E>: Associativity (number of lines per set)\n");
    printf("    -b <b>: Number of block bits (B = 2b is the block size)\n");
    printf("    -t <tracefile>: Name of the valgrind trace to replay\n");
}

typedef struct cache_line {
    int v;  // valid bit
    int tag;
    int last;
} cache_line;

// For debug
void print_cache (int S, int E, cache_line** cache) {
    for (int i=0; i < S; i++) {
        for (int j=0; j < E; j++) {
            printf("cache[%d][%d]: v: %d, tag: %d, last: %d\n", i, j, cache[i][j].v, cache[i][j].tag, cache[i][j].last);
        }
    } 
}

void free_cache (int S, int E, cache_line** cache) {
    for (int i=0; i < S; i++) {
        free(cache[i]);
    }
    free(cache);
}

void update(int s, int b, int E, int inst_index, int v, cache_line** cache, unsigned long long address, int* hit_count, int* miss_count, int* eviction_count) {
    // printf("address: %llx\n", address);
    
    unsigned long long one = 1;
    unsigned long long tag_bits = (address >> (s+b)) & ((one << (64 - s - b)) - 1);
    unsigned long long set_index = (address >> b) & ((one << s) - 1);
    unsigned long long block_offset = address & ((one << b) - 1);

    printf(" tag: %llx / set; %llx / block: %llx /", tag_bits, set_index, block_offset);

    // Hit
    for (int i=0; i < E; i++) {
        if ((cache[set_index][i].v == 1) && (cache[set_index][i].tag == tag_bits)){
            cache[set_index][i].last = inst_index;
            if (v) {
                printf(" hit");
            }
            (*hit_count) ++;
            return;
        }
    }

    // Miss
    if (v) {
        printf(" miss");
    }
    (*miss_count) ++;
    for (int i=0; i < E; i++) {
        if (cache[set_index][i].v == 0) {
            cache[set_index][i].v = 1;
            cache[set_index][i].tag = tag_bits;
            cache[set_index][i].last = inst_index;
            return;
        }
    }

    // Eviction
    if (v) {
        printf(" eviction");
    }
    (*eviction_count) ++;
    int MIN_TIMESTAMP = 2147483647; // INT_MAX
    int MIN_TIMESTAMP_INDEX = 0;
    for (int i=0; i < E; i++) {
        if (MIN_TIMESTAMP > cache[set_index][i].last) {
            MIN_TIMESTAMP = cache[set_index][i].last;
            MIN_TIMESTAMP_INDEX = i;
        }
    }
    cache[set_index][MIN_TIMESTAMP_INDEX].tag = tag_bits;
    cache[set_index][MIN_TIMESTAMP_INDEX].last = inst_index;
    return;
}

void run(int s, int E, int b, char* t, int v, int* hit_count, int* miss_count, int* eviction_count) {
    // Initialize cache
    int S = pow(2, s); // 2^s sets 
    cache_line** cache = (cache_line **)malloc(S * sizeof(cache_line *));
    for (int i=0; i < S; i++) {
        cache[i] = (cache_line *)malloc(E * sizeof(cache_line));
    }

    for (int i=0; i < S; i++) {
        for (int j=0; j < E; j++) {
            cache[i][j].v = 0;
            cache[i][j].tag = -1;
            cache[i][j].last = 0;
        }
    }

    // Simulation
    FILE* fp = fopen(t, "r");
    if (fp == NULL) {
        printf("Error while opening the file %s.\n", t);
        exit(EXIT_FAILURE);
    }

    size_t len = 0;
    char* line = NULL;
    char* format = " %c %llx,%d";
    char operation;
	unsigned long long address = 0;
    int size = 0;
    int inst_index = 0;

    while (getline(&line, &len, fp) != -1) {
        sscanf(line, format, &operation, &address, &size);
        if (v) {
            printf("%c %llx,%d", operation, address, size);
        }
        // process operation
        switch(operation) {
            case 'I':
                // "I": instruction load
                // Ignore all instruction cache accesses (lines starting with "I")
                break;
            case 'M':
                // "M": data modify, i.e. a data load ("L") followed by a data store ("S")
                update(s, b, E, inst_index, v, cache, address, hit_count, miss_count, eviction_count);
                if (v) {
                    printf(" hit");
                }
                (*hit_count) ++;
                break;
            case 'L':
                // "L": data load
                update(s, b, E, inst_index, v, cache, address, hit_count, miss_count, eviction_count);
                break;
            case 'S':
                // "S": data store
                update(s, b, E, inst_index, v, cache, address, hit_count, miss_count, eviction_count);
                break;
        }
        inst_index ++;
        if (v) {
            printf("\n");
        }
    }

    fclose(fp);

    // Free cache
    free_cache(S, E, cache);
}

int main(int argc, char **argv)
{
    int opt;
    int v = 0, s = 0, E = 0, b = 0;
    char t[1000];
    /* looping over arguments */
    // Usage: ./csim-ref [-hv] -s <s> -E <E> -b <b> -t <tracefile>
    while(-1 != (opt = (getopt(argc, argv, "hvs:E:b:t:")))) {
        switch(opt) {
            case 'h':
                printUsage();
                exit(1);
                break;
            case 'v':
                v = 1;
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
				E = atoi(optarg);
				break;
			case 'b':
				b = atoi(optarg);
				break;
			case 't':
				strcpy(t, optarg);
				break;
            default:
                printUsage();
                break;
        }
    }

    int hit_count = 0;
    int miss_count = 0;
    int eviction_count = 0;

    run( s, E, b, t, v, &hit_count, &miss_count, &eviction_count);
    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}
