#pragma once
#include <Windows.h>
#include <string>
#include <atlimage.h>
#include <climits>

class CEdoyunTool
{
public:
    static void Dump(BYTE* pData, size_t nSize)
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
    static HRESULT Bytes2Image(CImage& image, const std::string& bytes)
    {
        if (bytes.empty() || bytes.size() > ULONG_MAX) return E_INVALIDARG;
        CComPtr<IStream> stream;
        HRESULT result = CreateStreamOnHGlobal(NULL, TRUE, &stream);
        if (FAILED(result)) return result;
        ULONG written = 0;
        result = stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written);
        if (FAILED(result) || written != bytes.size()) return E_FAIL;
        LARGE_INTEGER start = {};
        result = stream->Seek(start, STREAM_SEEK_SET, NULL);
        if (FAILED(result)) return result;
        CImage decoded;
        result = decoded.Load(stream);
        if (SUCCEEDED(result)) {
            image.Destroy();
            image.Attach(decoded.Detach());
        }
        return result;
    }
};
