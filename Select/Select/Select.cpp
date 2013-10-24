#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initSock;

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
		return 0;
	}

	::listen(sListen, 5);

	fd_set fdSocket;
	FD_ZERO(&fdSocket);
	FD_SET(sListen, &fdSocket);
	while(TRUE)
	{
		fd_set fdRead = fdSocket;
		int nRet = ::select(0, &fdRead, NULL, NULL, NULL);
		if(nRet > 0)
		{
			for(int i = 0; i < (int)fdRead.fd_count; i++)
			{
				if(fdRead.fd_array[i] == sListen)
				{
					if(fdSocket.fd_count < FD_SETSIZE)
					{
						sockaddr_in addrRemote;
						int nAddrLen = sizeof(addrRemote);
						SOCKET sNew = ::accept(sListen, (sockaddr*)&addrRemote, &nAddrLen);
						FD_SET(sNew, &fdSocket);
						printf("接收到连接（%s）\n", ::inet_ntoa(addrRemote.sin_addr));
					}
					else
					{
						printf("Too much connections!\n");
						continue;
					}
				}
				else
				{
					char szText[256];
					int nRecv = ::recv(fdRead.fd_array[i], szText, strlen(szText), 0);
					if(nRecv > 0)
					{
						szText[nRecv] = '\0';
						printf("接收到数据：%s\n", szText);
					}
					else
					{
						::closesocket(fdRead.fd_array[i]);
						FD_CLR(fdRead.fd_array[i], &fdSocket);
					}
				}
			}
		}

		else
		{
			printf("Failed select()\n");
			break;
		}
	}
	return 0;
}