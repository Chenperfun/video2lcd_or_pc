
/* MJPEG : 实质上每一帧数据都是一个完整的JPEG文件 */

#include <convert_manager.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

typedef struct MyErrorMgr
{
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
}T_MyErrorMgr, *PT_MyErrorMgr;

extern void jpeg_mem_src_tj(j_decompress_ptr, unsigned char *, unsigned long);

static int isSupportMjpeg2Rgb(int iPixelFormatIn, int iPixelFormatOut)
{
    if (iPixelFormatIn != V4L2_PIX_FMT_MJPEG)
        return 0;
    if ((iPixelFormatOut != V4L2_PIX_FMT_RGB565) && (iPixelFormatOut != V4L2_PIX_FMT_RGB32))
    {
        return 0;
    }
    return 1;
}

static void MyErrorExit(j_common_ptr ptCInfo)
{
    static char errStr[JMSG_LENGTH_MAX];
    
	PT_MyErrorMgr ptMyErr = (PT_MyErrorMgr)ptCInfo->err;

    /* Create the message */
    (*ptCInfo->err->format_message) (ptCInfo, errStr);
    DBG_PRINTF("%s\n", errStr);

	longjmp(ptMyErr->setjmp_buffer, 1);
}

static int CovertOneLine(int iWidth, int iSrcBpp, int iDstBpp, unsigned char *pudSrcDatas, unsigned char *pudDstDatas)
{
	unsigned int dwRed;
	unsigned int dwGreen;
	unsigned int dwBlue;
	unsigned int dwColor;

	unsigned short *pwDstDatas16bpp = (unsigned short *)pudDstDatas;
	unsigned int   *pwDstDatas32bpp = (unsigned int *)pudDstDatas;

	int i;
	int pos = 0;

	if (iSrcBpp != 24)
	{
		return -1;
	}

	if (iDstBpp == 24)
	{
		memcpy(pudDstDatas, pudSrcDatas, iWidth*3);
	}
	else
	{
		for (i = 0; i < iWidth; i++)
		{
			dwRed   = pudSrcDatas[pos++];
			dwGreen = pudSrcDatas[pos++];
			dwBlue  = pudSrcDatas[pos++];
			if (iDstBpp == 32)
			{
				dwColor = (dwRed << 16) | (dwGreen << 8) | dwBlue;
				*pwDstDatas32bpp = dwColor;
				pwDstDatas32bpp++;
			}
			else if (iDstBpp == 16)
			{
				/* 565 */
				dwRed   = dwRed >> 3;
				dwGreen = dwGreen >> 2;
				dwBlue  = dwBlue >> 3;
				dwColor = (dwRed << 11) | (dwGreen << 5) | (dwBlue);
				*pwDstDatas16bpp = dwColor;
				pwDstDatas16bpp++;
			}
		}
	}
	return 0;
}

//static int GetPixelDatasFrmJPG(PT_FileMap ptFileMap, PT_PixelDatas ptPixelDatas)
/* 把内存里的JPEG图像转换为RGB图像 */
static int Mjpeg2RgbConvert(PT_VideoBuf ptVideoBufIn, PT_VideoBuf ptVideoBufOut)
{
	struct jpeg_decompress_struct tDInfo;
	//struct jpeg_error_mgr tJErr;
    int iRet;
    int iRowStride;
    unsigned char *aucLineBuffer = NULL;
    unsigned char *pucDest;
	T_MyErrorMgr tJerr;
    PT_PixelDatas ptPixelDatas = &ptVideoBufOut->tPixelDatas;

	// 分配和初始化一个decompression结构体
	//tDInfo.err = jpeg_std_error(&tJErr);

	tDInfo.err               = jpeg_std_error(&tJerr.pub);
	tJerr.pub.error_exit     = MyErrorExit;

	if(setjmp(tJerr.setjmp_buffer))
	{
		/* 如果程序能运行到这里, 表示JPEG解码出错 */
        jpeg_destroy_decompress(&tDInfo);
        if (aucLineBuffer)
        {
            free(aucLineBuffer);
        }
        if (ptPixelDatas->aucPixelDatas)
        {
            free(ptPixelDatas->aucPixelDatas);
        }
		return -1;
	}

	jpeg_create_decompress(&tDInfo);

	// 用jpeg_read_header获得jpg信息
	//jpeg_stdio_src(&tDInfo, ptFileMap->tFp);
	/* 把数据设为内存中的数据 */
    jpeg_mem_src_tj (&tDInfo, ptVideoBufIn->tPixelDatas.aucPixelDatas, ptVideoBufIn->tPixelDatas.iTotalBytes);
    

    iRet = jpeg_read_header(&tDInfo, TRUE);

	// 设置解压参数,比如放大、缩小
    tDInfo.scale_num = tDInfo.scale_denom = 1;
    
	// 启动解压：jpeg_start_decompress	
	jpeg_start_decompress(&tDInfo);
    
	// 一行的数据长度
	iRowStride = tDInfo.output_width * tDInfo.output_components;
	aucLineBuffer = malloc(iRowStride);

    if (NULL == aucLineBuffer)
    {
        return -1;
    }

	ptPixelDatas->iWidth  = tDInfo.output_width;
	ptPixelDatas->iHeight = tDInfo.output_height;
	//ptPixelDatas->iBpp    = iBpp;
	ptPixelDatas->iLineBytes    = ptPixelDatas->iWidth * ptPixelDatas->iBpp / 8;
    ptPixelDatas->iTotalBytes   = ptPixelDatas->iHeight * ptPixelDatas->iLineBytes;
	if (NULL == ptPixelDatas->aucPixelDatas)
	{
	    ptPixelDatas->aucPixelDatas = malloc(ptPixelDatas->iTotalBytes);
	}

    pucDest = ptPixelDatas->aucPixelDatas;

	// 循环调用jpeg_read_scanlines来一行一行地获得解压的数据
	while (tDInfo.output_scanline < tDInfo.output_height) 
	{
        /* 得到一行数据,里面的颜色格式为0xRR, 0xGG, 0xBB */
		(void) jpeg_read_scanlines(&tDInfo, &aucLineBuffer, 1);

		// 转到ptPixelDatas去
		CovertOneLine(ptPixelDatas->iWidth, 24, ptPixelDatas->iBpp, aucLineBuffer, pucDest);
		pucDest += ptPixelDatas->iLineBytes;
	}
	
	free(aucLineBuffer);
	jpeg_finish_decompress(&tDInfo);
	jpeg_destroy_decompress(&tDInfo);

    return 0;
}



static int Mjpeg2RgbConvertExit(PT_VideoBuf ptVideoBufOut)
{
    if (ptVideoBufOut->tPixelDatas.aucPixelDatas)
    {
        free(ptVideoBufOut->tPixelDatas.aucPixelDatas);
        ptVideoBufOut->tPixelDatas.aucPixelDatas = NULL;
    }
    return 0;
}

/* 构造 */
static T_VideoConvert g_tMjpeg2RgbConvert = {
    .name        = "mjpeg2rgb",
    .isSupport   = isSupportMjpeg2Rgb,
    .Convert     = Mjpeg2RgbConvert,
    .ConvertExit = Mjpeg2RgbConvertExit,
};


/* 注册 */
int Mjpeg2RgbInit(void)
{
    return RegisterVideoConvert(&g_tMjpeg2RgbConvert);
}


