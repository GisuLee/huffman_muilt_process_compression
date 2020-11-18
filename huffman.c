#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>
#include <malloc.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#define MAX_BUFFER_SIZE 256
#define INVALID_BIT_READ -1
#define INVALID_BIT_WRITE -1
#define FAILURE 1
#define SUCCESS 0
#define FILE_OPEN_FAIL -1
#define END_OF_FILE -1
#define MEM_ALLOC_FAIL -1

typedef struct {
	long mtype;
	char mtext[80];
} msgblock;						//멀티프로세스를 할 때, 메시지큐의 내용 구조체

typedef struct {
	int count;
	int size;
	int file_index[1000];
	char pathName[1000][255];
} workspace;					//자식 프로세스들이 할당받은 작업을 저장하는 구조체

typedef struct file_s{
	long int header_pos;
	char pathName[255];			//file's path & name
	unsigned int size;			//byte
} file_s;						//파일의 헤더위치, 경로 및 이름, 사이즈 저장

typedef struct file_node{
	file_s data;
	struct file_node* next;
}file_node;						//파일구조체의 노드

typedef struct Queue{
	file_node *front;
	file_node *rear;
	int count;				
}Queue;							//파일구조체의 연결리스트

typedef struct {
	int index;
	unsigned int weight;
} node_t;						//허프만 테이블을 저장하는 구조체

typedef struct {
	int count;
	char pathName[1000][255];
	long int header_pos[1000];
	int size[1000];
}metadata;						//메타테이터를 저장하는 구조체

//Global variable
int num_alphabets = 256;		//아스키 코드가 표현할 수 있는 문자의 갯수
int num_active = 0;				//실제 파일의 존재하는 문자의 갯수
int *frequency = NULL;			//존재하는 문자에 대한 빈도수를 저장하는 배열(동적할당)
unsigned int original_size = 0;	//파일의 본 사이즈
node_t *nodes = NULL;			//허프만 트리를 만들기 위한 최상위 노드
int num_nodes = 0;				//허프만 트리의 노드의 갯수
int *leaf_index = NULL;			//자식 노드
int *parent_index = NULL;		//부모 노드
int free_index = 1;				
int *stack;
int stack_top;
unsigned char buffer[MAX_BUFFER_SIZE];
int bits_in_buffer = 0;
int current_bit = 0;
int eof_input = 0;

//Function define
void getDirPath(char *result, char *dirPath);
void getDirPathParent(char *result, char *dirPath);
int read_header(FILE *f, char *ofile,const int start_pos);
int write_header(file_node* node_ptr,FILE *f,char* ifile);
int read_bit(FILE *f);
int write_bit(FILE *f, int bit);
int flush_buffer(FILE *f);
void decode_bit_stream(FILE *fin, FILE *fout);
int decode(const char* ifile, char *odirpath,const int start_pos);
void encode_alphabet(FILE *fout, int character);
int encode(file_node* node_ptr,char* ifile, const char *ofile);
void build_tree();
void add_leaves();
int add_node(int index, int weight);
void finalise();
void init();
Queue* createQueue();
void add(Queue* qptr, file_s data);
int get_strsize(const char *p);
void merge_filelist(Queue *fileList_queue,int *merge_size,int argc, char **argv);
void search_dir(char *path,int* merge_size,Queue *fileList_queue);
void write_meta(Queue* headerlist_ptr, const char *ofile);
void read_meta(Queue* headerlist_ptr,metadata* meta,const char *ifile);
void merge_decompress(Queue* filelist_prt,const char* ifile,char *ofile);
int get_cores();
void print_help();
void handler(int signo);
void compress_child(Queue * header_ptr, int index, metadata * meta, int argc, char ** argv);
void compress_parent(Queue * header_ptr, int index, metadata * meta, int argc, char ** argv);
void decompress_child(int index, metadata * meta, int argc, char ** argv);
void parent_divide(Queue * header_ptr, pid_t * childs_pid, int n);

/* 큐를 생성한다. */
Queue* createQueue(){
	Queue* newQueue = (Queue*)malloc(sizeof(Queue));
	newQueue->count = 0;
	newQueue->front=NULL;
	newQueue->rear=NULL;
	return newQueue;
}

/*	큐에 노드를 삽입한다.
	Queue* qptr = 큐의 헤더 포인터 
	file_s data = 큐에 추가할 노드 데이터 */
void add(Queue* qptr, file_s data){
	file_node* newNode = (file_node*)malloc(sizeof(file_node));
	newNode->data = data;
	newNode->next = NULL;
	if(qptr->count==0){
		qptr->front=newNode;
		qptr->rear=newNode;
	}else{
		qptr->rear->next=newNode;
		qptr->rear=newNode;
	}
	qptr->count++; 	
}

/* 문자열 *p의 사이즈를 구한다 */
int get_strsize(const char *p){
	int i;
	for(i=0;p[i]!='\0';i++);
	return i;
}

//	어떤 파일의 경로 및 이름에 대한 입력에서 경로만 반환한다.
//	if dirPath = "./Folder1/Folder2/FileName" then, result = "./Folder1/Foder2"
void getDirPath(char *result, char *dirPath){
	
	char *p;
	int i=0;

	p = strrchr(dirPath,'/');
	while(&dirPath[i] != p){
		result[i] = dirPath[i];
		i++;	
	}
	result[i] = '\0';
}	

//	어떤 파일의 경로 및 이름에 대한 입력에서 최상위 부모디렉토리경로를 반환한다.
//	if dirPath = "./Folder1/Folder2/FileName" then, result = "./Folder1"
void getDirPathParent(char *result, char *dirPath){
	char *p;
	int i=0;
	p = strtok(dirPath,"/");
	strcpy(result,p);
	strcat(result,"/");
	p = strtok(NULL,"/");
	strcat(result,p);
	strcat(result,"/");
	p = strtok(NULL,"/");
	strcat(result,p);
	strcat(result,"/\0");
}

/*	사용자에 입력(폴더 및 파일이름의 리스트)를 받아 깊이우선탐색으로 ASCKII 파일들만 추출하여 Queue에 저장한다. 
	fileList_queue =  큐의 헤더 포인터
	merge_size = 파일리스트가 생성됬을 때의 사이즈의 합 반환 
	**argv = 사용자 입력 문자열 포인터 */
void merge_filelist(Queue *fileList_queue,int *merge_size,int argc, char **argv){
	char 		*filename;
	char 		nextpath[255];
	char		command[255];
	char		output[255];
	int 		kind;
	file_s 		filetemp;
	struct dirent 	*dent;
	struct stat	buf;
	FILE 		*p;
	
	for(int i=2;i<argc-1;i++){
		filename = argv[i];
		stat(filename,&buf);
		kind = buf.st_mode & S_IFMT;

		switch (kind){
			
			//파일의 종류가 폴더일 경우, search_dir 호출
			case S_IFDIR:
				if(filename[get_strsize(filename)-1] != '/'){
					strcpy(nextpath,filename);
					strcat(nextpath,"/");				
				}
				search_dir(nextpath,merge_size,fileList_queue);			
				nextpath[0] = '\0';
				break;

			//파일의 종류가 Regular일 경우, ASKII 파일인지 검사하고, 파일리스트를 저장
			default:
				strcpy(command,"file ");
				strcat(command, filename);
				p = popen(command,"r");
				if(p!=NULL){
					while(fgets(output,sizeof(output),p)!=NULL);
					if((strstr(output,"ASCII"))!=NULL){
						strcpy(filetemp.pathName,filename);
						filetemp.size = (int)buf.st_size;
						add(fileList_queue,filetemp);
						*merge_size +=filetemp.size;
					}
				}
				pclose(p);
				break;				
		}
	
	}
}

/*	폴더의 경로를 입력받아, 폴더내의 ASCKII 파일을 추출하고, 폴더가 존재하면 재귀호출 
	path = 검색할 폴더
	*merge_size =  파일리스트의 사이즈 합을 반환
	*fileList_queue = 큐의 헤더 포인터 */
void search_dir(char *path,int* merge_size,Queue *fileList_queue){
	DIR 		*dp;
	file_s 		filetemp;
	int 		kind;
	char 		nextpath[255];
	char		filename[255];	
	struct dirent	*dent;
	struct stat 	buf;
	char 		output[255];
	char 		filepath[255];
	char 		command[255];
	FILE 		*p;

	if((dp=opendir(path)) == NULL){
		perror("Open Dir");
		exit(1);
	}
	
	while((dent = readdir(dp))){
		if((strcmp(dent->d_name,".")==0) || (strcmp(dent->d_name,"..")==0))
			continue;
		strcpy(filename,path);
		strcat(filename,dent->d_name);
		stat(filename,&buf);
		kind = buf.st_mode & S_IFMT;
		switch (kind){
			case S_IFDIR:
				if(filename[get_strsize(filename)-1] != '/'){
					strcpy(nextpath,path);
					strcat(nextpath,dent->d_name);
					strcat(nextpath,"/");			
				}

				search_dir(nextpath,merge_size,fileList_queue);
				nextpath[0] = '\0';
				break;
			default:
				strcpy(filepath,path);
				strcat(filepath,dent->d_name);
				strcpy(command,"file ");
				strcat(command, filepath);
				p = popen(command,"r");
				if(p!=NULL){
					while(fgets(output,sizeof(output),p)!=NULL);
					if((strstr(output,"ASCII"))!=NULL){
						strcpy(filetemp.pathName,filepath);
						filetemp.size = (int)buf.st_size;
						add(fileList_queue,filetemp);
						*merge_size +=filetemp.size;
					}
				}
				pclose(p);
				break;				
		}
		filename[0] = '\0';
		filepath[0] = '\0';
		command[0] = '\0';
	}

	closedir(dp);
}

//입력으로 파일포인터를 받아, 해당 파일의 존재하는 글자와 각 글자의 빈도수를 저장한다.
void determine_frequency(FILE *f) {

	int 	c;
	if(f==NULL){
		perror("FIle Error");
		exit(1);
	}
	while ((c = fgetc(f)) != EOF) {
		if(frequency==NULL){
			perror("frequency NULL");
			exit(1);
		}
		++frequency[c];
		++original_size;
	}

	for (c = 0; c < num_alphabets; ++c)
		if (frequency[c] > 0)
			++num_active;
}

//허프만 알고리즘을 위한 초기화 함수
void init() {

	num_alphabets = 256;
	num_active = 0;
	frequency = NULL;
	original_size = 0;
	nodes = NULL;
	num_nodes = 0;
	leaf_index = NULL;
	parent_index = NULL;
	free_index = 1;
	stack_top = 0;

	bits_in_buffer = 0;
	current_bit = 0;
	stack = NULL;
	eof_input = 0;
	frequency = (int *) calloc(2*num_alphabets,sizeof(int));
	leaf_index = frequency + num_alphabets - 1;
	memset(buffer, 0, MAX_BUFFER_SIZE);
}

//허프만 트리의 노드를 동적생성
void allocate_tree() {
	nodes = (node_t *) calloc(2 * num_active, sizeof(node_t));
	if(nodes == NULL){
		perror("nodes calloc error");
		exit(1);
	}
	parent_index = (int *)calloc(num_active, sizeof(int));
	if(parent_index == NULL){
		perror("parent_index calloc error");
		exit(1);
	}
}

void finalise() {
	free(parent_index);
	free(frequency);
	free(nodes);
}

/* 허프만 트리에 위치에 맞게 노드를 추가 
	index = 글자의 해당하는 인덱스
	weight = 글자의 인덱스				*/
int add_node(int index, int weight) {
	int i = num_nodes++;
	while (i > 0 && nodes[i].weight > weight) {
		memcpy(&nodes[i + 1], &nodes[i], sizeof(node_t));
		if (nodes[i].index < 0)
			++leaf_index[-nodes[i].index];
		else
			++parent_index[nodes[i].index];
		--i;
	}

	++i;
	nodes[i].index = index;
	nodes[i].weight = weight;
	if (index < 0)
		leaf_index[-index] = i;
	else	
		parent_index[index] = i;

	return i;
}

//존재하는 문자들을 허프만 노드에 추가하는 함수
void add_leaves() {
    int i, freq;
    for (i = 0; i < num_alphabets; ++i) {
        freq = frequency[i];
        if (freq > 0)
            add_node(-(i + 1), freq);
    }
}

//글자의 가중치(빈도수)에 맞게 허프만 트리를 만든다.
void build_tree() {
    int a, b, index;
    while (free_index < num_nodes) {
        a = free_index++;
        b = free_index++;
        index = add_node(b/2,
            nodes[a].weight + nodes[b].weight);
        parent_index[b/2] = index;
    }
}

/*	단일 파일에 대한 압축 알고리즘 
	*node_ptr = 헤더 작성하기 위한 큐의 노드를 반환한다
	*ifile = 압축할 입력 파일의 경로 및 이름
	*ofile = 압축의 출력 파일 경로 및 이름	*/
int encode(file_node* node_ptr,char* ifile, const char *ofile) {

	FILE 	*fin, *fout;

	if ((fin = fopen(ifile, "rb")) == NULL) {
		perror("Failed to open input file");
		return FILE_OPEN_FAIL;
	}
	if ((fout = fopen(ofile, "ab")) == NULL) {
		perror("Failed to open output file");
		fclose(fin);
		return FILE_OPEN_FAIL;
	}

	determine_frequency(fin);
	stack = (int *) calloc(num_active - 1, sizeof(int));	
	if(stack == NULL){
		perror("stack calloc Error");
		exit(1);
	}
	allocate_tree();
	add_leaves();
	write_header(node_ptr,fout,ifile);
	build_tree();
	
	fseek(fin, 0, SEEK_SET);
	
	int c;
	while ((c = fgetc(fin)) != EOF)
		encode_alphabet(fout, c);
	flush_buffer(fout);
	free(stack);
	fclose(fin);
	fclose(fout);
	return 0;
}

/* 허프만 트리를 이용해서 문자를 비트열로 변환한다
	*fout = 출력파일 경로 및 이름
	character = 비트열로 변환 할 문자			*/
void encode_alphabet(FILE *fout, int character) {
	int 	node_index;

	stack_top = 0;
	node_index = leaf_index[character + 1];
	while (node_index < num_nodes) {
		stack[stack_top++] = node_index % 2;
		node_index = parent_index[(node_index + 1) / 2];
	}
	while (--stack_top > -1)
		write_bit(fout, stack[stack_top]);
}

/* 단일 파일에 대한 decompreee 알고리즘
   파일 헤더를 읽고, 허프만 테이블을 읽고 비트열을 문자열로 변환 
   *ifile = 입력 파일
   *odirpath = 출력파일이 생성될 폴더의 경로
   start_pos = 헤더의 위치, defalut = 0		*/
int decode(const char* ifile, char *odirpath,const int start_pos) {

	FILE 	*fin, *fout;
	char 	ofilename[255];
	char 	ofileptr[255];
	char	 mkdir_command[255];
	char 	buff[255];

	if ((fin = fopen(ifile, "rb")) == NULL) {
		perror("Failed to open input file");
		return FILE_OPEN_FAIL;
	}
	
	if (read_header(fin,ofilename,start_pos) == 0) {
		build_tree();

		strcpy(ofileptr,odirpath);
		strcat(ofileptr,"/");		
		strcat(ofileptr,ofilename);

		strcpy(mkdir_command,"mkdir -p ");
		getDirPath(buff,ofileptr);
		strcat(mkdir_command,buff);
		system(mkdir_command);

		if ((fout = fopen(ofileptr, "wb")) == NULL) {
			perror("Failed to open output file");
			fclose(fin);
			return FILE_OPEN_FAIL;
		}

		decode_bit_stream(fin, fout);
	}
	printf("[Decompress] FileName=%s\n", ofilename);
	fclose(fin);
	fclose(fout);

    return 0;
}

/* 압축된 파일의 비트열을 읽어와 허프만 테이블을 이용하여 문자로 변환하여 출력
	fin = 입력 파일
	fout = 출력 파일 */
void decode_bit_stream(FILE *fin, FILE *fout) {

	int i = 0, bit, node_index = nodes[num_nodes].index;
	while (1) {
		bit = read_bit(fin);
		if (bit == -1)
			break;
		node_index = nodes[node_index * 2 - bit].index;
		if (node_index < 0) {
			char c = -node_index - 1;
			fwrite(&c, 1, 1, fout);
			if (++i == original_size){
				break;
			}
			node_index = nodes[num_nodes].index;
		}
   	 }
}

/* int 형 bit을 바이너리로 변환하여 버퍼에 저장한다. 
	bit = 변환할 허프만 코드(바이너리)의 int형 */
int write_bit(FILE *f, int bit) {

    if (bits_in_buffer == MAX_BUFFER_SIZE << 3) {
        size_t bytes_written = fwrite(buffer, 1, MAX_BUFFER_SIZE, f);
        if (bytes_written < MAX_BUFFER_SIZE && ferror(f))
            return INVALID_BIT_WRITE;
        bits_in_buffer = 0;
        memset(buffer, 0, MAX_BUFFER_SIZE);
    }
    if (bit){
        buffer[bits_in_buffer >> 3] |=
            (0x1 << (7 - bits_in_buffer % 8));
	}
    ++bits_in_buffer;
    return SUCCESS;
}

/* 바이너리가 기록된 버퍼를 출력파일 f에 쓰기한다*/
int flush_buffer(FILE *f) {
	if (bits_in_buffer) {
		size_t bytes_written =
		fwrite(buffer, 1,(bits_in_buffer + 7) >> 3, f);
		if (bytes_written < MAX_BUFFER_SIZE && ferror(f))
			return -1;
		bits_in_buffer = 0;
	}
	return 0;
}

/* 입력파일 f로부터 비트열(허프만코드)을 읽어 버퍼에 저장한다.*/
int read_bit(FILE *f) {
	if (current_bit == bits_in_buffer) {
		if (eof_input)
			return END_OF_FILE;
		else {
			size_t bytes_read =
			fread(buffer, 1, MAX_BUFFER_SIZE, f);
			if (bytes_read < MAX_BUFFER_SIZE) {
				if (feof(f))
					eof_input = 1;
			}
			bits_in_buffer = bytes_read << 3;
			current_bit = 0;
		}
	}

	if (bits_in_buffer == 0)
		return END_OF_FILE;
	int bit = (buffer[current_bit >> 3] >> (7 - current_bit % 8)) & 0x1;
	++current_bit;
	return bit;
}

/* 단일파일의 헤더를 작성한다. (헤더위치, 파일의 사이즈, 경로 및 이름, 존재하는 문자의 수, 허프만 테이블)
	*node_ptr = 큐에 저장할 파일의 정보를 담는 구조체
	*f = 목적파일
	*ifile = 입력파일 */
int write_header(file_node* node_ptr,FILE *f, char* ifile) {

	
	int 				i, blocksize_byte, byte = 0,
						size =  sizeof(unsigned int) + 1 + num_active * (1 + sizeof(int));
	unsigned int 		weight;
	char 				buffer[BUFSIZ];	
	//char *buffer = (char *) calloc(size, 1);
	file_s temp;
	
	//update header_pos 
	fseek(f,0,SEEK_END);
	fseek(f,0,SEEK_END);
	node_ptr->data.header_pos = ftell(f);
	
	//save original file size byte convert to binary	
	blocksize_byte = sizeof(int);
	while (blocksize_byte--){
		buffer[byte++] = (original_size >> (blocksize_byte << 3)) & 0xff;
	}

	//save original file name n byte
	for(i=0;ifile[i]!='\0';i++){
		buffer[byte++] = ifile[i];
	}
	buffer[byte++] = '\0';
	
	size += (i+1); //i+1 = ifile_name_size
	//buffer = (char *) realloc(buffer,size);

	//exist character count 1byte
	buffer[byte++] = (char) num_active;

	for (i = 1; i <= num_active; ++i) {
		weight = nodes[i].weight;

		//character index 1byte
		buffer[byte++] = (char) (-nodes[i].index - 1);
		blocksize_byte = sizeof(int);

		//character freq size(int) byte
		while (blocksize_byte--)
			buffer[byte++] = (weight >> (blocksize_byte << 3)) & 0xff;

	}

	//write fout from buffer
	fwrite(buffer, 1, size, f);
	//free(buffer);
	printf("[Compresss] FileName=%s, size=%d bytes\n",
		node_ptr->data.pathName, node_ptr->data.size);
	return 0;
}

/* 단일 파일의 헤더정보를 읽는다.
	*f = 입력파일	
	*ofile = 출력파일
	start_pos = 헤더 위치 default = 0 */
int read_header(FILE *f,char *ofile,const int start_pos) {
	int 		i, j, byte = 0, size;
	size_t		bytes_read;
	unsigned char	buff[4];

	fseek(f,start_pos,SEEK_SET);

	//read original file's byte
	bytes_read = fread(&buff, 1, sizeof(int), f);
	if (bytes_read < 1)
		return END_OF_FILE;
	byte = 0;
	original_size = buff[byte++];
	while (byte < sizeof(int))
		original_size =(original_size << (1 << 3)) | buff[byte++];

	//read original file's path
	fread(&ofile[0],1,1,f);
	byte++;
	for(i=0;ofile[i]!='\0';){
		fread(&ofile[++i],1,1,f);	
		byte++;
	}
	ofile[i] = '\0';
	byte++;
	//read exist number count	
	bytes_read = fread(&num_active, 1, 1, f);
	if (bytes_read < 1)
		return END_OF_FILE;

	//allocate_tree
	allocate_tree();
	
	//read huffman table
	size = num_active * (1 + sizeof(int));
	unsigned int weight;
	char *buffer = (char *) calloc(size, 1);
	if (buffer == NULL)
		return MEM_ALLOC_FAIL;
	fread(buffer, 1, size, f);
	byte = 0;
	for (i = 1; i <= num_active; ++i) {
		nodes[i].index = -(buffer[byte++] + 1);
		j = 0;
		weight = (unsigned char) buffer[byte++];
		while (++j < sizeof(int)) {
			weight = (weight << (1 << 3)) | (unsigned char) buffer[byte++];
		}
		nodes[i].weight = weight;
	}

	num_nodes = (int) num_active;
	free(buffer);
	return 0;
}

/*	압축을 할 때 압축된 파일의 끝에 메타데이터(headerlist_ptr이 가르키고 있는 정보들)를 삽입한다. 
	메타데이터는 각 파일의 헤더정보를 가르킨다.
	headerlist_ptr = 압축파일의 정보를 담고있는 큐 헤더
	ofile = 목적파일	*/
void write_meta(Queue* headerlist_ptr, const char *ofile){

	int 		blocksize_byte, i=0,byte=0,file_cnt=0;
	long int	meta_pos;	
	char 		buffer[BUFSIZ];
	FILE 		*f;
	int 		filesize;

	if ((f = fopen(ofile, "ab")) == NULL) {
		perror("Failed to open output file");
		fclose(f);
		return ;
	}

	file_node* curptr = headerlist_ptr->front;
	fseek(f,0,SEEK_END);
	meta_pos = ftell(f);

	//write File #N Header
	while(curptr!=NULL){

		//write File #N Header position, size(int) byte
		blocksize_byte = sizeof(curptr->data.header_pos);
		while (blocksize_byte--)
			buffer[byte++] = (curptr->data.header_pos >> (blocksize_byte << 3)) & 0xff;

		//write original file name n byte
		for(i=0;curptr->data.pathName[i]!='\0';i++){
			buffer[byte++] = curptr->data.pathName[i];
		}
		buffer[byte++] = '\0';

		//write data_size  (sizeof(data_size)) byte
		blocksize_byte = sizeof(curptr->data.size);
		while (blocksize_byte--)
			buffer[byte++] = (curptr->data.size >> (blocksize_byte << 3)) & 0xff;

		curptr = curptr->next;
		file_cnt++;	
	}

	//write meta_pos, sizeof(int) byte
	blocksize_byte = sizeof(meta_pos);
	while (blocksize_byte--){
		buffer[byte++] = (meta_pos >> (blocksize_byte << 3)) & 0xff;
	}

	fwrite(buffer, 1, byte, f);
	fclose(f);
	
	return ;
}
/* 메타 데이터를 읽는다. 파일의 맨마지막 4바이트는 메타 데이터가 시작하는 위치를 나타내므로 그것을 읽고
 그 위치를 이동하여 각 헤더파일의 정보를 headerlist_ptr에 저장한다.
	headerlist_ptr = 헤더파일의 정보를 담을 큐의 헤더
	*meta = 메타 데이터를 저장할 구조체 
	ifile = 입력 파일	*/
void read_meta(Queue* headerlist_ptr,metadata* meta,const char *ifile){

	int		i, j, size, byte = 0;
	long int 	meta_pos,meta_end_pos;
	size_t		bytes_read;
	unsigned char 	buff[BUFSIZ];
	FILE 		*f;
	file_s 		filetemp;

	if ((f = fopen(ifile, "rb")) == NULL) {
		perror("Failed to open input file");
		return ;
	}

	//1.Read Meta_pos
	fseek(f,-4,SEEK_END);
	meta_end_pos=ftell(f);
	fread(&buff, 1, sizeof(int), f);
	byte = 0;
	meta_pos = buff[byte++];
	while (byte < sizeof(int))
		meta_pos =(meta_pos << (1 << 3)) | buff[byte++];
	
	//Set fseek Mate_pos
	fseek(f,meta_pos,SEEK_SET);

	//2.Read Meta_File #N Header
	file_node* curptr = headerlist_ptr->front;
	meta->count = 0;
	while((ftell(f)) < meta_end_pos){

		//2.1 Read File #N Header, position, size(int) byte
		fread(&buff, 1, sizeof(int), f);
		byte = 0;
		filetemp.header_pos = buff[byte++];
		while (byte < sizeof(int))
			filetemp.header_pos =(filetemp.header_pos << (1 << 3)) | buff[byte++];
						
					
		//2.2 Read File #N Header, Original File Name, size n byte
		fread(&buff[0],1,1,f);
		for(i=0;buff[i]!='\0';){
			fread(&buff[++i],1,1,f);	
		}
		buff[i] = '\0';
		strcpy(filetemp.pathName,buff);

		//2.3 Read File #N Header, File_size, size(int) byte
		fread(&buff, 1, sizeof(int), f);
		byte = 0;
		filetemp.size = buff[byte++];
		while (byte < sizeof(int))
			filetemp.size =(filetemp.size << (1 << 3)) | buff[byte++];
		
		//2.4 Add FileList_queue, Update *Metadata from Read_Meta
		add(headerlist_ptr,filetemp);
		strcpy(meta->pathName[meta->count],filetemp.pathName);
		meta->header_pos[meta->count] = filetemp.header_pos;
		meta->size[meta->count] = filetemp.size;
		meta->count++;

	}



	return ;
}
/* filelist_ptr이 가르키는 파일들을 merge하여 encoding 한다. 
	filelist_ptr = 헤더정보를 가르키고 있는 큐의 헤더
	ofile = 출력 파일	*/
void merge_compress(Queue* filelist_prt, const char *ofile){

	file_node*	 curptr;

	curptr = filelist_prt->front;
	while(curptr!=NULL){
		init();
		encode(curptr,curptr->data.pathName,ofile);
		finalise();
		curptr = curptr->next;
	}
}

/* filelist_ptr이 가르키는 헤더를 이용하여 각 파일을 decoding 한다*/
void merge_decompress(Queue* filelist_prt,const char* ifile, char *ofile){
	
	file_node* 	curptr;

	curptr = filelist_prt->front;
	while(curptr!=NULL){
		init();
		decode(ifile, ofile,curptr->data.header_pos);
		finalise();
		curptr = curptr->next;
	}
}

// 로컬 컴퓨터의 논리적인 코어의 개수를 반환한다 
int get_cores(){

	FILE 	*p;
	char 	output[20];
	char 	*c;

	p = popen("grep 'cpu cores' /proc/cpuinfo","r");
	if(p!=NULL){
		while(fgets(output,sizeof(output),p)!=NULL);
		printf("%s\n",output);
		c = strtok(output,":");
		c = strtok(NULL, " ");
	}
	
	pclose(p);
	return atoi(c);
}

void print_help() {
	fprintf(stderr,
		"USAGE: ./huf [-e | -ep | -d - dp - v] "
		"<input out> <output file>\n");
}

void handler(int signo){
	//SIGUSR1 ignore
}	

/* 멀티 프로세스 압축 시 자식프로세스의 처리
   1. 자식프로세스가 작업을 분배한 것을 메시지큐로 읽어온다. (파일의 인덱스 번호가 넘어온다) 
   2. 자식 프로세스는 메타데이터만을 읽어 자신이 할당 받은 파일의 인덱스를 compress 하고 임시파일로 생성한다
   3. 자식 프로세스가 할당받은 작업을 완료하면 자신이 생성한 임시파일의 이름을 부모프로세스에게 큐로 보낸다
   4. 자식 프로세스 종료 */
void compress_child(Queue* header_ptr,int index,metadata *meta, int argc, char **argv){

	//msgqueue variable
	int 		rcv_msgid;
	int 		snd_msgid;
	int 		len;
	int 		n;
	char 		buf[255];
	msgblock 	msg;
	void 		(*hand)(int);
	key_t		key;

	file_node* cur = header_ptr->front;

	hand = signal(SIGINT,handler);
	
	//부모프로세스에게 작업을 할당받을 때까지 대기
	pause();
	
	//할당을 받으면 MsgQ를 얻어옴
	key = ftok("./huf.c",1);
	rcv_msgid = msgget(key,0);
	if(rcv_msgid==-1){
		perror("msgget Error");
		exit(1);
	}
	
	key = ftok("./huf",1);
	snd_msgid = msgget(key, IPC_CREAT|0644);

	if(snd_msgid==-1){
		perror("snd_msgget Error");
		exit(1);
	}
	
	//할당받은 파일을 모두 encoding
	while(1){
		msgrcv(rcv_msgid,&msg,sizeof(msgblock),index+1,0);
		if(strcmp(msg.mtext,"-1") ==0) break;
	
		n = atoi(msg.mtext);
		
		int file_index=0;
		while(cur!=NULL){
			
			if(file_index == n){
				
				//임시파일로 입력파일을 encoding
				strcpy(buf,cur->data.pathName);
				strcat(buf,".huf");
				init();
				printf("Process[%d],",index);
				encode(cur,cur->data.pathName, buf);
				finalise();
				cur = cur->next;
				file_index++;

				//부모 프로세스에게 받은 파일 인덱스를 타입, 임시파일명을 내용으로 메시지큐를 전송
				msg.mtype = file_index++;
				strcpy(msg.mtext, buf);

				if(msgsnd(snd_msgid,(void*)&msg,sizeof(msgblock),IPC_NOWAIT) == -1){
					perror("msgsnd");
					exit(1);
				}
				break;
			}
			cur = cur->next;
			file_index++;
		}
		cur = header_ptr->front;

	}	

	exit(1);

}

/* 멀티 프로세스 압축 시 부모 프로세스의 처리
   자식 프로세스로 부터 엔코딩된 임시파일명을 메시지 큐로부터 읽어와 이를 merge한다*/
void compress_parent(Queue* header_ptr,int index,metadata *meta, int argc, char **argv){

	//msgqueue variable
	FILE 		*fout,*fin;
	int 		rcv_msgid;
	int 		n;
	char 		*ifile;
	char 		buf[BUFSIZ];
	int 		file_index=0;
	msgblock 	msg;
	key_t 		key;
	file_node* 	cur = header_ptr->front;

	key = ftok("./huf",1);
	rcv_msgid = msgget(key,0);
	if(rcv_msgid==-1){
		perror("msgget Error");
		exit(1);
	}

	
	while(cur!=NULL){
			
		//Recieve Message Queue		
		msgrcv(rcv_msgid,&msg,sizeof(msgblock),file_index+1,0);

		//Merge to divide file
		if ((fin = fopen(msg.mtext, "rb")) == NULL) {
			perror("Failed to open input file");
			return ;
		}

		if ((fout = fopen(argv[argc-1], "ab")) == NULL) {
			perror("Failed to open output file");
			fclose(fin);
			return ;
		}
		fseek(fout,0,SEEK_END);
		fseek(fout,0,SEEK_END);
		cur->data.header_pos = ftell(fout);

		while((n=fread(buf,1,1,fin)) > 0){				
			if(fwrite(buf,1,1,fout) != n) break;
		}
		fclose(fin);
		fclose(fout);
		

		//count increse
		file_index++;
		cur= cur->next;
		strcpy(buf,"rm -r ");
		//getDirPathParent(result,msg.mtext);
		strcat(buf,msg.mtext);
		system(buf);
	}
	
	write_meta(header_ptr,argv[argc-1]);	
	
}

/* 멀티프로세스 압축 해제 시 자식 프로세스의 처리
	메타 데이터를 읽고, 부모 프로세스가 메시지 큐로 보낸 파일의 인덱스를 얻어와
	할당된 파일을 decoding하고 종료			*/
void decompress_child(int index,metadata *meta, int argc, char **argv){
	
	//msgqueue variable
	int 		msgid;
	int 		len;
	int 		n;
	char 		buf[10];
	msgblock	msg;
	void 		(*hand)(int);
	key_t		key;

	hand = signal(SIGINT,handler);

	pause();

	key = ftok("./huf.c",1);
	msgid = msgget(key,0);
	if(msgid==-1){
		perror("msgget Error");
		exit(1);
	}
		
	
	while(1){
		//Received Work allocation (File Index)
		msgrcv(msgid,&msg,sizeof(msgblock),index+1,0);
		if(strcmp(msg.mtext,"-1") ==0) break;
		n = atoi(msg.mtext);
		printf("Process[%d],",index);
		
		init();
		decode(argv[2], argv[3],meta->header_pos[n]);
		finalise();
	}	

	exit(1);
}

/* 부모 프로세스가 작업을 자식 프로세스에게 분배하고 메시지 큐로 전송 
	작업의 할당은 파일의 사이즈로 가장 적게 할당 받은 자식 프로세스에게 순차적으로 할당한다. (탐욕적인 방법) */
void parent_divide(Queue* header_ptr,pid_t* childs_pid,int n){
	
	//acllocation variable
	workspace	child[n];
	file_node *cur= header_ptr->front;;	
	int 		size=0;
	int 		smallest_size=0;
	int		smallest_index=0;

	//msgqueue variable
	int 		msgid;
	msgblock	msg;
	key_t 		key;
	char 		buf[10];

	//SIGNAL
	void (*hand)(int);
	hand = signal(SIGINT,handler);

	//1-1.initialize work_size at 0
	for(int i=0;i<n;i++){
		child[i].size = 0;
		child[i].count = 0;
	}
	

	//1.2allocation work to child process
	int fileindex = 0;
	while(cur!=NULL){
		size = cur->data.size;
		smallest_size = child[0].size;
		smallest_index = 0;
	
		for(int i=1;i<n;i++){
			if(child[i].size <= smallest_size){
				smallest_size = child[i].size;
				smallest_index = i;
			}					
		}
		child[smallest_index].file_index[child[smallest_index].count++]
			= fileindex;
		child[smallest_index].size += size;	
		cur = cur->next;
		fileindex++;
	}

	//2.1 Message Queue Creat
	key = ftok("./huf.c",1);
	msgid = msgget(key, IPC_CREAT|0644);
	if(msgid==-1){
		perror("msgget Error");
		exit(1);
	}
	
	//2.2 Message Queue Send
	for(int i=0;i<n;i++){
		msg.mtype = i+1; //process Index
		for(int j=0;j<child[i].count;j++){
			sprintf(buf,"%d",child[i].file_index[j]);
			strcpy(msg.mtext,buf);
			
			if(msgsnd(msgid,(void*)&msg,sizeof(msgblock),IPC_NOWAIT) == -1){
				perror("msgsnd");
				exit(1);
			}
		}
		
		//send EOF
		strcpy(msg.mtext,"-1");
		if(msgsnd(msgid,(void*)&msg,sizeof(msgblock),IPC_NOWAIT) == -1){
			perror("msgsnd");
			exit(1);
		}	
	}

	//3. Send SIGNAL
	sleep(1);
	for(int i=0;i<n;i++){
		kill((int)childs_pid[i],SIGINT);
	}
}

int main(int argc, char **argv) {
	
	metadata		meta;								//메타데이터를 저장하는 구조체
	Queue* 			headerlist_ptr=createQueue();		//헤더리스트를 저장하는 큐(연결리스트)
	struct stat 	file_info;							//압축된 파일의 사이즈를 읽기 위한 변수	
	int 			forkProcessCount = 0;				//자식프로세스가 생성된 갯수
	int				cores = 4;							//로컬컴퓨터의 코어의 개수
	int				merge_org_size=0;					//압축하기전의 파일 사이즈의 합
	int 			merge_com_size=0;					//압축 후의 파일사이즈
	double			compressibility = 0.;				//압축률 

	if (argc < 2) {
		print_help();
	}
	
	//get_cores() 함수는 컴퓨터의 논리적인 코어의 개수를 반환한다.
	//하지만 VMware의 설정값 때문에 코어의 수가 1로 설정되어, 4로 가정한다.
	//Next Line Command is Local Computer's get CPU cores //
	//cores = get_cores();

	pid_t pids[cores], pid;
	
	//1. encode Single process //
	if (strcmp(argv[1], "-e") == 0){
		
		//사용자의 입력을 분석하여 Compress할 파일들을 종합하여 저장한다. (headerlist_ptr)
		merge_filelist(headerlist_ptr,&merge_org_size,argc,argv);

		//종합된 파일을 병합압축한다.
		merge_compress(headerlist_ptr,argv[argc-1]);

		//병합압축된 파일에 끝에 메타 데이터를 삽입한다. -- 압축 끝 --
		write_meta(headerlist_ptr,argv[argc-1]);

		//상태확인 및 출력
		stat(argv[argc-1],&file_info);
		merge_com_size = file_info.st_size;
		compressibility = (((float)merge_org_size-merge_com_size)
				/ (float)merge_org_size) * 100;

		printf("Compress finish!!\n");
		printf("Compress File Count = %d\n",headerlist_ptr->count);
		printf("Original Files size = %d bytes  ",merge_org_size);
		printf("Compress Files size = %d bytes\n",merge_com_size);
		printf("Compressibility = %lf%%\n",compressibility);
	
	//2. decode Single process //
	}else if (strcmp(argv[1], "-d") == 0){
		
		//압축된 파일의 메타데이터를 읽고 headerlist_ptr에 저장한다.
		read_meta(headerlist_ptr,&meta,argv[2]);

		//headerList_ptr을 바탕으로 decompress 한다
		merge_decompress(headerlist_ptr,argv[2],argv[3]);

		printf("Decompress finish!!\n");
		printf("Decompress File Count = %d\n",meta.count);

	//3. encode parallel process //
	}else if (strcmp(argv[1], "-ep") == 0) {
		
		//사용자의 입력을 분석하여 Compress할 파일들을 종합하여 저장한다. (headerlist_ptr)
		merge_filelist(headerlist_ptr,&merge_org_size,argc,argv);

		//자식 프로세스를 코어의 개수만큼 fork()한다.
		while(forkProcessCount < cores){
			pids[forkProcessCount] = fork();
			if(pids[forkProcessCount] < 0){
				perror("fork error");
				exit(1);
			}else if(pids[forkProcessCount] == 0){ //child_process
				compress_child(
					headerlist_ptr,forkProcessCount,&meta,argc,argv);
				exit(1);
			}else { // parent process

			}
			forkProcessCount++;
		}
		
		//자식 프로세스에게 작업을 할당한다.
		parent_divide(headerlist_ptr,pids,cores);

		//자식 프로세스가 자신의 할당된 작업을 다 끝날때까지 기다린다.
		while((wait(NULL))>0);

		//자식 프로세스가 할일이 끝나면 부모프로세스는 그것을 종합하여 Compress한다.
		compress_parent(headerlist_ptr,forkProcessCount,&meta,argc,argv);	
	
		//Evaluate Compress File Size & rate	
		stat(argv[argc-1],&file_info);
		merge_com_size = file_info.st_size;
		compressibility = (((float)merge_org_size-merge_com_size)
				/ (float)merge_org_size) * 100;
		printf("Compress finish!!\n");
		printf("Compress File Count = %d\n",headerlist_ptr->count);
		printf("Original Files size = %d bytes  ",merge_org_size);
		printf("Compress Files size = %d bytes\n",merge_com_size);
		printf("Compressibility = %lf%%\n",compressibility);
	
	//4. decode parallel process //
	}else if (strcmp(argv[1], "-dp") == 0) {

		//압축된 파일의 메타데이터를 읽고 headerlist_ptr에 저장한다.
		read_meta(headerlist_ptr,&meta,argv[2]);
	
		//코어의 개수만큼 자식 프로세스 fork()
		while(forkProcessCount < cores){
			pids[forkProcessCount] = fork();
			if(pids[forkProcessCount] < 0){
				perror("fork error");
				exit(1);
			}else if(pids[forkProcessCount] == 0){ 	//child_process
				decompress_child(forkProcessCount,&meta,argc,argv);
				exit(1);
			}else { 				//parent process

			}
			forkProcessCount++;
		}	

		//Parent Process allocation to Child Process	
		parent_divide(headerlist_ptr,pids,cores);	
		
		//Waiting.. While Child Process Work finish 
		while((wait(NULL))>0);
		
		printf("Decompress finish!!\n");
		printf("Decompress File Count = %d\n",meta.count);
	
	// 5. View Mode //
	}else if(strcmp(argv[1],"-v")==0){

		read_meta(headerlist_ptr,&meta,argv[2]);
		printf("%5s %25s %15s %10s\n","Index","FileName","Byte","Header");	
		for(int i=0; i<meta.count;i++){
			printf("%5d %25s %15d %10ld\n",
				i,meta.pathName[i],meta.size[i],meta.header_pos[i]);	
		}
	
	// 6. Decoding part of Compress File // 
	}else if(strcmp(argv[1],"-decode") == 0 ){
		read_meta(headerlist_ptr,&meta,argv[2]);
		
		int index,n;		
		n = atoi(argv[4]);

		init();		
		decode(argv[2], argv[3],meta.header_pos[n]);
		finalise();	
		printf("Decompress finish!!\n");
	}else
		print_help();
	
	return SUCCESS;
}
