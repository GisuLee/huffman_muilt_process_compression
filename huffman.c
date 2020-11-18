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
} msgblock;						//��Ƽ���μ����� �� ��, �޽���ť�� ���� ����ü

typedef struct {
	int count;
	int size;
	int file_index[1000];
	char pathName[1000][255];
} workspace;					//�ڽ� ���μ������� �Ҵ���� �۾��� �����ϴ� ����ü

typedef struct file_s{
	long int header_pos;
	char pathName[255];			//file's path & name
	unsigned int size;			//byte
} file_s;						//������ �����ġ, ��� �� �̸�, ������ ����

typedef struct file_node{
	file_s data;
	struct file_node* next;
}file_node;						//���ϱ���ü�� ���

typedef struct Queue{
	file_node *front;
	file_node *rear;
	int count;				
}Queue;							//���ϱ���ü�� ���Ḯ��Ʈ

typedef struct {
	int index;
	unsigned int weight;
} node_t;						//������ ���̺��� �����ϴ� ����ü

typedef struct {
	int count;
	char pathName[1000][255];
	long int header_pos[1000];
	int size[1000];
}metadata;						//��Ÿ�����͸� �����ϴ� ����ü

//Global variable
int num_alphabets = 256;		//�ƽ�Ű �ڵ尡 ǥ���� �� �ִ� ������ ����
int num_active = 0;				//���� ������ �����ϴ� ������ ����
int *frequency = NULL;			//�����ϴ� ���ڿ� ���� �󵵼��� �����ϴ� �迭(�����Ҵ�)
unsigned int original_size = 0;	//������ �� ������
node_t *nodes = NULL;			//������ Ʈ���� ����� ���� �ֻ��� ���
int num_nodes = 0;				//������ Ʈ���� ����� ����
int *leaf_index = NULL;			//�ڽ� ���
int *parent_index = NULL;		//�θ� ���
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

/* ť�� �����Ѵ�. */
Queue* createQueue(){
	Queue* newQueue = (Queue*)malloc(sizeof(Queue));
	newQueue->count = 0;
	newQueue->front=NULL;
	newQueue->rear=NULL;
	return newQueue;
}

/*	ť�� ��带 �����Ѵ�.
	Queue* qptr = ť�� ��� ������ 
	file_s data = ť�� �߰��� ��� ������ */
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

/* ���ڿ� *p�� ����� ���Ѵ� */
int get_strsize(const char *p){
	int i;
	for(i=0;p[i]!='\0';i++);
	return i;
}

//	� ������ ��� �� �̸��� ���� �Է¿��� ��θ� ��ȯ�Ѵ�.
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

//	� ������ ��� �� �̸��� ���� �Է¿��� �ֻ��� �θ���丮��θ� ��ȯ�Ѵ�.
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

/*	����ڿ� �Է�(���� �� �����̸��� ����Ʈ)�� �޾� ���̿켱Ž������ ASCKII ���ϵ鸸 �����Ͽ� Queue�� �����Ѵ�. 
	fileList_queue =  ť�� ��� ������
	merge_size = ���ϸ���Ʈ�� �������� ���� �������� �� ��ȯ 
	**argv = ����� �Է� ���ڿ� ������ */
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
			
			//������ ������ ������ ���, search_dir ȣ��
			case S_IFDIR:
				if(filename[get_strsize(filename)-1] != '/'){
					strcpy(nextpath,filename);
					strcat(nextpath,"/");				
				}
				search_dir(nextpath,merge_size,fileList_queue);			
				nextpath[0] = '\0';
				break;

			//������ ������ Regular�� ���, ASKII �������� �˻��ϰ�, ���ϸ���Ʈ�� ����
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

/*	������ ��θ� �Է¹޾�, �������� ASCKII ������ �����ϰ�, ������ �����ϸ� ���ȣ�� 
	path = �˻��� ����
	*merge_size =  ���ϸ���Ʈ�� ������ ���� ��ȯ
	*fileList_queue = ť�� ��� ������ */
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

//�Է����� ���������͸� �޾�, �ش� ������ �����ϴ� ���ڿ� �� ������ �󵵼��� �����Ѵ�.
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

//������ �˰����� ���� �ʱ�ȭ �Լ�
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

//������ Ʈ���� ��带 ��������
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

/* ������ Ʈ���� ��ġ�� �°� ��带 �߰� 
	index = ������ �ش��ϴ� �ε���
	weight = ������ �ε���				*/
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

//�����ϴ� ���ڵ��� ������ ��忡 �߰��ϴ� �Լ�
void add_leaves() {
    int i, freq;
    for (i = 0; i < num_alphabets; ++i) {
        freq = frequency[i];
        if (freq > 0)
            add_node(-(i + 1), freq);
    }
}

//������ ����ġ(�󵵼�)�� �°� ������ Ʈ���� �����.
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

/*	���� ���Ͽ� ���� ���� �˰��� 
	*node_ptr = ��� �ۼ��ϱ� ���� ť�� ��带 ��ȯ�Ѵ�
	*ifile = ������ �Է� ������ ��� �� �̸�
	*ofile = ������ ��� ���� ��� �� �̸�	*/
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

/* ������ Ʈ���� �̿��ؼ� ���ڸ� ��Ʈ���� ��ȯ�Ѵ�
	*fout = ������� ��� �� �̸�
	character = ��Ʈ���� ��ȯ �� ����			*/
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

/* ���� ���Ͽ� ���� decompreee �˰���
   ���� ����� �а�, ������ ���̺��� �а� ��Ʈ���� ���ڿ��� ��ȯ 
   *ifile = �Է� ����
   *odirpath = ��������� ������ ������ ���
   start_pos = ����� ��ġ, defalut = 0		*/
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

/* ����� ������ ��Ʈ���� �о�� ������ ���̺��� �̿��Ͽ� ���ڷ� ��ȯ�Ͽ� ���
	fin = �Է� ����
	fout = ��� ���� */
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

/* int �� bit�� ���̳ʸ��� ��ȯ�Ͽ� ���ۿ� �����Ѵ�. 
	bit = ��ȯ�� ������ �ڵ�(���̳ʸ�)�� int�� */
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

/* ���̳ʸ��� ��ϵ� ���۸� ������� f�� �����Ѵ�*/
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

/* �Է����� f�κ��� ��Ʈ��(�������ڵ�)�� �о� ���ۿ� �����Ѵ�.*/
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

/* ���������� ����� �ۼ��Ѵ�. (�����ġ, ������ ������, ��� �� �̸�, �����ϴ� ������ ��, ������ ���̺�)
	*node_ptr = ť�� ������ ������ ������ ��� ����ü
	*f = ��������
	*ifile = �Է����� */
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

/* ���� ������ ��������� �д´�.
	*f = �Է�����	
	*ofile = �������
	start_pos = ��� ��ġ default = 0 */
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

/*	������ �� �� ����� ������ ���� ��Ÿ������(headerlist_ptr�� ����Ű�� �ִ� ������)�� �����Ѵ�. 
	��Ÿ�����ʹ� �� ������ ��������� ����Ų��.
	headerlist_ptr = ���������� ������ ����ִ� ť ���
	ofile = ��������	*/
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
/* ��Ÿ �����͸� �д´�. ������ �Ǹ����� 4����Ʈ�� ��Ÿ �����Ͱ� �����ϴ� ��ġ�� ��Ÿ���Ƿ� �װ��� �а�
 �� ��ġ�� �̵��Ͽ� �� ��������� ������ headerlist_ptr�� �����Ѵ�.
	headerlist_ptr = ��������� ������ ���� ť�� ���
	*meta = ��Ÿ �����͸� ������ ����ü 
	ifile = �Է� ����	*/
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
/* filelist_ptr�� ����Ű�� ���ϵ��� merge�Ͽ� encoding �Ѵ�. 
	filelist_ptr = ��������� ����Ű�� �ִ� ť�� ���
	ofile = ��� ����	*/
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

/* filelist_ptr�� ����Ű�� ����� �̿��Ͽ� �� ������ decoding �Ѵ�*/
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

// ���� ��ǻ���� ������ �ھ��� ������ ��ȯ�Ѵ� 
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

/* ��Ƽ ���μ��� ���� �� �ڽ����μ����� ó��
   1. �ڽ����μ����� �۾��� �й��� ���� �޽���ť�� �о�´�. (������ �ε��� ��ȣ�� �Ѿ�´�) 
   2. �ڽ� ���μ����� ��Ÿ�����͸��� �о� �ڽ��� �Ҵ� ���� ������ �ε����� compress �ϰ� �ӽ����Ϸ� �����Ѵ�
   3. �ڽ� ���μ����� �Ҵ���� �۾��� �Ϸ��ϸ� �ڽ��� ������ �ӽ������� �̸��� �θ����μ������� ť�� ������
   4. �ڽ� ���μ��� ���� */
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
	
	//�θ����μ������� �۾��� �Ҵ���� ������ ���
	pause();
	
	//�Ҵ��� ������ MsgQ�� ����
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
	
	//�Ҵ���� ������ ��� encoding
	while(1){
		msgrcv(rcv_msgid,&msg,sizeof(msgblock),index+1,0);
		if(strcmp(msg.mtext,"-1") ==0) break;
	
		n = atoi(msg.mtext);
		
		int file_index=0;
		while(cur!=NULL){
			
			if(file_index == n){
				
				//�ӽ����Ϸ� �Է������� encoding
				strcpy(buf,cur->data.pathName);
				strcat(buf,".huf");
				init();
				printf("Process[%d],",index);
				encode(cur,cur->data.pathName, buf);
				finalise();
				cur = cur->next;
				file_index++;

				//�θ� ���μ������� ���� ���� �ε����� Ÿ��, �ӽ����ϸ��� �������� �޽���ť�� ����
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

/* ��Ƽ ���μ��� ���� �� �θ� ���μ����� ó��
   �ڽ� ���μ����� ���� ���ڵ��� �ӽ����ϸ��� �޽��� ť�κ��� �о�� �̸� merge�Ѵ�*/
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

/* ��Ƽ���μ��� ���� ���� �� �ڽ� ���μ����� ó��
	��Ÿ �����͸� �а�, �θ� ���μ����� �޽��� ť�� ���� ������ �ε����� ����
	�Ҵ�� ������ decoding�ϰ� ����			*/
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

/* �θ� ���μ����� �۾��� �ڽ� ���μ������� �й��ϰ� �޽��� ť�� ���� 
	�۾��� �Ҵ��� ������ ������� ���� ���� �Ҵ� ���� �ڽ� ���μ������� ���������� �Ҵ��Ѵ�. (Ž������ ���) */
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
	
	metadata		meta;								//��Ÿ�����͸� �����ϴ� ����ü
	Queue* 			headerlist_ptr=createQueue();		//�������Ʈ�� �����ϴ� ť(���Ḯ��Ʈ)
	struct stat 	file_info;							//����� ������ ����� �б� ���� ����	
	int 			forkProcessCount = 0;				//�ڽ����μ����� ������ ����
	int				cores = 4;							//������ǻ���� �ھ��� ����
	int				merge_org_size=0;					//�����ϱ����� ���� �������� ��
	int 			merge_com_size=0;					//���� ���� ���ϻ�����
	double			compressibility = 0.;				//����� 

	if (argc < 2) {
		print_help();
	}
	
	//get_cores() �Լ��� ��ǻ���� ������ �ھ��� ������ ��ȯ�Ѵ�.
	//������ VMware�� ������ ������ �ھ��� ���� 1�� �����Ǿ�, 4�� �����Ѵ�.
	//Next Line Command is Local Computer's get CPU cores //
	//cores = get_cores();

	pid_t pids[cores], pid;
	
	//1. encode Single process //
	if (strcmp(argv[1], "-e") == 0){
		
		//������� �Է��� �м��Ͽ� Compress�� ���ϵ��� �����Ͽ� �����Ѵ�. (headerlist_ptr)
		merge_filelist(headerlist_ptr,&merge_org_size,argc,argv);

		//���յ� ������ ���վ����Ѵ�.
		merge_compress(headerlist_ptr,argv[argc-1]);

		//���վ���� ���Ͽ� ���� ��Ÿ �����͸� �����Ѵ�. -- ���� �� --
		write_meta(headerlist_ptr,argv[argc-1]);

		//����Ȯ�� �� ���
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
		
		//����� ������ ��Ÿ�����͸� �а� headerlist_ptr�� �����Ѵ�.
		read_meta(headerlist_ptr,&meta,argv[2]);

		//headerList_ptr�� �������� decompress �Ѵ�
		merge_decompress(headerlist_ptr,argv[2],argv[3]);

		printf("Decompress finish!!\n");
		printf("Decompress File Count = %d\n",meta.count);

	//3. encode parallel process //
	}else if (strcmp(argv[1], "-ep") == 0) {
		
		//������� �Է��� �м��Ͽ� Compress�� ���ϵ��� �����Ͽ� �����Ѵ�. (headerlist_ptr)
		merge_filelist(headerlist_ptr,&merge_org_size,argc,argv);

		//�ڽ� ���μ����� �ھ��� ������ŭ fork()�Ѵ�.
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
		
		//�ڽ� ���μ������� �۾��� �Ҵ��Ѵ�.
		parent_divide(headerlist_ptr,pids,cores);

		//�ڽ� ���μ����� �ڽ��� �Ҵ�� �۾��� �� ���������� ��ٸ���.
		while((wait(NULL))>0);

		//�ڽ� ���μ����� ������ ������ �θ����μ����� �װ��� �����Ͽ� Compress�Ѵ�.
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

		//����� ������ ��Ÿ�����͸� �а� headerlist_ptr�� �����Ѵ�.
		read_meta(headerlist_ptr,&meta,argv[2]);
	
		//�ھ��� ������ŭ �ڽ� ���μ��� fork()
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
