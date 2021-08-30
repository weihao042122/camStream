#include <stdio.h>
#include <stdlib.h>
#include "jpeglib.h"


typedef struct _BITMAPFILEHEADER {

    u_int16_t bfType;
    u_int32_t bfSize;
    u_int16_t bfReserved1;
    u_int16_t bfReserved2;
    u_int32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct _BMPINFOHEAD {
    u_int32_t biSize;
    u_int32_t biWidth;
    u_int32_t biHight;
    u_int16_t biPlanes;
    u_int16_t biBitCount;
    u_int32_t biCompression;
    u_int32_t biSizeImage;
    u_int32_t biXPelsPerMeter;
    u_int32_t biYPelsPerMeter;
    u_int32_t biClrUsed;
    u_int32_t biClrImportant;
} BMPINFOHEAD;

#define MAX_CAM_NUM 4
#define MAX_WIDTH 640
#define MAX_HEIGHT 480

#define YUV_2_B(y, u) ((int)(y + 1.732446 * (u - 128)))
#define YUV_2_G(y, u, v)((int)(y - 0.698001 * (u - 128) - 0.703125 * (v - 128)))
#define YUV_2_R(y, v)((int)(y + 1.370705  * (v - 128)))

int YUYVToBGR24_Native ( unsigned char* pYUV,unsigned char* pBGR24,int width,int height )
{
    if ( width < 1 || height < 1 || pYUV == NULL || pBGR24 == NULL )
        return 0;
//     const long len = width * height;
    unsigned char* yData = pYUV;
    unsigned char* vData = pYUV;
    unsigned char* uData = pYUV;
    int y, x, k;

    int bgr[3];
    int yIdx,uIdx,vIdx,idx;
    for ( y=0; y < height; y++ ) {
        for ( x=0; x < width; x++ ) {
            yIdx = 2* ( ( y*width ) + x );
            uIdx = 4* ( ( ( y*width ) + x ) >>1 ) + 1;
            vIdx = 4* ( ( ( y*width ) + x ) >>1 ) + 3;

            bgr[0] = YUV_2_B ( yData[yIdx], uData[uIdx] ); // b分量
            bgr[1] = YUV_2_G ( yData[yIdx], uData[uIdx], vData[vIdx] ); // g分量
            bgr[2] = YUV_2_R ( yData[yIdx], vData[vIdx] ); // r分量

            for ( k = 0; k < 3; k++ ) {
                idx = ( y * width + x ) * 3 + k;
                if ( bgr[k] >= 0 && bgr[k] <= 255 )
                    pBGR24[idx] = bgr[k];
                else
                    pBGR24[idx] = ( bgr[k] < 0 ) ?0:255;
            }
        }
    }
    return 1;
}
void bmp_write ( char *bmpFile, unsigned char *image_in, int width, int height )
{
    long file_size= ( long ) height* ( long ) width * 3 + 54;
    FILE *fp_out=fopen ( bmpFile, "wb" );

    unsigned char header[54]= {
        0x42,
        0x4d,
        0, 0, 0, 0,
        0, 0,
        0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        1, 0,
        24, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };

    header[2]= ( unsigned char ) ( file_size & 0x000000ff );
    header[3]= ( file_size >> 8 ) & 0x000000ff;
    header[4]= ( file_size >> 16 ) & 0x000000ff;
    header[5]= ( file_size >> 24 ) & 0x000000ff;

    header[18]=width & 0x000000ff;
    header[19]= ( width >> 8 ) & 0x000000ff;
    header[20]= ( width >> 16 ) & 0x000000ff;
    header[21]= ( width >> 24 ) & 0x000000ff;

    header[22]=height & 0x000000ff;
    header[23]= ( height >> 8 ) & 0x000000ff;
    header[24]= ( height >> 16 ) & 0x000000ff;
    header[25]= ( height >> 24 ) & 0x000000ff;

    fprintf (stderr,  "%s %d: %d %d\n",__func__, __LINE__, height, width );

    fwrite ( header, sizeof ( unsigned char ), 54, fp_out );
    fwrite ( image_in, sizeof ( unsigned char ), ( size_t ) ( long ) height*width*3, fp_out );

    fclose ( fp_out );
}
#if 0
unsigned char* compressYUV422toJPEG(unsigned char* src, int width, int height, unsigned long* olen){
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    *olen = 0;
    unsigned char* outbuffer = NULL;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &outbuffer, olen);   
    
    // jrow is a libjpeg row of samples array of 1 row pointer
    cinfo.image_width = width & -1;
    cinfo.image_height = height & -1;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr; //libJPEG expects YUV 3bytes, 24bit

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 50, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    
    unsigned char *tmprowbuf = malloc(width*3);
    JSAMPROW row_pointer[1];
    row_pointer[0] = (JSAMPROW)tmprowbuf;

    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned i, j;
        unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
        for (i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) { //src strides by 4 bytes, output strides by 6 (2 pixels)
            tmprowbuf[j + 0] = src[offset + i + 0]; // Y (unique to this pixel)
            tmprowbuf[j + 1] = src[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 2] = src[offset + i + 3]; // V (shared between pixels)
            tmprowbuf[j + 3] = src[offset + i + 2]; // Y (unique to this pixel)
            tmprowbuf[j + 4] = src[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 5] = src[offset + i + 3]; // V (shared between pixels)
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
//     fprintf (stderr, "libjpeg produced ,olen=%lu\n", *olen);    
    free(tmprowbuf);
    return outbuffer;
}
#else
void compressYUV422toJPEG(unsigned char* src, int width, int height, FILE* out_file){
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    unsigned char* outbuffer = NULL;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, out_file);

    
    // jrow is a libjpeg row of samples array of 1 row pointer
    cinfo.image_width = width & -1;
    cinfo.image_height = height & -1;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr; //libJPEG expects YUV 3bytes, 24bit

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 50, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    
    unsigned char *tmprowbuf = malloc(width*3);
    JSAMPROW row_pointer[1];
    row_pointer[0] = (JSAMPROW)tmprowbuf;

    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned i, j;
        unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
        for (i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) { //src strides by 4 bytes, output strides by 6 (2 pixels)
            tmprowbuf[j + 0] = src[offset + i + 0]; // Y (unique to this pixel)
            tmprowbuf[j + 1] = src[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 2] = src[offset + i + 3]; // V (shared between pixels)
            tmprowbuf[j + 3] = src[offset + i + 2]; // Y (unique to this pixel)
            tmprowbuf[j + 4] = src[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 5] = src[offset + i + 3]; // V (shared between pixels)
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
//     fprintf (stderr, "libjpeg produced ,olen=%lu\n", *olen);    
    free(tmprowbuf);
}
#endif