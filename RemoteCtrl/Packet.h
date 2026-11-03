#pragma once
#include "pch.h"
#include "framework.h"

#pragma pack(push)
#pragma pack(1)
//设计一个包
class CPacket
{
public:
	CPacket() :sHead(0), nLength(0), sCmd(0), sSum(0) {}
	CPacket(WORD nCmd, const BYTE* pData, size_t nSize) {
		sHead = 0xFEFF;
		nLength = nSize + 4;
		sCmd = nCmd;
		if (nSize > 0) {
			strData.resize(nSize);
			memcpy((void*)strData.c_str(), pData, nSize);
		}
		else {
			strData.clear();
		}
		sSum = 0;
		for (size_t j = 0; j < strData.size(); j++)
		{
			sSum += BYTE(strData[j]) & 0xFF;
		}
	}
	CPacket(const CPacket& pack) {
		sHead = pack.sHead;
		nLength = pack.nLength;
		sCmd = pack.sCmd;
		strData = pack.strData;
		sSum = pack.sSum;
	}
	CPacket(const BYTE* pData, size_t& nSize) : CPacket() {
        const size_t available = nSize;
        nSize = 0;
        size_t offset = 0;
        WORD head = 0;
        while (offset + sizeof(head) <= available) {
            memcpy(&head, pData + offset, sizeof(head));
            if (head == 0xFEFF) break;
            ++offset;
        }
        if (available - offset < 10) return;
        DWORD length = 0;
        memcpy(&length, pData + offset + 2, sizeof(length));
        if (length < 4 || length > 64 * 1024 * 1024 || length > available - offset - 6) return;
        WORD command = 0, checksum = 0, sum = 0;
        memcpy(&command, pData + offset + 6, sizeof(command));
        memcpy(&checksum, pData + offset + 6 + length - 2, sizeof(checksum));
        std::string payload(reinterpret_cast<const char*>(pData + offset + 8), length - 4);
        for (unsigned char value : payload) sum += value;
        if (sum != checksum) return;
        sHead = head;
        nLength = length;
        sCmd = command;
        sSum = checksum;
        strData.swap(payload);
        nSize = offset + 6 + length;
    }
    ~CPacket() {}
	CPacket& operator=(const CPacket& pack) {
		if (this != &pack) {
			sHead = pack.sHead;
			nLength = pack.nLength;
			sCmd = pack.sCmd;
			strData = pack.strData;
			sSum = pack.sSum;
		}
		return *this;
	}
	int Size() { //包数据的大小
		return nLength + 6;
	}
	const char* Data() {
		strOut.resize(nLength + 6);
		BYTE* pData = (BYTE*)strOut.c_str();
		*(WORD*)pData = sHead; pData += 2;
		*(DWORD*)(pData) = nLength; pData += 4;
		*(WORD*)pData = sCmd; pData += 2;
		memcpy(pData, strData.c_str(), strData.size()); pData += strData.size();
		*(WORD*)pData = sSum;
		return strOut.c_str();
	}
public:
	WORD sHead; //固定位 0xFEFF
	DWORD nLength; //包长度（从控制命令开始，到和校验结束）
	WORD sCmd;    //控制命令
	std::string strData; //包数据
	WORD sSum; //和校验
	std::string strOut; //整个包的数据
};

#pragma pack(pop)

typedef struct MouseEvent {
	MouseEvent() {
		nAction = 0;
		nButton = -1; //如果置成0，默认就是左键  置成-1，一旦出现问题，说明参数设置有问题
		ptXY.x = 0;
		ptXY.y = 0;
	}
	WORD nAction; //点击、移动、双击
	WORD nButton; //左键、右键、中键
	POINT ptXY; //坐标
}MOUSEEV, * PMOUSEEV;

typedef struct file_info {
	file_info() {
		IsInvalid = FALSE;
		IsDirectory = -1;
		HasNext = TRUE;
		memset(szFileName, 0, sizeof(szFileName));
	}
	BOOL IsInvalid; // 是否有效
	BOOL IsDirectory; //是否为目录 0 否 1 是
	BOOL HasNext; // 是否还有后续 0没有 1 有
	char szFileName[256]; // 文件名

}FILEINFO, * PFILEINFO;