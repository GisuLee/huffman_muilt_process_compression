# 허프만 알고리즘과 멀티 프로세스를 이용한 멀티 압축 및 해제 프로그램
리눅스의 fork() 함수를 이용해 멀티 프로세스를 구현하고, 
공유메모리 및 시그널을 통해 프로세스간 통신 및 동기화하여 다수의 파일데이터를 병렬적으로 압축 및 해제 할 수 있습니다.

- 사용 언어 및 플랫폼 : Linux, C

## 1. 압축 DFD
![](https://raw.githubusercontent.com/GisuLee/huffman_muilt_process_compression/master/dfd_encode_mul.png)

## 2. 압축 해제 DFD
![](https://raw.githubusercontent.com/GisuLee/huffman_muilt_process_compression/master/dfd_decode_mul.png)

## 3. 압축 파일의 구조
![](https://raw.githubusercontent.com/GisuLee/huffman_muilt_process_compression/master/filestruct.png)

## 4. 사용법

### 압축
명령어 : <-ep> <input file list> <output compression file name>   
![](https://raw.githubusercontent.com/GisuLee/huffman_muilt_process_compression/master/compression.png)   
<4개의 프로세스가 병렬적으로 파일데이터를 압축>

### 압축 해제
명령어 : <-dp> <compression file> <output folder path>   
![](https://raw.githubusercontent.com/GisuLee/huffman_muilt_process_compression/master/decompression.png)   
<4개의 프로세스가 병렬적으로 압축해제>
