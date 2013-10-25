#include <stdio.h>
#include "../../Utility/Utility/CInitSock.h"

CInitSock initSock;

#define WM_SOCKET (WM_USER + 2)

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int main()
{
	char szClassName[] = "MainWClass";
	WNDCLASSEX wndclass;
	wndclass.cbSize = sizeof(wndclass);
	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = WindowProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = NULL;
	wndclass.hIcon = ::LoadIcon(NULL, IDI_APPLICATION);
	wndclass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)::GetStockObject(WHITE_BRUSH);
	wndclass.hIconSm = NULL;
	wndclass.lpszClassName = szClassName;
	wndclass.lpszMenuName = NULL;
	::RegisterClassEx(&wndclass);

	HWND hWnd = ::CreateWindowEx(
		0,
		szClassName,
		"",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		NULL,
		NULL);

	if(hWnd == NULL)
	{
		::MessageBox(NULL, "创建窗口出错！", "error", MB_OK);
		return -1;
	}

	u_short nPort = 4567;
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

	::WSAAsyncSelect(sListen, hWnd, WM_SOCKET, FD_ACCEPT | FD_CLOSE);
	::listen(sListen, 5);

	MSG msg;
	while(::GetMessage(&msg, NULL, 0, 0))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
	return msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_SOCKET:
		{
			SOCKET s = wParam;
			if(WSAGETSELECTERROR(lParam))
			{
				::closesocket(s);
				return 0;
			}

			switch(WSAGETSELECTEVENT(lParam))
			{
			case FD_ACCEPT:
				{
					SOCKET client = ::accept(s, NULL, NULL);
					::WSAAsyncSelect(client, hWnd, WM_SOCKET, FD_READ | FD_CLOSE);
				}
				break;

			case FD_WRITE:
				{

				}
				break;
			case FD_READ:
				{
					char szText[1024] = {0};
					if(::recv(s, szText, 1024, 0) == -1) ::closesocket(s);
					else printf("接收数据：%s\n", szText);
				}
				break;

			case FD_CLOSE:
				{
					::closesocket(s);	
				}
				break;
			}
		}
		return 0;

	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}

	return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
}