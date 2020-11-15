#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <config.h>
#include <input_manager.h>
#include <disp_manager.h>
#include <video_manager.h>
#include <convert_manager.h>
#include <render.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>


/* video2lcd </dev/video0,1,...> */
int main(int argc, char **argv)
{	
	int iError;
    T_VideoDevice tVideoDevice;
    PT_VideoConvert ptVideoConvert;
    int iPixelFormatOfVideo;
    int iPixelFormatOfDisp;

    PT_VideoBuf ptVideoBufCur;
    T_VideoBuf tVideoBuf;
    T_VideoBuf tConvertBuf;
    T_VideoBuf tZoomBuf;
    T_VideoBuf tFrameBuf;
    
    int iLcdWidth;
    int iLcdHeigt;
    int iLcdBpp;

    int iTopLeftX;
    int iTopLeftY;

    float k;

    T_InputEvent tInputEvent;
        
    if (argc != 2)
    {
        printf("Usage:\n");
        printf("%s </dev/video0,1,...>\n", argv[0]);
        return -1;
    }
    
    

    /* 一系列的初始化 */
	/* 注册显示设备 */
	DisplayInit();
	/* 可能可支持多个显示设备: 选择和初始化指定的显示设备 */
	SelectAndInitDefaultDispDev("crt");
    GetDispResolution(&iLcdWidth, &iLcdHeigt, &iLcdBpp);
    GetVideoBufForDisplay(&tFrameBuf);
    iPixelFormatOfDisp = tFrameBuf.iPixelFormat;
    
    VideoInit();

    iError = VideoDeviceInit(argv[1], &tVideoDevice);
    if (iError)
    {
        DBG_PRINTF("VideoDeviceInit for %s error!\n", argv[1]);
        return -1;
    }
    iPixelFormatOfVideo = tVideoDevice.ptOPr->GetFormat(&tVideoDevice);

    VideoConvertInit();
    ptVideoConvert = GetVideoConvertForFormats(iPixelFormatOfVideo, iPixelFormatOfDisp);
    if (NULL == ptVideoConvert)
    {
        DBG_PRINTF("can not support this format convert\n");
        return -1;
    }

    InputInit();
	iError = AllInputDevicesInit();
	if (iError)
	{
		DBG_PRINTF("Error AllInputDevicesInit\n");
		return -1;
	}

    /* 启动摄像头设备 */
    iError = tVideoDevice.ptOPr->StartDevice(&tVideoDevice);
    if (iError)
    {
        DBG_PRINTF("StartDevice for %s error!\n", argv[1]);
        return -1;
    }

    memset(&tVideoBuf, 0, sizeof(tVideoBuf));
    memset(&tConvertBuf, 0, sizeof(tConvertBuf));
    tConvertBuf.iPixelFormat     = iPixelFormatOfDisp;
    tConvertBuf.tPixelDatas.iBpp = iLcdBpp;
    
    
    memset(&tZoomBuf, 0, sizeof(tZoomBuf));
    
    while (GetInputEvent(&tInputEvent))  /* 如果没有按键输入,则循环显示 */
    {
        /* 读入摄像头数据 */
        iError = tVideoDevice.ptOPr->GetFrame(&tVideoDevice, &tVideoBuf);
        if (iError)
        {
            DBG_PRINTF("GetFrame for %s error!\n", argv[1]);
            return -1;
        }
        ptVideoBufCur = &tVideoBuf;

        if (iPixelFormatOfVideo != iPixelFormatOfDisp)
        {
            /* 转换为RGB */
            iError = ptVideoConvert->Convert(&tVideoBuf, &tConvertBuf);
            DBG_PRINTF("Convert %s, ret = %d\n", ptVideoConvert->name, iError);
            if (iError)
            {
                DBG_PRINTF("Convert for %s error!\n", argv[1]);
                return -1;
            }            
            ptVideoBufCur = &tConvertBuf;
        }
        

        /* 如果图像分辨率大于LCD, 缩放 */
        if (ptVideoBufCur->tPixelDatas.iWidth > iLcdWidth || ptVideoBufCur->tPixelDatas.iHeight > iLcdHeigt)
        {
            /* 确定缩放后的分辨率 */
            /* 把图片按比例缩放到VideoMem上, 居中显示
             * 1. 先算出缩放后的大小
             */
            k = (float)ptVideoBufCur->tPixelDatas.iHeight / ptVideoBufCur->tPixelDatas.iWidth;
            tZoomBuf.tPixelDatas.iWidth  = iLcdWidth;
            tZoomBuf.tPixelDatas.iHeight = iLcdWidth * k;
            if ( tZoomBuf.tPixelDatas.iHeight > iLcdHeigt)
            {
                tZoomBuf.tPixelDatas.iWidth  = iLcdHeigt / k;
                tZoomBuf.tPixelDatas.iHeight = iLcdHeigt;
            }
            tZoomBuf.tPixelDatas.iBpp        = iLcdBpp;
            tZoomBuf.tPixelDatas.iLineBytes  = tZoomBuf.tPixelDatas.iWidth * tZoomBuf.tPixelDatas.iBpp / 8;
            tZoomBuf.tPixelDatas.iTotalBytes = tZoomBuf.tPixelDatas.iLineBytes * tZoomBuf.tPixelDatas.iHeight;

            if (!tZoomBuf.tPixelDatas.aucPixelDatas)
            {
                tZoomBuf.tPixelDatas.aucPixelDatas = malloc(tZoomBuf.tPixelDatas.iTotalBytes);
            }
            
            PicZoom(&ptVideoBufCur->tPixelDatas, &tZoomBuf.tPixelDatas);
            ptVideoBufCur = &tZoomBuf;
        }

        /* 合并进framebuffer */
        /* 接着算出居中显示时左上角坐标 */
        iTopLeftX = (iLcdWidth - ptVideoBufCur->tPixelDatas.iWidth) / 2;
        iTopLeftY = (iLcdHeigt - ptVideoBufCur->tPixelDatas.iHeight) / 2;

        PicMerge(iTopLeftX, iTopLeftY, &ptVideoBufCur->tPixelDatas, &tFrameBuf.tPixelDatas);

        FlushPixelDatasToDev(&tFrameBuf.tPixelDatas);

        iError = tVideoDevice.ptOPr->PutFrame(&tVideoDevice, &tVideoBuf);
        if (iError)
        {
            DBG_PRINTF("PutFrame for %s error!\n", argv[1]);
            return -1;
        }       
        
    }

    ExitDispDev();
		
	return 0;
}

