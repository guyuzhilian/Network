#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"
#include <IphlpApi.h>

#pragma comment(lib, "Iphlpapi.lib")

CInitSock initSock;

//全局数据
u_char		g_ucLocalMac[6];
DWORD		g_dwGatewayIP;
DWORD		g_dwLocalIP;
DWORD		g_dwMask;

BOOL GetGlobalData()
{
	PIP_ADAPTER_INFO pAdapterInfo = NULL;
	ULONG ulLen = 0;

	//为适配器结构申请内存
	::GetAdaptersInfo(pAdapterInfo, &ulLen);
	pAdapterInfo = (PIP_ADAPTER_INFO)::GlobalAlloc(GPTR, ulLen);

	//取得本地适配器结构信息
	if(::GetAdaptersInfo(pAdapterInfo, &ulLen) == ERROR_SUCCESS)
	{
		if(pAdapterInfo)
		{
			memcpy(g_ucLocalMac, pAdapterInfo->Address, 6);
			g_dwGatewayIP = ::inet_addr(pAdapterInfo->GatewayList.IpAddress.String);
			g_dwLocalIP = ::inet_addr(pAdapterInfo->IpAddressList.IpAddress.String);
			g_dwMask = ::inet_addr(pAdapterInfo->IpAddressList.IpMask.String);
		}
	}

	printf("\n-----------------本地主机信息-----------------\n");
	in_addr in;
	in.S_un.S_addr = g_dwLocalIP;
	printf("      IP Address:%s\n", ::inet_ntoa(in));

	in.S_un.S_addr = g_dwMask;
	printf("      Subnet Mask:%s\n", ::inet_ntoa(in));

	in.S_un.S_addr = g_dwGatewayIP;
	printf("      Default Gateway:%s\n", ::inet_ntoa(in));

	u_char *p = g_ucLocalMac;
	printf("      MAC Address:%02X-%02X-%02X-%02X-%02X-%02X\n", p[0], p[1], p[2], p[3], p[4], p[5]);

	printf("\n\n");
	return TRUE;
}

int main()
{
	GetGlobalData();
	return 0;
}