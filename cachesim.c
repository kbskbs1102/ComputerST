#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <strings.h>
#include <limits.h>

//메모리 주소를 저장하기위한 64bit의 변수 선언
typedef unsigned long long int mem_addr_t;

//캐시의 인자들을 다루기 위한 구조체 선언
typedef struct
{
	int s; // 2**s cache sets 
	int b; // cacheline block size 2**b bytes 
	int E; // number of cachelines per set 
	int S; // number of sets S = 2**s 
	int B; // cacheline block size B = 2**b 
} cache_param_t;

//시뮬레이터 도움말 출력
void printUsage(char *argv[])
{
	printf("Usage: %s [-h] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
	printf("Options:\n");
	printf("  -h         Print this help message.\n");
	printf("  -s <num>   Number of set index bits.\n");
	printf("  -E <num>   Number of lines per set.\n");
	printf("  -b <num>   Number of block offset bits.\n");
	printf("  -t <file>  Trace file.\n");
	printf("\nExamples:\n");
	printf("  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
	exit(0);
}

//시뮬레이터 결과 출력
void printSummary(int load_hit_count, int store_hit_count, int load_miss_count, int store_miss_count)
{
	int total_load = load_hit_count + load_miss_count;
	int total_store = store_hit_count + store_miss_count;
	printf("total load: %d\ttotal store: %d \nload hits: %d\tstore hits: %d \nload misses: %d\tstore misses: %d \n",
			total_load, total_store, load_hit_count, store_hit_count, load_miss_count, store_miss_count);
}

int main(int argc, char **argv)
{
	cache_param_t par; //구조체 생성

	char *trace_file;  //trace_file
	char c;

	//getopt함수를 이용해 입력받은 인자들을 구조체에 저장
	while ((c = getopt(argc, argv, "s:E:b:t:h")) != -1) 
	{
		switch (c)
		{
		case 's':
			par.s = atoi(optarg);
			break;
		case 'E':
			par.E = atoi(optarg);
			break;
		case 'b':
			par.b = atoi(optarg);
			break;
		case 't':
			trace_file = optarg;
			break;
		case 'h':
			printUsage(argv);
			exit(0);
		default:
			printUsage(argv);
			exit(1);
		}
	}

	//인자를 전달받는 중에 오류가 생기면 메세지 출력
	if (par.s == 0 || par.E == 0 || par.b == 0 || trace_file == NULL)
	{
		printf("%s: Missing required command line argument\n", argv[0]);
		printUsage(argv);
		exit(1);
	}

	//S, B 계산
	par.S = (1 << par.s);
	par.B = (1 << par.b);

	//line을 위한 구조체
	typedef struct
	{
		int valid;
		mem_addr_t tag;
		int timestamp; //LRU를 위한 시간표시
	} line_st;

	//set을 위한 구조체
	typedef struct
	{
		line_st *lines;
	} cache_set;

	//cache를 위한 구조체
	typedef struct
	{
		cache_set *sets;
	} cache_t;

	
	cache_t cache;

	//set과 line을 위한 메모리 할당
	cache.sets = malloc(par.S * sizeof(cache_set));
	for (int i = 0; i < par.S; i++)
	{
		cache.sets[i].lines = malloc(sizeof(line_st) * par.E);
	}

	//횟수를 카운트하기 위한 변수 선언
	int total_load_count = 0, load_hit_count = 0, store_hit_count = 0;
	int total_store_count = 0, load_miss_count = 0, store_miss_count = 0;
	int eviction_count = 0;

	char act;		// L,S
	int size;		// size read in from file
	int TSTAMP = 0; // value for LRU
	int empty = -1; // index of empty space
	int H = 0;		// is there a hit
	int E = 0;		// is there an eviction
	mem_addr_t addr;//주소 저장을 위한 변수

	//파일을 열고 내용을 읽는다
	int s; // 2**s cache sets 
	int b; // cacheline block size 2**b bytes 
	int E; // number of cachelines per set 
	int S; // number of sets S = 2**s 
	int B; // cacheline block size B = 2**b 

	FILE *traceFile = fopen(trace_file, "r");
	if (traceFile != NULL){
		//" "를 기준으로 trace파일의 내용을 분리
		while (fscanf(traceFile, " %c %llx %d", &act, &addr, &size) == 3){
			int toEvict = 0;
			// keeps track of what to evict
			if (act != 'I'){
				// calculate address tag and set index
				mem_addr_t addr_tag = addr >> (par.s + par.b); //addr 중 입력받은 s,b를 뺀 상위 비트가 태그
				int tag_size = (64 - (par.s + par.b));         //태그의 크기는 64-(s+b)
				unsigned long long temp = addr << (tag_size);  //태그의 크기만큼 addr의 상위비트를 임시변수에 저장
				unsigned long long setid = temp >> (tag_size + par.b);  //id를 temp의 b를 뺀 하위비트로 설정 
				cache_set set = cache.sets[setid];
				int low = INT_MAX;

				//hit했는지 판별
				for (int e = 0; e < par.E; e++){
					if (set.lines[e].valid == 1){          //유효비트가 1이라면 
						if (set.lines[e].tag == addr_tag){ //태그와 주소의 상위 tag_size만큼 비교하고 같다면 hit
							if (act == 'l' || act == 'L'){
								load_hit_count++;
							}else{
								store_hit_count++;
							}
							H = 1; //hit 했으므로 표시
							set.lines[e].timestamp = TSTAMP;//시간 순서 표시
							TSTAMP++;
						}
						else if (set.lines[e].timestamp < low){//시간 순서 비교
							low = set.lines[e].timestamp;
							toEvict = e;
						}
					}
					else if (empty == -1){
						empty = e;
					}
				}

				//miss했다면
				if (H != 1){
					if (act == 'l' || act == 'L'){ //miss 카운트 증가
						load_miss_count++;
					}else{
						store_miss_count++;
					}
					//빈 line이 존재한다면
					if (empty > -1){
						set.lines[empty].valid = 1;
						set.lines[empty].tag = addr_tag;
						set.lines[empty].timestamp = TSTAMP;
						TSTAMP++;    //캐시에 올림
					}
					//set이 가득 찼다면 eviction
					else if (empty < 0){
						E = 1;
						set.lines[toEvict].tag = addr_tag;
						set.lines[toEvict].timestamp = TSTAMP;
						TSTAMP++;
						eviction_count++;
					}
				}
				empty = -1;
				H = 0;
				E = 0;
			}
		}
	}

	//결과 출력
	printSummary(load_hit_count, store_hit_count, load_miss_count, store_miss_count);
	return 0;
}
