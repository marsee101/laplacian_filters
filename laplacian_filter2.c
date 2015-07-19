// laplacian_filter2.c
// RGBをYに変換後にラプラシアンフィルタを掛ける。
// ピクセルのフォーマットは、{8'd0, R(8bits), G(8bits), B(8bits)}, 1pixel = 32bits
// 2013/09/16
// 2014/12/04 : ZYBO用Ubuntu Linux のUIO用に変更
// Vivado HLS 2014.4 のプロジェクト http://marsee101.blog19.fc2.com/blog-entry-3102.html

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <fcntl.h>

#define HORIZONTAL_PIXEL_WIDTH    800
#define VERTICAL_PIXEL_WIDTH    600
#define ALL_PIXEL_VALUE    (HORIZONTAL_PIXEL_WIDTH*VERTICAL_PIXEL_WIDTH)

#define CMA_START_ADDRESS           0x17800000
#define VIDEO_BUFFER_START_ADDRESS  0x18000000  // Limit 0x18800000, 800*600*4 = 2MBytes * 2
#define LAPLACIAN_FILTER_ADDRESS    0x18200000  // 800*600*4 = 0x1d4c00

int laplacian_fil(int x0y0, int x1y0, int x2y0, int x0y1, int x1y1, int x2y1, int x0y2, int x1y2, int x2y2);
int conv_rgb2y(int rgb);
int chkhex(char *str);

int main()
{
    volatile unsigned int *cam_fb = 0;
    volatile unsigned int *lap_fb = 0;
    volatile unsigned int *cam_addr;
    volatile unsigned int *lap_addr;
    int lap_fil_val;
    int x, y;
    struct timeval start_time, temp1, temp2, end_time;
    unsigned int line_buf[3][HORIZONTAL_PIXEL_WIDTH];
    int a, b;
    int fl, sl, tl;
    int fd0, fd3;
  	unsigned int offset_cam_addr, offset_lap_addr;
	unsigned int lap_buf[HORIZONTAL_PIXEL_WIDTH];
	volatile unsigned int *cam_fb_addr, *lap_fb_addr;
	int line_sel;
    volatile unsigned int *bmdc_axi_lites;
    volatile unsigned int *frame_buffer;

    // gettimeofday(&start_time, NULL);    // プログラム起動時の時刻を記録

    // frame_buffer にマップする
    fd3 = open("/dev/uio3", O_RDWR); // Frame Buffer
    if (fd3 < 1){
        fprintf(stderr, "/dev/uio3 open error\n");
        exit(-1);
    }
    frame_buffer = (volatile unsigned int *)mmap(NULL, 0x1000000, PROT_READ|PROT_WRITE, MAP_SHARED, fd3, 0);
    if (!frame_buffer){
        fprintf(stderr, "frame_buffer mmap error\n");
        exit(-1);
    }
    cam_addr = (volatile unsigned int *)((unsigned int)frame_buffer + (unsigned int)(VIDEO_BUFFER_START_ADDRESS-CMA_START_ADDRESS));

    // ラプラシアンフィルタの結果を入れておくフレーム・バッファ
    lap_addr = (volatile unsigned int *)((unsigned int)frame_buffer + (unsigned int)(LAPLACIAN_FILTER_ADDRESS-CMA_START_ADDRESS));

	offset_cam_addr = (volatile unsigned int)((unsigned int)cam_addr/sizeof(int));
	offset_lap_addr = (volatile unsigned int)((unsigned int)lap_addr/sizeof(int));
	
	gettimeofday(&start_time, NULL);
	
    // RGB値をY（輝度成分）のみに変換し、ラプラシアンフィルタを掛けた。
   for (y=0; y<VERTICAL_PIXEL_WIDTH; y++){
        fl = (y-1)%3;    // 最初のライン, y=1 012, y=2 120, y=3 201, y=4 012
        sl = y%3;        // 2番めのライン
        tl = (y+1)%3;    // 3番目のライン
        for (x=0; x<HORIZONTAL_PIXEL_WIDTH; x++){
            if (y==0 || y==VERTICAL_PIXEL_WIDTH-1){ // 縦の境界の時の値は0とする
                lap_fil_val = 0;
            }else if (x==0 || x==HORIZONTAL_PIXEL_WIDTH-1){ // 横の境界の時も値は0とする
                lap_fil_val = 0;
            }else{
                 if (x == 1){ // ラインの最初でラインの画素を読み出す
                    if (y == 1){ // 最初のラインでは3ライン分の画素を読み出す
                        for (a=0; a<3; a++){ // 3ライン分
                            cam_fb_addr = (int*)(cam_fb+offset_cam_addr+(a*(HORIZONTAL_PIXEL_WIDTH)));
                            memcpy(&line_buf[a][0], (const int*)cam_fb_addr, HORIZONTAL_PIXEL_WIDTH*sizeof(int));
                            for (b=0; b<HORIZONTAL_PIXEL_WIDTH; b++){ // ライン
                                line_buf[a][b] = conv_rgb2y(line_buf[a][b]);    // カラーから白黒へ
                            }
                        }
                    } else { // 最初のラインではないので、1ラインだけ読み込む。すでに他の2ラインは読み込まれている
                        cam_fb_addr = (int*)(cam_fb+offset_cam_addr+((y+1)*(HORIZONTAL_PIXEL_WIDTH)));
                         memcpy(line_buf[(y+1)%3], (const int*)cam_fb_addr, HORIZONTAL_PIXEL_WIDTH*sizeof(int));
                        for (b=0; b<HORIZONTAL_PIXEL_WIDTH; b++){ // ライン
                            line_buf[(y+1)%3][b] = conv_rgb2y(line_buf[(y+1)%3][b]);    // カラーから白黒へ
                        }
                    }
                }
                lap_fil_val = laplacian_fil(line_buf[fl][x-1], line_buf[fl][x], line_buf[fl][x+1], line_buf[sl][x-1], line_buf[sl][x], line_buf[sl][x+1], line_buf[tl][x-1], line_buf[tl][x], line_buf[tl][x+1]);
            }
            lap_buf[x] = (lap_fil_val<<16)+(lap_fil_val<<8)+lap_fil_val; // RGB同じ値を入れる
        }
        lap_fb_addr = (int *)(lap_fb+offset_lap_addr+(y*(HORIZONTAL_PIXEL_WIDTH)));
        memcpy(lap_fb_addr, (const int*)lap_buf, HORIZONTAL_PIXEL_WIDTH*sizeof(int));
    }
	
	gettimeofday(&end_time, NULL);
	
    munmap((void *)frame_buffer, 0x1000000);
 
   // ラプラシアンフィルタ表示画面に切り替え
    // Bitmap Display Controller AXI4 Lite Slave (UIO0)
    fd0 = open("/dev/uio0", O_RDWR); // bitmap_display_controller axi4 lite
    if (fd0 < 1){
        fprintf(stderr, "/dev/uio0 open error\n");
        exit(-1);
    }
    bmdc_axi_lites = (volatile unsigned *)mmap(NULL, 0x10000, PROT_READ|PROT_WRITE, MAP_SHARED, fd0, 0);
    if (!bmdc_axi_lites){
        fprintf(stderr, "bmdc_axi_lites mmap error\n");
        exit(-1);
    }
    bmdc_axi_lites[0] = (unsigned int)LAPLACIAN_FILTER_ADDRESS; // Bitmap Display Controller start (ラプラシアンフィルタ表示画面のアドレス)
    munmap((void *)bmdc_axi_lites, 0x10000);
    
    //gettimeofday(&end_time, NULL);
    if (end_time.tv_usec < start_time.tv_usec) {
        printf("total time = %ld.%06ld sec\n", end_time.tv_sec - start_time.tv_sec - 1, 1000000 + end_time.tv_usec - start_time.tv_usec);
    }
    else {
        printf("total time = %ld.%06ld sec\n", end_time.tv_sec - start_time.tv_sec, end_time.tv_usec - start_time.tv_usec);
    }
    return(0);
}

// RGBからYへの変換
// RGBのフォーマットは、{8'd0, R(8bits), G(8bits), B(8bits)}, 1pixel = 32bits
// 輝度信号Yのみに変換する。変換式は、Y =  0.299R + 0.587G + 0.114B
// "YUVフォーマット及び YUV<->RGB変換"を参考にした。http://vision.kuee.kyoto-u.ac.jp/~hiroaki/firewire/yuv.html
//　2013/09/27 : float を止めて、すべてint にした
int conv_rgb2y(int rgb){
    int r, g, b, y_f;
    int y;

    b = rgb & 0xff;
    g = (rgb>>8) & 0xff;
    r = (rgb>>16) & 0xff;

    y_f = 77*r + 150*g + 29*b; //y_f = 0.299*r + 0.587*g + 0.114*b;の係数に256倍した
    y = y_f >> 8; // 256で割る

    return(y);
}

// ラプラシアンフィルタ
// x0y0 x1y0 x2y0 -1 -1 -1
// x0y1 x1y1 x2y1 -1  8 -1
// x0y2 x1y2 x2y2 -1 -1 -1
int laplacian_fil(int x0y0, int x1y0, int x2y0, int x0y1, int x1y1, int x2y1, int x0y2, int x1y2, int x2y2)
{
    int y;

    y = -x0y0 -x1y0 -x2y0 -x0y1 +8*x1y1 -x2y1 -x0y2 -x1y2 -x2y2;
    if (y<0)
        y = 0;
    else if (y>255)
        y = 255;
    return(y);
}

// 文字列が16進数かを調べる
int chkhex(char *str){
    while (*str != '\0'){
        if (!isxdigit(*str))
            return 0;
        str++;
    }
    return 1;
}
