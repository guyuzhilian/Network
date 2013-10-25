#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initSock;

int main()
{
	WSAEVENT eventArray[WSA_MAXIMUM_WAIT_EVENTS];
	SOCKET sockArray[WSA_MAXIMUM_WAIT_EVENTS];

	int nEventTotal = 0;
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

	::listen(sListen, 5);

	WSAEVENT event = ::WSACreateEvent();
	::WSAEventSelect(sListen, event, FD_ACCEPT | FD_CLOSE);

	eventArray[nEventTotal] = event;
	sockArray[nEventTotal] = sListen;
	nEventTotal++;

	while(TRUE)
	{
		int nIndex = ::WSAWaitForMultipleEvents(nEventTotal, eventArray, FALSE, WSA_INFINITE, FALSE);
		nIndex -= WSA_WAIT_EVENT_0;
		for(int i = nIndex; i < nEventTotal; i++)
		{
			int nIndex = ::WSAWaitForMultipleEvents(1, &eventArray[i], TRUE, 1000, FALSE);
			if(nIndex == WSA_WAIT_FAILED || nIndex == WSA_WAIT_TIMEOUT)
			{
				continue;
			}
			else
			{
				WSANETWORKEVENTS event;
				::WSAEnumNetworkEvents(sockArray[i], eventArray[i], &event);
				if(event.lNetworkEvents & FD_ACCEPT)
				{
					if(event.iErrorCode[FD_ACCEPT_BIT] == 0)
					{
						if(nEventTotal > WSA_MAXIMUM_WAIT_EVENTS)
						{
							printf("Too many connections!\n");
							continue;
						}
						SOCKET sNew = ::accept(sockArray[i], NULL, NULL);
						WSAEVENT event = ::WSACreateEvent();
						::WSAEventSelect(sNew, event, FD_READ | FD_CLOSE | FD_WRITE);
						eventArray[nEventTotal] = event;
						sockArray[nEventTotal] = sNew;
						nEventTotal++;
					}
				}
				else if(event.lNetworkEvents & FD_READ)
				{
					if(event.iErrorCode[FD_READ_BIT] == 0)
					{
						char szText[256];
						int nRecv = ::recv(sockArray[i], szText, strlen(szText), 0);
						if(nRecv > 0)
						{
							szText[nRecv] = '\0';
							printf("接收到数据：%s\n", szText);
						}
					}
				}
				else if(event.lNetworkEvents & FD_CLOSE)
				{
					if(event.iErrorCode[FD_CLOSE_BIT] == 0)
					{
						::closesocket(sockArray[i]);
						for(int j = i; j < nEventTotal; j++)
						{
							sockArray[j] = sockArray[j+1];
							eventArray[j] = eventArray[j+1];
						}
						nEventTotal--;
					}
				}
				else if(event.lNetworkEvents & FD_WRITE)
				{

				}
			}
		}
	}

	return 0;
}