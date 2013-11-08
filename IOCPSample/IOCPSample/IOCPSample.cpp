#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initSock;

#define BUFFER_SIZE 1024

typedef struct _PER_HANDLE_DATA
{
	SOCKET s;			//��Ӧ���׽��־��
	sockaddr_in addr;	//�ͻ���ַ
} PER_HANDLE_DATA, *PPER_HANDLE_DATA;

typedef struct _PER_IO_DATA	//per-I/P����
{
	OVERLAPPED ol;		//�ص��ṹ
	char buf[BUFFER_SIZE];	//���ݻ�����
	int nOpertionType;	//��������
#define OP_READ 1
#define OP_WRITE 2
#define OP_ACCEPT 3
}PER_IO_DATA, *PPER_IO_DATA;

DWORD WINAPI ServerThread(LPVOID lpParam)
{
	//�õ���ɶ˿ڶ���ľ��
	HANDLE hCompletion = (HANDLE)lpParam;
	DWORD dwTrans;
	PPER_HANDLE_DATA pPerHandle;
	PPER_IO_DATA pPerIO;
	while(TRUE)
	{
		//�ڹ���������ɶ˿ڵ������׽����ϵȴ�I/P���
		BOOL bOK = ::GetQueuedCompletionStatus(hCompletion,
			&dwTrans, (LPDWORD)&pPerHandle, (LPOVERLAPPED*)&pPerIO, WSA_INFINITE);
		if(!bOK)
		{
			::closesocket(pPerHandle->s);
			::GlobalFree(pPerHandle);
			::GlobalFree(pPerIO);
			continue;
		}

		if(dwTrans == 0 &&	//�׽��ֱ��Է��ر�
			(pPerIO->nOpertionType == OP_READ || pPerIO->nOpertionType == OP_WRITE))
		{
			::closesocket(pPerHandle->s);
			::GlobalFree(pPerHandle);
			::GlobalFree(pPerIO);
			continue;
		}

		switch(pPerIO->nOpertionType)
		{
		case OP_READ:
			{
				pPerIO->buf[dwTrans] = '\0';
				printf(pPerIO->buf);
				//����Ͷ�ݽ���I/O����
				WSABUF buf;
				buf.buf = pPerIO->buf;
				buf.len = BUFFER_SIZE;
				pPerIO->nOpertionType = OP_READ;
				DWORD nFlags = 0;
				::WSARecv(pPerHandle->s, &buf, 1, &dwTrans, &nFlags, &pPerIO->ol, NULL);
			}
			break;
		case OP_WRITE:
		case OP_ACCEPT:
			break;
		}
	}
	return 0;
}

int main()
{
	int nPort = 4567;
	//������ɶ˿ڶ��󣬴��������̴߳�����ɶ˿ڶ����е��¼�
	HANDLE hCompletion = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	::CreateThread(NULL, 0, ServerThread, (LPVOID)hCompletion, 0, 0);
	//���������׽��֣��󶨵����ص�ַ����ʼ����
	SOCKET sListen = ::socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in si;
	si.sin_family = AF_INET;
	si.sin_port = htons(nPort);
	si.sin_addr.S_un.S_addr = INADDR_ANY;

	::bind(sListen, (sockaddr*)&si, sizeof(si));
	::listen(sListen, 5);

	//ѭ��������������
	while(TRUE)
	{
		//�ȴ�����δ������������
		sockaddr_in saRemote;
		int nRemoteLen = sizeof(saRemote);
		SOCKET sNew = ::accept(sListen, (sockaddr*)&saRemote, &nRemoteLen);
		//���ܵ�������֮��Ϊ������һ��per-handle���ݣ��������ǹ�������ɶ˿ڶ���
		PPER_HANDLE_DATA pPerHandler = 
			(PPER_HANDLE_DATA)::GlobalAlloc(GPTR, sizeof(PER_HANDLE_DATA));
		pPerHandler->s = sNew;
		memcpy(&pPerHandler->addr, &saRemote, nRemoteLen);
		::CreateIoCompletionPort((HANDLE)pPerHandler->s, hCompletion, (DWORD)pPerHandler, 0);
		//Ͷ��һ����������
		PPER_IO_DATA pPerIO = (PPER_IO_DATA)::GlobalAlloc(GPTR, sizeof(PER_IO_DATA));
		pPerIO->nOpertionType = OP_READ;
		WSABUF buf;
		buf.buf = pPerIO->buf;
		buf.len = BUFFER_SIZE;
		DWORD dwRecv;
		DWORD dwFlags = 0;
		::WSARecv(pPerHandler->s, &buf, 1, &dwRecv, &dwFlags, &pPerIO->ol, NULL);
	}
}