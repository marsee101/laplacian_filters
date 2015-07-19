// laplacian_filter5.c
// RGBをYに変換後にラプラシアンフィルタを掛ける。
// ピクセルのフォーマットは、{8'd0, R(8bits), G(8bits), B(8bits)}, 1pixel = 32bits
// 2013/09/16
// 2014/12/04 : ZYBO用Ubuntu Linux のUIO用に変更
// 2015/07/18 : OpenMP用に for 文の中の分岐を無くした

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
    volatile unsigned int *fb_addr, *next_frame_addr;
    int lap_fil_val;
    int x, y;
    struct timeval start_time, temp1, temp2, end_time;
    unsigned int line_buf[3][HORIZONTAL_PIXEL_WIDTH];
    int fl, sl, tl;
    int fd0, fd3;
    volatile unsigned *bmdc_axi_lites;
    volatile unsigned int *frame_buffer;

    // gettimeofday(&start_time, NULL);    // プログラム起動時の時刻を記録

    // frame_buffer にマップする
    fd3 = open("/dev/uio3", O_RDWR); // Frame Buffer
    if (fd3 < 1){
        fprintf(stderr, "/dev/uio3 open error\n");
        exit(-1);
    }
    frame_buffer = (volatile unsigned *)mmap(NULL, 0x1000000, PROT_READ|PROT_WRITE, MAP_SHARED, fd3, 0);
    if (!frame_buffer){
        fprintf(stderr, "frame_buffer mmap error\n");
        exit(-1);
    }
    fb_addr = (volatile unsigned int *)((unsigned int)frame_buffer + (unsigned int)(VIDEO_BUFFER_START_ADDRESS-CMA_START_ADDRESS));

    // ラプラシアンフィルタの結果を入れておくフレーム・バッファ
    next_frame_addr = (volatile unsigned int *)((unsigned int)frame_buffer + (unsigned int)(LAPLACIAN_FILTER_ADDRESS-CMA_START_ADDRESS));

    gettimeofday(&start_time, NULL);
    
    // RGB値をY（輝度成分）のみに変換し、ラプラシアンフィルタを掛けた。
    for (y=0; y<VERTICAL_PIXEL_WIDTH; y++){
        for (x=0; x<HORIZONTAL_PIXEL_WIDTH; x++){               
            // 1つのピクセルを読み込みながらラプラシアン・フィルタを実行する
            line_buf[y%3][x] = fb_addr[(y*HORIZONTAL_PIXEL_WIDTH)+x];
            line_buf[y%3][x] = conv_rgb2y(line_buf[y%3][x]);
            
            if ((y < 2) || (x < 2)){
                next_frame_addr[(y*HORIZONTAL_PIXEL_WIDTH)+x] = 0;
                continue;
            }
            
            fl = (y+1)%3;    // 最初のライン, y=2 012, y=3 120, y=4 201
            sl = (y+2)%3;    // 2番めのライン
            tl = (y+3)%3;    // 3番目のライン
            lap_fil_val = laplacian_fil(line_buf[fl][x-2], line_buf[fl][x-1], line_buf[fl][x], line_buf[sl][x-2], line_buf[sl][x-1], line_buf[sl][x], line_buf[tl][x-2], line_buf[tl][x-1], line_buf[tl][x]);
            // ラプラシアンフィルタ・データの書き込み
            next_frame_addr[(y*HORIZONTAL_PIXEL_WIDTH)+x] = (lap_fil_val<<16)+(lap_fil_val<<8)+lap_fil_val ;
            // printf("x = %d  y = %d", x, y);
        }
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
