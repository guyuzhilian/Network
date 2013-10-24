#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initSock;

int main()
{
	//�����׽���
	SOCKET sListen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sListen == INVALID_SOCKET)
	{
		printf("Failed socket()\n");
		return 0;
	}

	//���sockaddr_in�ṹ
	sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(4567);
	sin.sin_addr.S_un.S_addr = INADDR_ANY;
	//������׽��ֵ�һ�����ص�ַ
	if(::bind(sListen, (sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR)
	{
		printf("Failed bind()\n");
		return 0;
	}

	//�������ģʽ
	if(::listen(sListen, 2) == SOCKET_ERROR)
	{
		printf("Failed listen()\n");
		return 0;
	}

	//ѭ�����ܿͻ�����������
	sockaddr_in remoteAddr;
	int nAddrLen = sizeof(remoteAddr);
	SOCKET sClient;
	char szText[] = "TCP Server Demo!\r\n";
	while(TRUE)
	{
		//����������
		sClient = ::accept(sListen, (sockaddr*)&remoteAddr, &nAddrLen);
		if(sClient == INVALID_SOCKET)
		{
			printf("Failed accept()\n");
			continue;
		}

		printf("���ܵ�һ�����ӣ�%s\r\n", inet_ntoa(remoteAddr.sin_addr));
		
		//��ͻ��˷�������
		::send(sClient, szText, strlen(szText), 0);
		//�رտͻ��˵�����
		::closesocket(sClient);
	}

	//�رռ����׽���
	::closesocket(sListen);
	return 0;
}