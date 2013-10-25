#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initsock;
#define MAX_SOCKET_NUM 4

//�ͻ����׽�����Ϣ
typedef struct _SOCKET_OBJ
{
	SOCKET s;					//�׽��־��
	HANDLE event;				//����׽�����������¼�������
	sockaddr_in addrRemote;		//�ͻ��˵�ַ��Ϣ
	_SOCKET_OBJ *pNext;			//ָ����һ��SOCKET_OBJ����������һ����
}SOCKET_OBJ, *PSOCKET_OBJ;

//����һ���׽��ֶ��󣬳�ʼ�����ĳ�Ա
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
	HANDLE events[MAX_SOCKET_NUM];		//��¼��ǰ�߳�Ҫ�ȴ����¼�����ľ��
	int nSocketCount;							//��¼��ǰ�̴߳�����׽������� <= MAX_SOCKET_NUM
	PSOCKET_OBJ pSockHeader;					//��ǰ�̴߳�����׽��ֶ����б�pSockHeaderָ���ͷ
	PSOCKET_OBJ pSockTail;						//pSockTailָ���β
	CRITICAL_SECTION cs;						//ͬ��
	_THREAD_OBJ *pNext;							//ָ����һ��THREAD_OBJ����
}THREAD_OBJ, *PTHREAD_OBJ;

PTHREAD_OBJ g_pThreadList;	//�߳��б��ͷ
CRITICAL_SECTION g_cs;		//ͬ��

//����һ���̶߳��󣬳�ʼ�����ĳ�Ա����������ӵ��̶߳����б�
PTHREAD_OBJ GetThreadObj()
{
	PTHREAD_OBJ pThread = (PTHREAD_OBJ)::GlobalAlloc(GPTR, sizeof(THREAD_OBJ));
	if(pThread != NULL)
	{
		::InitializeCriticalSection(&pThread->cs);
		//����һ���¼���������ָʾ���̵߳ľ��������Ҫ�ؽ�
		pThread->events[0] = ::WSACreateEvent();
		//����������̶߳�����ӵ��б���
		::EnterCriticalSection(&g_cs);
		pThread->pNext = g_pThreadList;
		g_pThreadList = pThread;
		::LeaveCriticalSection(&g_cs);
	}
	return pThread;
}

//�ͷ�һ���̶߳��󣬲��������̶߳����б���ɾ��
void FreeThreadObj(PTHREAD_OBJ pThread)
{
	//���̶߳����б��в���pThread��ָ�Ķ���
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

LONG g_nTotalConnections;	//�ܹ�������
LONG g_nCurrentConnections;	//��ǰ������

//��ͬһ���̵߳��׽����б��в���һ���׽���
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

	//����ɹ���˵���ɹ������˿ͻ�����������
	if(bRet)
	{
		::InterlockedIncrement(&g_nTotalConnections);
		::InterlockedIncrement(&g_nCurrentConnections);
	}
	return bRet;
}

//��һ���׽��ֶ����Ÿ����е��̴߳���
void AssignToFreeThread(PSOCKET_OBJ pSocket)
{
	DWORD _stdcall ServerThread(LPVOID);
	pSocket->pNext = NULL;
	::EnterCriticalSection(&g_cs);
	PTHREAD_OBJ pThread = g_pThreadList;
	//��ͼ�����ִ��߳�
	while(pThread != NULL)
	{
		if(InsertSocketObj(pThread, pSocket)) break;
		pThread = pThread->pNext;
	}

	//û�п����̣߳�Ϊ���׽��ִ����߳�
	if(pThread == NULL)
	{
		pThread = GetThreadObj();
		InsertSocketObj(pThread, pSocket);
		::CreateThread(NULL, 0, ServerThread, pThread, 0, NULL);
	}
	::LeaveCriticalSection(&g_cs);
	::WSASetEvent(pThread->events[0]);
}

//�Ӹ����̵߳��׽��ֶ����б����Ƴ�һ���׽��ֶ���
void RemoveSocketObj(PTHREAD_OBJ pThread, PSOCKET_OBJ pSocket)
{
	::EnterCriticalSection(&pThread->cs);

	//���׽��ֶ����б��в���ָ�����׽��ֶ���
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
					printf("���յ����ݣ�%s\n", szText);
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

	//�׽��ֹرջ����д����������򶼻�ת�����ִ��
	RemoveSocketObj(pThread, pSocket);
	FreeSocketObj(pSocket);
	return FALSE;
}

DWORD WINAPI ServerThread(LPVOID lpParam)
{
	//ȡ�ñ��̶߳����ָ��
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