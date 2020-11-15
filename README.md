# 背景

​		在完成V4L2程序时，我对V4L2框架的了解相当朦胧，对uvc驱动更是所知甚少。再后来工作之余，为补充完善这部分程序，特意重写uvc底层驱动，通过这样的方式，彻底了解了uvc工作原理以及底层操作函数，在此基础上，彻底完善了该程序，才有了现在的最终版本。所以，我最大的收获是，即使对于软件开发来说，Linux驱动的学习也是至关重要！好了就不多BB了，接下来进入正题，开始记录V4L2摄像头应用的全过程。最后，提前感谢您的阅读，若有不足之处，还望不吝赐教，谢谢~



# 一、应用简介

​		V4L2是Video for linux2的简称,为linux中关于视频设备的内核驱动。在Linux中，视频设备是设备文件，可以像访问普通文件一样对其进行读写，摄像头在/dev/video0下。v4L2是针对uvc免驱usb设备的编程框架 [2] ，主要用于采集usb摄像头等。

​		本程序可用于任意USB/COMS标准的摄像头，其功能为读取USB/COMS摄像头数据，通过调用UVC驱动VideoStream接口函数，实现数据接收；通过调用VideoControl接口来实现属性控制（如音量等）。

​		根据分离分层的编程设计思想，每个模块由各自的manager管理，上层直接调用manager中的接口，不用和底层打交道。这样做有利于程序进一步的开发与拓展。





# 二、程序框架

​		应用程序基于UVC驱动的任意USB/COMS摄像头，支持输入格式YUYV、MJPEG以及标准RGB的原始格式，将数据转化为标准的RGB，缩放并合并，最终在LCD和PC虚拟终端（SVGA）显示实时视频监控。分为5个模块，依次为：

​		1、视频读取模块：video。根据UVC接口从底层获得视频原始数据，此处构造一个结构体videoopr，存放获取相关函数。得到的数据存入videobuf。

​		2、格式转换模块：convert。原始数据无法在LCD上显示，需要转换为RGB的标准格式。根据输入、输出的格式不同，此处分为3个子模块：YUV2RGB、MJPEG2RGB、RGB2RGB。由一个convert_mamager管理。

​		3、缩放显示模块：zoom。读到的数据分辨率可能大于屏幕，并且考虑用户体验，实现显示的放大和缩小。

​		4、合并显示模块：merge。缩放完成的数据，需要放入某个LCD背景。该模块获取LCD硬件分辨率，构造背景，并将缩放后的数据合并入背景。

​		5、最终显示模块：display。最终的显示模块，分为两个子模块：fb、crt，分别将数据显示到LCD、PC虚拟终端。

​		6、按键输入模块：input。考虑到在PC虚拟终端显示时，需要退出，所以引入该模块，简单的实现q键退出功能。

​		如下图所示：

![](C:\Users\Administrator\Desktop\项目\V4L2摄像头应用\video2lcd\video2lcd_or_pc\框架.png)



# 三、依赖安装

1、格式转换模块需要用到libjpeg库来解析mjpeg格式的视频输出。

2、显示模块实现了将结果输出到PC虚拟终端的操作，此处用到了SVGA。



# 四、模块分析

## 4.1 视频读取模块

​		要了解该模块，首先需要介绍一部分uvc的知识。

​		uvc驱动在ioctl中提供了传输流程，如下所示：

```
VIDIOC_QUERYCAP 确定它是否视频捕捉设备,支持哪种接口(streaming/read,write)
VIDIOC_ENUM_FMT 查询支持哪种格式
VIDIOC_S_FMT    设置摄像头使用哪种格式
VIDIOC_REQBUFS  申请buffer
 
VIDIOC_QUERYBUF 确定每一个buffer的信息 并且 mmap
VIDIOC_QBUF     放入队列
VIDIOC_STREAMON 启动设备
poll            等待有数据
VIDIOC_DQBUF    从队列中取出
处理....
VIDIOC_QBUF     放入队列
....
```

​		该方法是利用steam流的方式传输数据，在应用程序中，还可以使用read/write的方法来获取数据：

```
read
处理....
read
```

​		两种方法，在本应用中都有体现。

​		对于uvc的传输过程，并非是本文的重点，所以只是大概介绍，在我的另外一篇UVC驱动分析中有详细的说明，请移步：< 后续添加跳转连接 >，接下来开始分析视频原始数据的获取。



​		视频原始数据通过V4L2子模块获取（或许后续可以添加别的框架），通过一个video_manager管理，提供如下的结构体框架

```c
#define NB_BUFFER 4
struct VideoDevice {
    int iFd;
    int iPixelFormat;
    int iWidth;
    int iHeight;

    int iVideoBufCnt;
    int iVideoBufMaxLen;				//buf需要的最大长度，由于支持多种格式，每种格式缓存不同
    int iVideoBufCurIndex;				//当前数据的buf，主要用于启动传输
    unsigned char *pucVideBuf[NB_BUFFER];	//4个缓冲区接收原始数据

    PT_VideoOpr ptOPr;			//操作函数
};

struct VideoOpr {
    char *name;
    int (*InitDevice)(char *strDevName, PT_VideoDevice ptVideoDevice);
    int (*ExitDevice)(PT_VideoDevice ptVideoDevice);
    int (*GetFrame)(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf);	//获得有数据缓存
    int (*GetFormat)(PT_VideoDevice ptVideoDevice);				//查询格式
    int (*PutFrame)(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf);//释放读完的缓存
    int (*StartDevice)(PT_VideoDevice ptVideoDevice);		//启动传输
    int (*StopDevice)(PT_VideoDevice ptVideoDevice);
    struct VideoOpr *ptNext;
};
```

​		video_manager通过RegisterVideoOpr函数，将v4l2模块的VideoDevice结构体注册入全局链表。上层直接调用video_manager中的函数即可，不用直接操作v4l2层。该应用符合分离分层的编程设计思想。

​		v4l2子模块中，首先构造一个操作结构体，如下所示：

```c
static T_VideoOpr g_tV4l2VideoOpr = {
    .name        = "v4l2",
    .InitDevice  = V4l2InitDevice,
    .ExitDevice  = V4l2ExitDevice,
    .GetFormat   = V4l2GetFormat,
    .GetFrame    = V4l2GetFrameForStreaming,
    .PutFrame    = V4l2PutFrameForStreaming,
    .StartDevice = V4l2StartDevice,
    .StopDevice  = V4l2StopDevice,
};
```

​		接下来是功能的以此实现。



​		首先是设备的初始化：

​		1、根据main函数传入的参数（dev/video0）打开设备，通过open：

```c
    iFd = open(strDevName, O_RDWR);
    if (iFd < 0)
    {
        DBG_PRINTF("can not open %s\n", strDevName);
        return -1;
    }
    ptVideoDevice->iFd = iFd;
```

​		2、调用uvc的ioctl，查询格式参数，并判断：

```c
    iError = ioctl(iFd, VIDIOC_QUERYCAP, &tV4l2Cap);
    memset(&tV4l2Cap, 0, sizeof(struct v4l2_capability));
    iError = ioctl(iFd, VIDIOC_QUERYCAP, &tV4l2Cap);
    if (iError) {
    	DBG_PRINTF("Error opening device %s: unable to query device.\n", strDevName);
    	goto err_exit;
    }

    if (!(tV4l2Cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
    	DBG_PRINTF("%s is not a video capture device\n", strDevName);
        goto err_exit;
    }

	if (tV4l2Cap.capabilities & V4L2_CAP_STREAMING) {
	    DBG_PRINTF("%s supports streaming i/o\n", strDevName);
	}
    
	if (tV4l2Cap.capabilities & V4L2_CAP_READWRITE) {
	    DBG_PRINTF("%s supports read i/o\n", strDevName);
	}
```

​		3、枚举、设置硬件传输数据格式，若某些参数无法设置，需要返回接收：

```c
	while ((iError = ioctl(iFd, VIDIOC_ENUM_FMT, &tFmtDesc)) == 0) 
    ... ...
    GetDispResolution(&iLcdWidth, &iLcdHeigt, &iLcdBpp);
    memset(&tV4l2Fmt, 0, sizeof(struct v4l2_format));
    tV4l2Fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    tV4l2Fmt.fmt.pix.pixelformat = ptVideoDevice->iPixelFormat;
    tV4l2Fmt.fmt.pix.width       = iLcdWidth;
    tV4l2Fmt.fmt.pix.height      = iLcdHeigt;
    tV4l2Fmt.fmt.pix.field       = V4L2_FIELD_ANY;

	iError = ioctl(iFd, VIDIOC_S_FMT, &tV4l2Fmt); 
```

​		4、分配缓存，初始化队列，为正式传输做好准备：

```c
    memset(&tV4l2ReqBuffs, 0, sizeof(struct v4l2_requestbuffers));
    tV4l2ReqBuffs.count = NB_BUFFER;
    tV4l2ReqBuffs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    tV4l2ReqBuffs.memory = V4L2_MEMORY_MMAP;

    iError = ioctl(iFd, VIDIOC_REQBUFS, &tV4l2ReqBuffs);
```

​		5、对于每一个缓存，依次查询其应该的格式，并且映射到用户空间，之后APP可直接操作buffer：

```c
        for (i = 0; i < ptVideoDevice->iVideoBufCnt; i++) 
        {
        	iError = ioctl(iFd, VIDIOC_QUERYBUF, &tV4l2Buf);
            ... ...
        	ptVideoDevice->pucVideBuf[i] = mmap(0 /* start anywhere */ ,
        			  tV4l2Buf.length, PROT_READ, MAP_SHARED, iFd,
        			  tV4l2Buf.m.offset);
        	... ...
        }
```

​		6、将准备就绪的缓存，放入队列：

```c
        	iError = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
```

​		至此，设备初始化完成，接下来需要调用streamon开始传输，此部分有start函数完成。

​		

​		以上是stream方式初始化的方法，针对read/write，初始化只需要分配一个缓存即可。

​		

​		接下来，是启动设备的传输，在V4l2StartDevice函数中完成：			

​		7、启动传输，核心是直接调用uvc驱动的ioctl，中的streamon：

```c
    iError = ioctl(ptVideoDevice->iFd, VIDIOC_STREAMON, &iType);
```

​		

​		数据的获取，V4l2GetFrameForStreaming：

​		8、通过poll，查询数据，无数据休眠，若有数据，则调用VIDIOC_DQBUF，从队列中取出有数据的缓存，并从中解析出原始数据：

```c
static int V4l2GetFrameForStreaming(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
    struct pollfd tFds[1];
    int iRet;
    struct v4l2_buffer tV4l2Buf;
            
    /* poll */
    tFds[0].fd     = ptVideoDevice->iFd;
    tFds[0].events = POLLIN;

    iRet = poll(tFds, 1, -1);
    if (iRet <= 0)
    {
        DBG_PRINTF("poll error!\n");
        return -1;
    }
    
    /* VIDIOC_DQBUF */
    memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
    tV4l2Buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    tV4l2Buf.memory = V4L2_MEMORY_MMAP;
    iRet = ioctl(ptVideoDevice->iFd, VIDIOC_DQBUF, &tV4l2Buf);
    if (iRet < 0) 
    {
    	DBG_PRINTF("Unable to dequeue buffer.\n");
    	return -1;
    }
    ptVideoDevice->iVideoBufCurIndex = tV4l2Buf.index;
	... ...
    ptVideoBuf->tPixelDatas.aucPixelDatas = ptVideoDevice->pucVideBuf[tV4l2Buf.index];    
    return 0;
}
```



​		9、数据处理完成，将缓存放回到队列尾：

```c
static int V4l2PutFrameForStreaming(PT_VideoDevice ptVideoDevice, PT_VideoBuf ptVideoBuf)
{
	... ...
    iError = ioctl(ptVideoDevice->iFd, VIDIOC_QBUF, &tV4l2Buf);
	... ...
```



​	10、循环查询、取出缓存，放回缓存。。。

​	11、当数据传输完成，关闭传输接口：

```c
static int V4l2StopDevice(PT_VideoDevice ptVideoDevice)
{
	... ...
    iError = ioctl(ptVideoDevice->iFd, VIDIOC_STREAMOFF, &iType);     
```

​		至此，原始数据的传输已经彻底完成。在本模块详细分析了streamon的流程，模块还支持read/write方式获取数据，具体就不做过多分析，可以查看源码 :D。



## 4.2 格式转换模块

​		通过v4l2，我们获取了摄像头硬件传递的原始视频数据。但是v4l2传输的原始数据，一般是YUYV或者MJPEG格式的，LCD需要标准的RGB565或RGB32格式数据，所以，需要转换原始数据格式。

​		考虑到兼容性，程序支持三种输入格式，分别为：YUYV / MJPEG / RGB。另外，输出可以根据参数设置为两种格式：RGB565、RGB32。

​		同样，根据分离分层思想，通过一个manager管理转换模块的三个子模块：yuv2rgb / mjpeg2rgb / rgb2rgb。通过一个转换结构体：

```c
typedef struct VideoConvert {
    char *name;
    int (*isSupport)(int iPixelFormatIn, int iPixelFormatOut);
    int (*Convert)(PT_VideoBuf ptVideoBufIn, PT_VideoBuf ptVideoBufOut);
    int (*ConvertExit)(PT_VideoBuf ptVideoBufOut);
    struct VideoConvert *ptNext;
}T_VideoConvert, *PT_VideoConvert;
```

​		在manager中，将三个子模块注册入全局链表，并提供一个转换函数，通过该函数调用下层的具体转换函数：

```c
PT_VideoConvert GetVideoConvertForFormats(int iPixelFormatIn, int iPixelFormatOut)
{
	PT_VideoConvert ptTmp = g_ptVideoConvertHead;
	
	while (ptTmp)
	{
        if (ptTmp->isSupport(iPixelFormatIn, iPixelFormatOut))
        {
            return ptTmp;
        }
		ptTmp = ptTmp->ptNext;
	}
	return NULL;
}
```



### 4.2.1 YUV格式转换

​		YUV422格式转换函数如下：

```c
static int Yuv2RgbConvert(PT_VideoBuf ptVideoBufIn, PT_VideoBuf ptVideoBufOut)
{
    if (ptVideoBufOut->iPixelFormat == V4L2_PIX_FMT_RGB565)
        Pyuv422torgb565(ptPixelDatasIn->aucPixelDatas, ptPixelDatasOut->aucPixelDatas, ptPixelDatasOut->iWidth, ptPixelDatasOut->iHeight);
	... ...
    else if (ptVideoBufOut->iPixelFormat == V4L2_PIX_FMT_RGB32)
        Pyuv422torgb32(ptPixelDatasIn->aucPixelDatas, ptPixelDatasOut->aucPixelDatas, ptPixelDatasOut->iWidth, ptPixelDatasOut->iHeight);
	... ...         
```

​		根据不同的输出格式，封装为2个函数，RGB565和RGB32格式本质是相同的，只不过像素构成是R[11-15] + G[5-10] + B[0-4]；RGB32：0[25-32] + R[16-24] + G[8-15] + B[0-7]。所以，就不单独分析了。

```c
	for (i = size; i > 0; i--) {
		/* bgr instead rgb ?? */
		Y = buff[0] ;
		U = buff[1] ;
		Y1 = buff[2];
		V = buff[3];
		buff += 4;
		r = R_FROMYV(Y,V);
		g = G_FROMYUV(Y,U,V); //b
		b = B_FROMYU(Y,U); //v

        /* 把r,g,b三色构造为rgb565的16位值 */
        r = r >> 3;
        g = g >> 2;
        b = b >> 3;
        color = (r << 11) | (g << 5) | b;
        *output_pt++ = color & 0xff;
        *output_pt++ = (color >> 8) & 0xff;
			
		r = R_FROMYV(Y1,V);
		g = G_FROMYUV(Y1,U,V); //b
		b = B_FROMYU(Y1,U); //v
		
        /* 把r,g,b三色构造为rgb565的16位值 */
        r = r >> 3;
        g = g >> 2;
        b = b >> 3;
        color = (r << 11) | (g << 5) | b;
        *output_pt++ = color & 0xff;
        *output_pt++ = (color >> 8) & 0xff;
	}
```

​		YUV422有自己的解析库，一帧的YUV数据可以被拆分为2个RGB8的数据包，这样就可以构造出RGB16的数据，并输出。同理RGB32位的数据格式也是类似的处理方法。





### 4.2.2 MJPEG格式转换

​		MJPEG实质上每一帧数据都是一个完整的JPEG文件，所以，解析JPEG将会用到libjpeg库。		

```c
static int Mjpeg2RgbConvert(PT_VideoBuf ptVideoBufIn, PT_VideoBuf ptVideoBufOut)
{
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
    
    jpeg_mem_src_tj (&tDInfo, ptVideoBufIn->tPixelDatas.aucPixelDatas, ptVideoBufIn->tPixelDatas.iTotalBytes);

    iRet = jpeg_read_header(&tDInfo, TRUE);

	// 设置解压参数,比如放大、缩小
    tDInfo.scale_num = tDInfo.scale_denom = 1;
    
	// 启动解压：jpeg_start_decompress	
	jpeg_start_decompress(&tDInfo);
    
	// 循环调用jpeg_read_scanlines来一行一行地获得解压的数据
	while (tDInfo.output_scanline < tDInfo.output_height) 
	{
        /* 得到一行数据,里面的颜色格式为0xRR, 0xGG, 0xBB */
		(void) jpeg_read_scanlines(&tDInfo, &aucLineBuffer, 1);

		// 转到ptPixelDatas去
		CovertOneLine(ptPixelDatas->iWidth, 24, ptPixelDatas->iBpp, aucLineBuffer, pucDest);
		pucDest += ptPixelDatas->iLineBytes;
    ... ...
}
```

​		libjpeg库的解压如上所示，核心是扫描JPEG图片中一行的数据，将其转化为RGB32。如果想要转换为RGB24格式的话，需要我们自己处理。通过CovertOneLine实现，首先判断输出格式，若为RGB32，则不作任何处理，memcpy即可。若为RGB24，则组合数据：

```C
				dwRed   = dwRed >> 3;
				dwGreen = dwGreen >> 2;
				dwBlue  = dwBlue >> 3;
				dwColor = (dwRed << 11) | (dwGreen << 5) | (dwBlue);
				*pwDstDatas16bpp = dwColor;
				pwDstDatas16bpp++;
```

​		到此，格式转换模块完成，得到了RGB24位或者RGB32位的颜色数据。



## 4.3 缩放显示模块

​		得到RGB标准格式的数据后，需要判断其大小，是否超出屏幕的分辨率，若超过，则按一定比例缩放。用户也可通过缩放函数，调整显示窗口大小。

```c
int PicZoom(PT_PixelDatas ptOriginPic, PT_PixelDatas ptZoomPic)
{
	... ...
    if (ptOriginPic->iBpp != ptZoomPic->iBpp)
		return -1;
    
    pdwSrcXTable = malloc(sizeof(unsigned long) * dwDstWidth);
    for (x = 0; x < dwDstWidth; x++)//生成表 pdwSrcXTable
    {
        pdwSrcXTable[x]=(x*ptOriginPic->iWidth/ptZoomPic->iWidth);
    }

    for (y = 0; y < ptZoomPic->iHeight; y++)
    {			
        dwSrcY = (y * ptOriginPic->iHeight / ptZoomPic->iHeight);

		pucDest = ptZoomPic->aucPixelDatas + y*ptZoomPic->iLineBytes;
		pucSrc  = ptOriginPic->aucPixelDatas + dwSrcY*ptOriginPic->iLineBytes;
		
        for (x = 0; x <dwDstWidth; x++)
        {
            /* 原图座标: pdwSrcXTable[x]，srcy
             * 缩放座标: x, y
			 */
			 memcpy(pucDest+x*dwPixelBytes, pucSrc+pdwSrcXTable[x]*dwPixelBytes, dwPixelBytes);
        }
    }
}
```

​		部分缩放代码如上所示。



## 4.4 合并显示模块

​		将得到的适合LCD分辨率的显示结构体，合并入LCD的背景中。

```c
int PicMerge(int iX, int iY, PT_PixelDatas ptSmallPic, PT_PixelDatas ptBigPic)
{
	pucSrc = ptSmallPic->aucPixelDatas;
	pucDst = ptBigPic->aucPixelDatas + iY * ptBigPic->iLineBytes + iX * ptBigPic->iBpp / 8;
	for (i = 0; i < ptSmallPic->iHeight; i++)
	{
		memcpy(pucDst, pucSrc, ptSmallPic->iLineBytes);
		pucSrc += ptSmallPic->iLineBytes;
		pucDst += ptBigPic->iLineBytes;
	}
	return 0;
}
```

​		至此，需要显示的数据已经全部准备好，只需要放入FB，或者刷新屏幕即可。



## 4.5 显示模块

​		该模块分为两个子模块：fb，crt。分别用于在LCD、PC虚拟终端显示。各自的显示函数为：

​		FB模块的刷新到屏幕（显示），就是将最终的数据，写入FrameBuffer。

```c
static int FBShowPage(PT_PixelDatas ptPixelDatas)
{
    if (g_tFBOpr.pucDispMem != ptPixelDatas->aucPixelDatas)
    {
    	memcpy(g_tFBOpr.pucDispMem, ptPixelDatas->aucPixelDatas, ptPixelDatas->iTotalBytes);
    }
	return 0;
}
```

​		crt模块的显示，是将数据组合为符合SVGA库标准的形式，最终调用SVGA提供的API：

```c
static int CRTShowPage(PT_PixelDatas ptPixelDatas)
{
    int x, y;
    unsigned int *pdwColor = (unsigned int *)ptPixelDatas->aucPixelDatas;
    unsigned int dwColor;
    unsigned int dwRed, dwGreen, dwBlue;
    
    if (ptPixelDatas->iBpp != 32)
    {
        return -1;
    }
    
    for (y = 0; y < g_tCRTOpr.iYres; y++)
    {
        for (x = 0; x < g_tCRTOpr.iXres; x++)
        {
            /* 0x00RRGGBB */
            dwColor = *pdwColor++;
            dwRed   = (dwColor >> 16) & 0xff;
            dwGreen = (dwColor >> 8) & 0xff;
            dwBlue  = (dwColor >> 0) & 0xff;
            
            // CRTShowPixel(x, y, dwColor);
            gl_setpixelrgb(x, y, dwRed, dwGreen, dwBlue);
        }
    }

    gl_copyscreen(physicalscreen);
    
    return 0;
}

```



## 4.6 输入模块

​		此模块用于，当程序在PC的虚拟终端运行时，按q键可以退出到用户操作界面。相对简单，使用stdin作为输入：

```c
static int StdinGetInputEvent(PT_InputEvent ptInputEvent)
{
    struct timeval tTV;
    fd_set tFDs;
	char c;
	
    tTV.tv_sec = 0;
    tTV.tv_usec = 0;
    FD_ZERO(&tFDs);
	
    FD_SET(STDIN_FILENO, &tFDs); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &tFDs, NULL, NULL, &tTV);
	
    if (FD_ISSET(STDIN_FILENO, &tFDs))
    {
		/* 处理数据 */
		ptInputEvent->iType = INPUT_TYPE_STDIN;
		gettimeofday(&ptInputEvent->tTime, NULL);
		
		c = fgetc(stdin);
		if (c == 'q')
		{
			ptInputEvent->iVal = INPUT_VALUE_EXIT;
		}
		return 0;
    }
	else
	{
		return -1;
	}
}
```

​		这里用到查询方式，select，将其设置为非阻塞访问。



# 五、问题与解决

## 5.1 终端模式的修改

​		在从stdin获取输入时，通过键盘输入任意值，必须要回车才能将数据读出。

​		但是，这里需要的是任意键都可返回，则违背了设计的初衷。所以，对终端作如下初始化，使其在获得任意输入立刻返回。

```c
static int StdinDevInit(void)
{
    struct termios tTTYState;
 
    //get the terminal state
    tcgetattr(STDIN_FILENO, &tTTYState);
 
    //turn off canonical mode
    tTTYState.c_lflag &= ~ICANON;
    //minimum of number input read.
    tTTYState.c_cc[VMIN] = 1;   /* 有一个数据时就立刻返回 */

    //set the terminal attributes.
    tcsetattr(STDIN_FILENO, TCSANOW, &tTTYState);

	return 0;
}
```



## 5.1 select的非阻塞与休眠

```
int select(int maxfdp,fd_set *readfds,fd_set *writefds,fd_set *errorfds,struct timeval *timeout);
```

各个参数的用法：

int maxfdp：是一个整数值，是指集合中所有文件描述符的范围，即所有文件描述符的最大值加1，不能错！在Windows中这个参数的值无所谓，可以设置不正确。

struct fd_set：可以理解为一个集合，这个集合中存放的是文件描述符(file descriptor)，即文件句柄。fd_set集合可以通过一些宏由人为来操作。

```
FD_ZERO(fd_set *fdset)：清空fdset与所有文件句柄的联系。 
FD_SET(int fd, fd_set *fdset)：建立文件句柄fd与fdset的联系。 
FD_CLR(int fd, fd_set *fdset)：清除文件句柄fd与fdset的联系。 
FD_ISSET(int fd, fdset *fdset)：检查fdset联系的文件句柄fd是否可读写，>0表示可读写。
```

struct timeval：用来代表时间值，有两个成员，一个是秒数，另一个是毫秒数。

​		 1、若将NULL以形参传入，即不传入时间结构，就是将select置于阻塞状态，一定等到监视文件描述符集合中某个文件描述符发生变化为止；

​		2、若将时间值设为0秒0毫秒，就变成一个纯粹的非阻塞函数，不管文件描述符是否有变化，都立刻返回继续执行，文件无变化返回0，有变化返回一个正值；

​		3、timeout的值大于0，这就是等待的超时时间，即select在timeout时间内阻塞，超时时间之内有事件到来就返回了，否则在超时后不管怎样一定返回。

```c
struct timeval{      
        long tv_sec;   /*秒 */
        long tv_usec;  /*微秒 */   
}
```









