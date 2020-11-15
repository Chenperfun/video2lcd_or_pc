
#include <config.h>
#include <input_manager.h>
#include <string.h>

static PT_InputOpr g_ptInputOprHead;

int RegisterInputOpr(PT_InputOpr ptInputOpr)
{
	PT_InputOpr ptTmp;

	if (!g_ptInputOprHead)
	{
		g_ptInputOprHead   = ptInputOpr;
		ptInputOpr->ptNext = NULL;
	}
	else
	{
		ptTmp = g_ptInputOprHead;
		while (ptTmp->ptNext)
		{
			ptTmp = ptTmp->ptNext;
		}
		ptTmp->ptNext	  = ptInputOpr;
		ptInputOpr->ptNext = NULL;
	}

	return 0;
}


void ShowInputOpr(void)
{
	int i = 0;
	PT_InputOpr ptTmp = g_ptInputOprHead;

	while (ptTmp)
	{
		printf("%02d %s\n", i++, ptTmp->name);
		ptTmp = ptTmp->ptNext;
	}
}

int AllInputDevicesInit(void)
{
	PT_InputOpr ptTmp = g_ptInputOprHead;
	int iError = -1;

	while (ptTmp)
	{
		if (0 == ptTmp->DeviceInit())
		{
			iError = 0;
		}
		ptTmp = ptTmp->ptNext;
	}
	return iError;
}

int GetInputEvent(PT_InputEvent ptInputEvent)
{
	/* 把链表中的InputOpr的GetInputEvent都调用一次,一旦有数据即返回 */
	
	PT_InputOpr ptTmp = g_ptInputOprHead;

	while (ptTmp)
	{
		if (0 == ptTmp->GetInputEvent(ptInputEvent))
		{
			return 0;
		}
		ptTmp = ptTmp->ptNext;
	}
	return -1;
}

int InputInit(void)
{
	int iError;
	iError = StdinInit();
	return iError;
}

