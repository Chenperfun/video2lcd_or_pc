
#ifndef _RENDER_H
#define _RENDER_H

#include <pic_operation.h>
#include <disp_manager.h>

int PicZoom(PT_PixelDatas ptOriginPic, PT_PixelDatas ptZoomPic);
int PicMerge(int iX, int iY, PT_PixelDatas ptSmallPic, PT_PixelDatas ptBigPic);


#endif /* _RENDER_H */

