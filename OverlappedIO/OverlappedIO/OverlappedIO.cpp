#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"
#include <MSWSock.h>

#define BUFFERSIZE 512

typedef struct _SOCKET_OBJ
{
	SOCKET s;					//套接字句柄
	int nOutstandingOps;		//记录此套接字上的重叠I/)数量
	LPFN_ACCEPTEX lpfnAcceptEx;	//扩展函数AcceptEx的指针（仅对监听套接字而言）
}SOCKET_OBJ, *PSOCKET_OBJ;

PSOCKET_OBJ GetSocketObj(SOCKET s)
{
	PSOCKET_OBJ pSocket = (PSOCKET_OBJ)::GlobalAlloc(GPTR, sizeof(SOCKET_OBJ));
	if(pSocket != NULL)
		pSocket->s = s;
	return pSocket;
}

void FreeSocketObj(PSOCKET_OBJ pSocket)
{
	if(pSocket->s != INVALID_SOCKET)
		::closesocket(pSocket->s);
	::GlobalFree(pSocket);
}

typedef struct _BUFFER_OBJ
{
	OVERLAPPED ol;			//重叠结构
	char *buff;				//send/recv/AcceptEx所使用的缓冲区
	int nLen;				//buff的长度
	PSOCKET_OBJ pSocket;	//此I/O所属的套接字对象
	int nOperation;			//提交的操作类型
#define OP_ACCEPT	1
#define OP_READ		2
#define OP_WRITE	3
	SOCKET sAccept;			//用来保存AcceptEx接受的客户套接字（仅对监听套接字而言）
	_BUFFER_OBJ *pNext;
}BUFFER_OBJ, *PBUFFER_OBJ;

HANDLE g_events[WSA_MAXIMUM_WAIT_EVENTS];	//I/O事件句柄数组
int g_nBufferCount;							//上数组中有效句柄数量
PBUFFER_OBJ g_pBufferHead, g_pBufferTail;	//记录缓冲区对象组成的表的地址

PBUFFER_OBJ GetBufferObj(PSOCKET_OBJ pSocket, ULONG nLen)
{
	if(g_nBufferCount > WSA_MAXIMUM_WAIT_EVENTS-1)
		return NULL;
	PBUFFER_OBJ pBuffer = (PBUFFER_OBJ)::GlobalAlloc(GPTR, sizeof(BUFFER_OBJ));
	if(pBuffer != NULL)
	{
		pBuffer->buff = (char*)::GlobalAlloc(GPTR, nLen);
		pBuffer->ol.hEvent = ::WSACreateEvent();
		pBuffer->pSocket = pSocket;
		pBuffer->sAccept = INVALID_SOCKET;

		//将新的BUFFER_OBJ添加到列表中
		if(g_pBufferHead == NULL)
			g_pBufferHead = g_pBufferTail = pBuffer;
		else
		{
			g_pBufferTail->pNext = pBuffer;
			g_pBufferTail = pBuffer;
		}
		g_events[++g_nBufferCount] = pBuffer->ol.hEvent;
	}
	return pBuffer;
}

void FreeBufferObj(PBUFFER_OBJ pBuffer)
{
	//从列表中移除BUFFER_OBJ对象
	PBUFFER_OBJ pTest = g_pBufferHead;
	BOOL bFind = FALSE;
	if(pTest == pBuffer)
	{
		g_pBufferHead = g_pBufferTail = NULL;
		bFind = TRUE;
	}
	else
	{
		while(pTest != NULL && pTest->pNext != pBuffer)
			pTest = pTest->pNext;
		if(pTest != NULL)
		{
			pTest->pNext = pBuffer->pNext;
			if(pTest->pNext == NULL)
				g_pBufferTail = pTest;
			bFind = TRUE;
		}
	}

	if(bFind)
	{
		g_nBufferCount--;
		::CloseHandle(pBuffer->ol.hEvent);
		::GlobalFree(pBuffer->buff);
		::GlobalFree(pBuffer);
	}
}

PBUFFER_OBJ FindBufferObj(HANDLE hEvent)
{
	PBUFFER_OBJ pBuffer = g_pBufferHead;
	while(pBuffer != NULL)
	{
		if(pBuffer->ol.hEvent == hEvent)
			break;
		pBuffer = pBuffer->pNext;
	}
	return pBuffer;
}

void RebuildArray()
{
	PBUFFER_OBJ pBuffer = g_pBufferHead;
	int i = 1;
	while(pBuffer != NULL)
	{
		g_events[i++] = pBuffer->ol.hEvent;
		pBuffer = pBuffer->pNext;
	}
}

BOOL PostAccept(PBUFFER_OBJ pBuffer)
{
	PSOCKET_OBJ pSocket = pBuffer->pSocket;
	if(pSocket->lpfnAcceptEx != NULL)
	{
		//设置I/O类型，增加套接字上的重叠I/O技术
		pBuffer->nOperation = OP_ACCEPT;
		pSocket->nOutstandingOps++;

		//投递此重叠I/O
		DWORD dwBytes;
		pBuffer->sAccept =
			::WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

		BOOL b = pSocket->lpfnAcceptEx( pSocket->s,
			pBuffer->sAccept,
			pBuffer->buff,
			BUFFERSIZE-((sizeof(sockaddr_in)+16)*2),
			sizeof(sockaddr_in)+16,
			sizeof(sockaddr_in)+16,
			&dwBytes,
			&pBuffer->ol);
		if(!b)
		{
			if(::WSAGetLastError() != WSA_IO_PENDING)
				return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

BOOL PostRecv(PBUFFER_OBJ pBuffer)
{
	//设置I/O类型，增加套接字上的重叠I/O计数
	pBuffer->nOperation = OP_READ;
	pBuffer->pSocket->nOutstandingOps++;

	//投递此重叠I/O
	DWORD dwBytes;
	DWORD dwFlags = 0;
	WSABUF buf;
	buf.buf = pBuffer->buff;
	buf.len = pBuffer->nLen;
	if(::WSARecv(pBuffer->pSocket->s, &buf, 1, &dwBytes, &dwFlags, &pBuffer->ol, NULL) != NO_ERROR)
	{
		if(::WSAGetLastError() != WSA_IO_PENDING)
			return FALSE;
	}

	return TRUE;
}

BOOL PostSend(PBUFFER_OBJ pBuffer)
{
	//设置I/O类型，增加套接字上的重叠I/O计数
	pBuffer->nOperation = OP_WRITE;
	pBuffer->pSocket->nOutstandingOps++;

	DWORD dwBytes;
	DWORD dwFlags = 0;
	WSABUF buf;
	buf.buf = pBuffer->buff;
	buf.len = pBuffer->nLen;
	if(::WSASend(pBuffer->pSocket->s,
		&buf, 1, &dwBytes, dwFlags, &pBuffer->ol, NULL) != NO_ERROR)
	{
		if(::WSAGetLastError() != WSA_IO_PENDING)
			return FALSE;
	}
	return TRUE;
}

BOOL HandleIO(PBUFFER_OBJ pBuffer)
{
	PSOCKET_OBJ pSocket = pBuffer->pSocket;	//从BUFFER_OBJ对象中提取SOCKET_OBJ对象指针
											//为的是方便引用
	pSocket->nOutstandingOps--;
	//获得重叠操作结果
	DWORD dwTrans;
	DWORD dwFlags;
	BOOL bRet = ::WSAGetOverlappedResult(pSocket->s, &pBuffer->ol, &dwTrans, FALSE, &dwFlags);
	if(!bRet)
	{
		//在此套接字上有错误发生，因此，关闭套接字，移除此缓冲区对象
		//如果没有其他抛出的I/O请求了，释放此缓冲区对象，否则，等待此套接字上的其他I/O也wancheng
		if(pSocket->s != INVALID_SOCKET)
		{
			::closesocket(pSocket->s);
			pSocket->s = INVALID_SOCKET;
		}

		if(pSocket->nOutstandingOps == 0)
			FreeSocketObj(pSocket);

		FreeBufferObj(pBuffer);
		return FALSE;
	}

	//没有错误发生，处理已完成的I/O
	switch(pBuffer->nOperation)
	{
	case OP_ACCEPT://接受到一个连接
		{
			//为新客户创建一个SOCKET_OBJ对象
			PSOCKET_OBJ pClient = GetSocketObj(pBuffer->sAccept);
			//为发送数据创建一个BUFFER_OBJ对象，这个对象会在套接字出错或者关闭时释放
			PBUFFER_OBJ pSend = GetBufferObj(pClient, BUFFERSIZE);
			if(pSend == NULL)
			{
				printf("Too much connections!\n");
				FreeSocketObj(pClient);
				return FALSE;
			}

			RebuildArray();
			//将数据复制到发送缓冲区
			pSend->nLen = dwTrans;
			memcpy(pSend->buff, pBuffer->buff, dwTrans);
			//投递此发送I/O（将数据回显给客户）
			if(!PostSend(pSend))
			{
				FreeSocketObj(pSocket);
				FreeBufferObj(pSend);
				return FALSE;
			}

			//继续投递接受I/O
			PostAccept(pBuffer);
		}
		break;
	case OP_READ:	//接收数据完成
		{
			if(dwTrans > 0)
			{
				//创建一个缓冲区，以发送数据。这里就是用原来的缓冲区
				PBUFFER_OBJ pSend = pBuffer;
				pSend->nLen = dwTrans;
				//投递发送I/O（将数据回显给客户）
				PostSend(pSend);
			}
			else	//套接字关闭
			{
				//必须先关闭套接字，以便在此套接字上投递的其他I/O也返回
				if(pSocket->s != INVALID_SOCKET)
				{
					::closesocket(pSocket->s);
					pSocket->s = INVALID_SOCKET;
				}

				if(pSocket->nOutstandingOps == 0)
					FreeSocketObj(pSocket);
				FreeBufferObj(pBuffer);
				return FALSE;
			}
		}
		break;
	case OP_WRITE:	//发送数据完成
		{
			if(dwTrans > 0)
			{
				//接续使用这个缓冲区投递接受数据
				pBuffer->nLen = BUFFERSIZE;
				PostRecv(pBuffer);
			}
			else
			{
				if(pSocket->s != INVALID_SOCKET)
				{
					::closesocket(pSocket->s);
					pSocket->s = INVALID_SOCKET;
				}
				if(pSocket->nOutstandingOps == 0)
					FreeSocketObj(pSocket);
				FreeBufferObj(pBuffer);
				return FALSE;
			}
		}
		break;
	}
	return TRUE;
}

int main()
{
	int nPort = 4567;
	SOCKET sListen = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	sockaddr_in si;
	si.sin_family = AF_INET;
	si.sin_port = ::htons(nPort);
	si.sin_addr.S_un.S_addr = INADDR_ANY;
	::bind(sListen, (sockaddr*)&si, sizeof(si));
	::listen(sListen, 200);

	//为监听套接字创建一个SOCKET_OBJ对象
	PSOCKET_OBJ pListen = GetSocketObj(sListen);
	//加载扩展函数AcceptEx
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	DWORD dwBytes;
	WSAIoctl(pListen->s,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&pListen->lpfnAcceptEx,
		sizeof(pListen->lpfnAcceptEx),
		&dwBytes,
		NULL,
		NULL);

	//创建用来重新建立g_events数组的事件对象
	g_events[0] = ::WSACreateEvent();
	//在此可以投递多个接受I/O请求
	for(int i = 0; i < 5; i++)
	{
		PostAccept(GetBufferObj(pListen, BUFFERSIZE));
	}

	while(TRUE)
	{
		int nIndex = ::WSAWaitForMultipleEvents(g_nBufferCount+1, g_events, FALSE, WSA_INFINITE,FALSE);
		if(nIndex == WSA_WAIT_FAILED)
		{
			printf("WSAWaitForMultipleEvents() failed\n");
			break;
		}

		nIndex = nIndex - WSA_WAIT_EVENT_0;
		for(int i = nIndex; i <= g_nBufferCount; i++)
		{
			int nRet = ::WSAWaitForMultipleEvents(1, &g_events[i], TRUE, 0, FALSE);
			if(nRet == WSA_WAIT_TIMEOUT)
				continue;
			else
			{
				::WSAResetEvent(g_events[i]);
				if(i == 0)
				{
					RebuildArray();
					continue;
				}
				//处理这个I/O
				PBUFFER_OBJ pBuffer = FindBufferObj(g_events[i]);
				if(pBuffer != NULL)
				{
					if(!HandleIO(pBuffer))
						RebuildArray();
				}
			}
		}
	}
}