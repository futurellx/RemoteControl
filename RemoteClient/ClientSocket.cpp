#include "pch.h"
#include "ClientSocket.h"
#include <memory>

CClientSocket* CClientSocket::m_instance = NULL;
CClientSocket::CHelper CClientSocket::m_helper;

CClientSocket* pclient = CClientSocket::getInstance();

std::string GetErrInfo(int wsaErrCode)
{
	std::string ret;
	LPVOID lpMsgBuf = NULL;
	FormatMessageA( //错误码进行格式化的函数
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL,
		wsaErrCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&lpMsgBuf, 0, NULL);
	if (lpMsgBuf) ret = (char*)lpMsgBuf;
	LocalFree(lpMsgBuf);
	return ret;
}

void Dump(BYTE* pData, size_t nSize)
{
	std::string strOut;
	for (size_t i = 0; i < nSize; i++)
	{
		char buf[8] = "";
		if (i > 0 && (i % 16 == 0)) strOut += "\n";
		snprintf(buf, sizeof(buf), "%02X ", pData[i] & 0xFF);
		strOut += buf;
	}
	strOut += "\n";
	OutputDebugStringA(strOut.c_str());
}
CClientSocket::CClientSocket(const CClientSocket& ss) 
{
	m_hThread = INVALID_HANDLE_VALUE;
	m_bAutoClose = ss.m_bAutoClose;
	m_sock = ss.m_sock.load();
	m_nIP = ss.m_nIP;
	m_nPort = ss.m_nPort;
	std::map<UINT, CClientSocket::MSGFUNC>::const_iterator it = ss.m_mapFunc.begin();
	for (; it != ss.m_mapFunc.end(); it++) {
		m_mapFunc.insert(std::pair<UINT, MSGFUNC>(it->first, it->second));
	}
}
CClientSocket::CClientSocket():
	m_nIP(INADDR_ANY), m_nPort(0), m_sock(INVALID_SOCKET), m_bAutoClose(true),
	m_hThread(INVALID_HANDLE_VALUE)
{
	if (InitSockEnv() == FALSE) {
		MessageBox(NULL, _T("无法初始化套接字环境,请检查网络设置！"), _T("初始化错误！"), MB_OK | MB_ICONERROR);//MB_OK | MB_ICONERROR 结合起来表示消息框会显示一个包含“确定”按钮和一个错误图标的消息框
		exit(0);
	}
	m_buffer.resize(BUFFER_SIZE);
	struct {
		UINT message;
		MSGFUNC func;
	}funcs[] = {
		{WM_SEND_PACK,&CClientSocket::SendPack},
		//{WM_SEND_PACK,&CClientSocket::SendPack},
		{0,NULL}
	};
	for (int i = 0; funcs[i].message != 0; i++) {
		if (m_mapFunc.insert(std::pair<UINT, MSGFUNC>(funcs[i].message, funcs[i].func)).second == false) {
			TRACE("插入失败，消息值：%d 函数值:%08X 序号:%d\r\n", funcs[i].message, funcs[i].func, i);
		}
	}
	m_eventInvoke = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hThread = (HANDLE)_beginthreadex(NULL, 0, &CClientSocket::threadEntry, this, 0, &m_nThreadID);
	if (WaitForSingleObject(m_eventInvoke, INFINITE) == WAIT_TIMEOUT) {
		TRACE("网络消息处理线程启动失败了!\r\n");
	}
	CloseHandle(m_eventInvoke);

}
bool CClientSocket::InitSocket()
{
	std::lock_guard<std::mutex> guard(m_lock);
	if (m_stopping) return false; //检查初始化有没有成功
	if (m_sock != INVALID_SOCKET)CloseSocket();
	m_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (m_sock == -1) return false;
	//TODO:校验
	sockaddr_in serv_adr;
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	TRACE("addr %08X nIP %08X\r\n", htonl(INADDR_LOOPBACK), m_nIP);
	serv_adr.sin_addr.s_addr = htonl(m_nIP); //问题所在 IP地址出现错误
	serv_adr.sin_port = htons(m_nPort); //一开始顺序不一样
	if (serv_adr.sin_addr.s_addr == INADDR_NONE) {
		TRACE(_T("指定的IP地址，不存在！"));
		return false;
	}
	DWORD timeout = 5000;
	setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
	int ret = connect(m_sock, (sockaddr*)&serv_adr, sizeof(serv_adr));
	if (ret == -1) {
		TRACE(_T("连接失败！"));
		TRACE("连接失败：%d %s\r\n", WSAGetLastError(), GetErrInfo(WSAGetLastError()).c_str());
		return false;
	}
	TRACE("socket init done!\r\n");
	return true;
}

bool CClientSocket::SendPacket(HWND hWnd, const CPacket& pack, bool isAutoClosed,WPARAM wParam)
{
	if (m_stopping) return false;
	UINT nMode = isAutoClosed ? CSM_AUTOCLOSE : 0;
	std::string strOut;
	pack.Data(strOut);
	PACKET_DATA* pData = new PACKET_DATA(strOut.c_str(), strOut.size(), nMode, wParam);
	bool ret = PostThreadMessage(m_nThreadID, WM_SEND_PACK, (WPARAM)pData, (LPARAM)hWnd);
	if (ret == false) {
		delete pData;
	}
	return ret;
}
/*
bool CClientSocket::SendPacket(const CPacket& pack, std::list<CPacket>& lstPacks, bool isAutoClosed)
{
	if (m_sock == INVALID_SOCKET && m_hThread == INVALID_HANDLE_VALUE) {
		//	if (InitSocket() == false) return false;
		m_hThread =(HANDLE) _beginthread(&CClientSocket::threadEntry, 0, this);
		TRACE("start thread\r\n");
	}

	m_lock.lock();
	auto pr = m_mapAck.insert(std::pair<HANDLE, std::list<CPacket>&>(pack.hEvent,lstPacks));
	m_mapAutoClosed.insert(std::pair<HANDLE, bool>(pack.hEvent, isAutoClosed));
	m_lstSend.push_back(pack); //保证是一个原子操作，和其他线程不会同步
	m_lock.unlock();
	TRACE("cmd:%d  event %08X thread id%d\r\n", pack.sCmd, pack.hEvent, GetCurrentThreadId());
	WaitForSingleObject(pack.hEvent, INFINITE);
	TRACE("cmd:%d  event %08X thread id%d\r\n", pack.sCmd, pack.hEvent, GetCurrentThreadId());
	std::map<HANDLE, std::list<CPacket>&>::iterator it;
	it = m_mapAck.find(pack.hEvent);
	if (it != m_mapAck.end()) {
		m_lock.lock();
		m_mapAck.erase(it);
		m_lock.unlock();
		return true;
	}
	return false;
}
*/
unsigned CClientSocket::threadEntry(void* arg)
{
	CClientSocket* thiz = (CClientSocket*)arg;
	thiz->threadFunc2();
	_endthreadex(0);
	return 0;
}
/*
void CClientSocket::threadFunc()
{

	std::string strBuffer;
	strBuffer.resize(BUFFER_SIZE);
	char* pBuffer = (char*)strBuffer.c_str();
	int index = 0;
	InitSocket();
	while (m_sock != INVALID_SOCKET) {
		//while ((m_nIP == INADDR_ANY) || (m_nPort == 0)) {
		//	Sleep(1);
		//}
		//if (InitSocket() == false) {
		//	return;
		//}
		if (m_lstSend.size() > 0) {
			TRACE("lstSend size: %d\r\n", m_lstSend.size());
			m_lock.lock();
			CPacket& head = m_lstSend.front();
			m_lock.unlock();
			if (Send(head) == false) {
				TRACE("发送失败！\r\n");

				continue;
			}
			std::map<HANDLE, std::list<CPacket>&>::iterator it;
			it = m_mapAck.find(head.hEvent);
			if (it != m_mapAck.end()) {
				std::map<HANDLE, bool>::iterator it0 = m_mapAutoClosed.find(head.hEvent);
				do {
					int length = recv(m_sock, pBuffer + index, BUFFER_SIZE - index, 0);
					TRACE("recv %d\r\n", length);
					if (length > 0 || (index > 0)) {
						index += length;
						size_t size = (size_t)index;
						CPacket pack((BYTE*)pBuffer, size);
						if (size > 0) {//TODO:对于文件夹信息获取，文件信息获取可能会产生问题
							pack.hEvent = head.hEvent;
							it->second.push_back(pack);
							memmove(pBuffer, pBuffer + size, index - size);
							index -= size;
							TRACE("recv %d %d\r\n", pack.sCmd, it0->second);
							if (it0->second) {
								SetEvent(head.hEvent);
								break;
							}
						}
					}
					else if (length <= 0 && index <= 0) {
						CloseSocket();
						SetEvent(head.hEvent);//等到服务器关闭命令后，再通知事情完成
						if (it0 != m_mapAutoClosed.end()) {

							TRACE("recv %d %d\r\n", head.sCmd, it0->second);
						}
						else {
							TRACE("异常的情况，没有对应的pair\r\n");
						}

						break;
					}
				} while (it0->second == false);
			}
			m_lock.lock();
			m_lstSend.pop_front();
			m_mapAutoClosed.erase(head.hEvent);
			m_lock.unlock();
			if(InitSocket()==false){
				InitSocket();
			}
		}
		Sleep(1);

		
	}
	CloseSocket();
}
*/

void CClientSocket::threadFunc2()
{
	MSG msg;
	PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
	SetEvent(m_eventInvoke);
	while (::GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if (msg.message == WM_SEND_PACK && m_stopping) {
			delete reinterpret_cast<PACKET_DATA*>(msg.wParam);
			continue;
		}
		if (m_mapFunc.find(msg.message) != m_mapFunc.end()) {
			(this->*m_mapFunc[msg.message])(msg.message, msg.wParam, msg.lParam);
		}
	}
}

bool CClientSocket::Send(const CPacket& pack)
{
	TRACE("m_sock = %llu\r\n", static_cast<unsigned long long>(m_sock.load()));
	if (m_sock == -1) return false;
	std::string strOut;
	pack.Data(strOut);
	return send(m_sock, strOut.c_str(), strOut.size(), 0) > 0;
}

void CClientSocket::SendPack(UINT, WPARAM wParam, LPARAM lParam)
{
    std::unique_ptr<PACKET_DATA> data(reinterpret_cast<PACKET_DATA*>(wParam));
    HWND window = reinterpret_cast<HWND>(lParam);
    auto notify = [&](const CPacket* packet, LPARAM status) {
        if (::IsWindow(window))
            ::SendMessage(window, WM_SEND_PACK_ACK, reinterpret_cast<WPARAM>(packet), status);
    };
    if (!InitSocket()) {
        CloseSocket();
        notify(nullptr, -2);
        return;
    }
    size_t sent = 0;
    while (sent < data->strData.size()) {
        int count = send(m_sock, data->strData.data() + sent,
            static_cast<int>(data->strData.size() - sent), 0);
        if (count <= 0) { CloseSocket(); notify(nullptr, -1); return; }
        sent += count;
    }
    std::string pending;
    char buffer[64 * 1024];
    for (;;) {
        int count = recv(m_sock, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            CloseSocket();
            notify(nullptr, count == 0 && pending.empty() ? 1 : -1);
            return;
        }
        pending.append(buffer, count);
        while (!pending.empty()) {
            size_t consumed = pending.size();
            CPacket packet(reinterpret_cast<const BYTE*>(pending.data()), consumed);
            if (!consumed) break;
            notify(&packet, data->wParam);
            pending.erase(0, consumed);
            if (data->nMode & CSM_AUTOCLOSE) { CloseSocket(); return; }
        }
        if (pending.size() > 64 * 1024 * 1024) {
            CloseSocket(); notify(nullptr, -1); return;
        }
    }
}

void CClientSocket::Shutdown()
{
    if (m_stopping.exchange(true)) return;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        CloseSocket();
    }
    PostThreadMessage(m_nThreadID, WM_QUIT, 0, 0);
    if (m_hThread && m_hThread != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
        m_hThread = INVALID_HANDLE_VALUE;
    }
}
