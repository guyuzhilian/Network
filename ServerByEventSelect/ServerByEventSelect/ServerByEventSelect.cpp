#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initsock;
#define MAX_SOCKET_NUM 4

//客户端套接字信息
typedef struct _SOCKET_OBJ
{
	SOCKET s;					//套接字句柄
	HANDLE event;				//与此套接字相关联的事件对象句柄
	sockaddr_in addrRemote;		//客户端地址信息
	_SOCKET_OBJ *pNext;			//指向下一个SOCKET_OBJ对象，以连成一个表
}SOCKET_OBJ, *PSOCKET_OBJ;

//申请一个套接字对象，初始化它的成员
PSOCKET_OBJ GetSocketObj(SOCKET s)
{
	PSOCKET_OBJ pSocket = (PSOCKET_OBJ)::GlobalAlloc(GPTR, sizeof(SOCKET_OBJ));
	if(pSocket != NULL)
	{
		pSocket->s = s;
		pSocket->event = ::WSACreateEvent();
	}
	return pSocket;
}

void FreeSocketObj(PSOCKET_OBJ pSocket)
{
	::CloseHandle(pSocket->event);
	if(pSocket->s != INVALID_SOCKET)
	{
		::closesocket(pSocket->s);
	}
	GlobalFree(pSocket);
}

typedef struct _THREAD_OBJ
{
	HANDLE events[MAX_SOCKET_NUM];		//记录当前线程要等待的事件对象的句柄
	int nSocketCount;							//记录当前线程处理的套接字数量 <= MAX_SOCKET_NUM
	PSOCKET_OBJ pSockHeader;					//当前线程处理的套接字对象列表，pSockHeader指向表头
	PSOCKET_OBJ pSockTail;						//pSockTail指向表尾
	CRITICAL_SECTION cs;						//同步
	_THREAD_OBJ *pNext;							//指向下一个THREAD_OBJ对象
}THREAD_OBJ, *PTHREAD_OBJ;

PTHREAD_OBJ g_pThreadList;	//线程列表表头
CRITICAL_SECTION g_cs;		//同步

//申请一个线程对象，初始化它的成员，并将它添加到线程对象列表
PTHREAD_OBJ GetThreadObj()
{
	PTHREAD_OBJ pThread = (PTHREAD_OBJ)::GlobalAlloc(GPTR, sizeof(THREAD_OBJ));
	if(pThread != NULL)
	{
		::InitializeCriticalSection(&pThread->cs);
		//创建一个事件对象，用于指示该线程的句柄数据需要重建
		pThread->events[0] = ::WSACreateEvent();
		//将心申请的线程对象添加到列表中
		::EnterCriticalSection(&g_cs);
		pThread->pNext = g_pThreadList;
		g_pThreadList = pThread;
		::LeaveCriticalSection(&g_cs);
	}
	return pThread;
}

//释放一个线程对象，并将它从线程对象列表中删除
void FreeThreadObj(PTHREAD_OBJ pThread)
{
	//在线程对象列表中查找pThread所指的对象
	::EnterCriticalSection(&g_cs);
	PTHREAD_OBJ p = g_pThreadList;
	if(p == pThread)
	{
		g_pThreadList = p->pNext;
	}
	else
	{
		while(p != NULL && p->pNext != pThread)
		{
			p = p->pNext;
		}
		if(p != NULL)
		{
			p->pNext = pThread->pNext;
		}
	}
	::LeaveCriticalSection(&g_cs);

	::CloseHandle(pThread->events[0]);
	::DeleteCriticalSection(&pThread->cs);
	::GlobalFree(pThread);
}

void RebuildArray(PTHREAD_OBJ pThread)
{
	::EnterCriticalSection(&pThread->cs);
	PSOCKET_OBJ pSocket = pThread->pSockHeader;
	int n = 1;
	while(pSocket != NULL)
	{
		pThread->events[n++] = pSocket->event;
		pSocket = pSocket->pNext;
	}
	::LeaveCriticalSection(&pThread->cs);
}

LONG g_nTotalConnections;	//总共连接数
LONG g_nCurrentConnections;	//当前连接数

//向同一个线程的套接字列表中插入一个套接字
BOOL InsertSocketObj(PTHREAD_OBJ pThread, PSOCKET_OBJ pSocket)
{
	BOOL bRet = FALSE;
	::EnterCriticalSection(&pThread->cs);
	if(pThread->nSocketCount < MAX_SOCKET_NUM-1)
	{
		if(pThread->pSockHeader == NULL)
		{
			pThread->pSockHeader = pThread->pSockTail = pSocket;
		}
		else
		{
			pThread->pSockTail->pNext = pSocket;
			pThread->pSockTail = pSocket;
		}
		pThread->nSocketCount++;
		bRet = TRUE;
	}
	::LeaveCriticalSection(&pThread->cs);

	//插入成功，说明成功处理了客户的连接请求
	if(bRet)
	{
		::InterlockedIncrement(&g_nTotalConnections);
		::InterlockedIncrement(&g_nCurrentConnections);
	}
	return bRet;
}

//将一个套接字对象安排给空闲的线程处理
void AssignToFreeThread(PSOCKET_OBJ pSocket)
{
	DWORD _stdcall ServerThread(LPVOID);
	pSocket->pNext = NULL;
	::EnterCriticalSection(&g_cs);
	PTHREAD_OBJ pThread = g_pThreadList;
	//试图插入现存线程
	while(pThread != NULL)
	{
		if(InsertSocketObj(pThread, pSocket)) break;
		pThread = pThread->pNext;
	}

	//没有空闲线程，为该套接字创建线程
	if(pThread == NULL)
	{
		pThread = GetThreadObj();
		InsertSocketObj(pThread, pSocket);
		::CreateThread(NULL, 0, ServerThread, pThread, 0, NULL);
	}
	::LeaveCriticalSection(&g_cs);
	::WSASetEvent(pThread->events[0]);
}

//从给定线程的套接字对象列表中移除一个套接字对象
void RemoveSocketObj(PTHREAD_OBJ pThread, PSOCKET_OBJ pSocket)
{
	::EnterCriticalSection(&pThread->cs);

	//在套接字对象列表中查找指定的套接字对象，
	PSOCKET_OBJ pTest = pThread->pSockHeader;
	if(pTest == pSocket)
	{
		if(pThread->pSockHeader == pThread->pSockTail)
			pThread->pSockHeader = pThread->pSockTail = pTest->pNext;
		else
			pThread->pSockHeader = pTest->pNext;
	}
	else
	{
		while(pTest != NULL && pTest->pNext != pSocket)
			pTest = pTest->pNext;

		if(pTest != NULL)
		{
			if(pThread->pSockTail == pSocket) pThread->pSockTail = pTest;
			pTest->pNext = pSocket->pNext;
		}
	}
	pThread->nSocketCount--;

	::LeaveCriticalSection(&pThread->cs);

	::WSASetEvent(pThread->events[0]);
	::InterlockedDecrement(&g_nCurrentConnections);
}

PSOCKET_OBJ FindSocketObj(PTHREAD_OBJ pThread, int nIndex)
{
	PSOCKET_OBJ pSocket = pThread->pSockHeader;
	while(--nIndex)
	{
		if(pSocket == NULL)
			return NULL;
		pSocket = pSocket->pNext;
	}
	return pSocket;
}

BOOL HandleIO(PTHREAD_OBJ pThread, PSOCKET_OBJ pSocket)
{
	WSANETWORKEVENTS event;
	::WSAEnumNetworkEvents(pSocket->s, pSocket->event, &event);
	do 
	{
		if(event.lNetworkEvents & FD_READ)
		{
			if(event.iErrorCode[FD_READ_BIT] == 0)
			{
				char szText[256];
				int nRecv = ::recv(pSocket->s, szText, strlen(szText), 0);
				if(nRecv > 0)
				{
					szText[nRecv] = '\0';
					printf("接收到数据：%s\n", szText);
				}
			}
			else
				break;
		}
		else if(event.lNetworkEvents & FD_CLOSE)
		{
			break;
		}
		else if(event.lNetworkEvents & FD_WRITE)
		{
			if(event.iErrorCode[FD_WRITE_BIT] == 0)
			{

			}
			else
				break;
		}
		return TRUE;
	} while (FALSE);

	//套接字关闭或者有错误发生，程序都会转到这儿执行
	RemoveSocketObj(pThread, pSocket);
	FreeSocketObj(pSocket);
	return FALSE;
}

DWORD WINAPI ServerThread(LPVOID lpParam)
{
	//取得本线程对象的指针
	PTHREAD_OBJ pThread = (PTHREAD_OBJ)lpParam;
	while(TRUE)
	{
		int nIndex = ::WSAWaitForMultipleEvents(pThread->nSocketCount+1, pThread->events, FALSE, WSA_INFINITE, FALSE);
		nIndex -= WSA_WAIT_EVENT_0;
		for(int i = nIndex; i < pThread->nSocketCount; i++)
		{
			nIndex = ::WSAWaitForMultipleEvents(1, &pThread->events[i], TRUE, 1000, FALSE);
			if(nIndex == WSA_WAIT_FAILED || nIndex == WSA_WAIT_TIMEOUT)
			{
				continue;
			}
			else
			{
				if(i == 0)
				{
					RebuildArray(pThread);
					if(pThread->nSocketCount == 0)
					{
						FreeThreadObj(pThread);
						return 0;
					}
					::WSAResetEvent(pThread->events[0]);
				}
				else
				{
					PSOCKET_OBJ pSocket = (PSOCKET_OBJ)FindSocketObj(pThread, i);
					if(pSocket != NULL)
					{
						if(!HandleIO(pThread, pSocket))
							RebuildArray(pThread);
					}
					else
					{
						printf("Unable to find socket object\n");
					}
				}
			}
		}
	}
	return 0;
}

int main()
{
	USHORT nPort = 4567;
	SOCKET sListen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(nPort);
	sin.sin_addr.S_un.S_addr = INADDR_ANY;
	if(::bind(sListen, (sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR)
	{
		printf("Failed bind()\n");
		return -1;
	}
	::listen(sListen, 200);

	WSAEVENT event = ::WSACreateEvent();
	::WSAEventSelect(sListen, event, FD_ACCEPT | FD_CLOSE);
	::InitializeCriticalSection(&g_cs);
	while(TRUE)
	{
		int nRet = ::WaitForSingleObject(event, 5000);
		if(nRet == WAIT_FAILED)
		{
			printf("Failed WaitForSingleObject()\n");
			break;
		}
		else if(nRet == WSA_WAIT_TIMEOUT)
		{
			printf("\n");
			printf("    TotalConnections:%d\n", g_nTotalConnections);;
			printf("    CurrentConnections:%d\n", g_nCurrentConnections);
			continue;
		}
		else
		{
			::ResetEvent(event);
			while(TRUE)
			{
				sockaddr_in si;
				int nLen = sizeof(si);
				SOCKET sNew = ::accept(sListen, (sockaddr*)&si, &nLen);
				if(sNew == SOCKET_ERROR)
					break;
				PSOCKET_OBJ pSocket = GetSocketObj(sNew);
				pSocket->addrRemote = si;
				::WSAEventSelect(pSocket->s, pSocket->event, FD_READ | FD_CLOSE | FD_WRITE);
				AssignToFreeThread(pSocket);
			}
		}
	}
	::DeleteCriticalSection(&g_cs);
	return 0;
}