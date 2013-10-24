#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initSock;

int main()
{
	SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(s == INVALID_SOCKET)
	{
		printf("Failed socket()\n");
		return 0;
	}

	sockaddr_in servAddr;
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(4567);
	servAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");

	if(::connect(s, (sockaddr*)&servAddr, sizeof(servAddr)) == SOCKET_ERROR)
	{
		printf("Failed connect()\n");
		return 0;
	}

	char buff[] = "hello world£¡";
	::send(s, buff, strlen(buff), 0);

	system("pause");
	::closesocket(s);
	return 0;
}