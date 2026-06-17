#include "IHttpClient.h"
#include "utils/BaseUtil.h"
#include "utils/FileUtil.h"
// #include "utils/ScopedWin.h"
// #include "utils/WinDynCalls.h"
#include "utils/WinUtil.h"
#include "utils/ThreadUtil.h"
#include "utils/StrQueue.h"
// #include "utils/HttpUtil.h"
#include "utils/net/HttpClient.h"

// #include "utils/Log.h"


namespace LocalSend {

constexpr const WCHAR* kUserAgent = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:152.0) Gecko/20100101 Firefox/152.0";


#if 0
// 判断是否为 URL 安全字符（RFC 3986）
int is_url_safe(unsigned char c) {
    return (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~');
}

char*
UrlEncode(const char* s)
{
    if (!s) return NULL;

    size_t len = strlen(s);
    // 最坏情况：每个字符变成 %XX，所以需要 3 * len + 1
    char* out = (char*)malloc(3 * len + 1);
    if (!out) return NULL;

    char* p = out;
    const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (is_url_safe(c)) {
            *p++ = (char)c;
        } else {
            *p++ = '%';
            *p++ = hex[c >> 4];
            *p++ = hex[c & 0x0F];
        }
    }
    *p = '\0';

    return out;
}
#endif

#if 0
// Funzioni di I/O astratte: socket nudo o SSL.
typedef std::function<ssize_t(char*, size_t)> ReadFn;
typedef std::function<bool(const char*, size_t)> WriteFn;

namespace {

int
ConnectTo(const std::string& host, int port)
{
	addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	char portStr[16];
	snprintf(portStr, sizeof(portStr), "%d", port);

	addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res)
		return -1;

	int fd = -1;
	for (addrinfo* p = res; p; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}


std::string
ToLower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower((unsigned char)c));
	return s;
}


HttpResponse
ReadResponse(const ReadFn& readFn)
{
	HttpResponse resp;
	std::string buf;
	char tmp[65536];

	// Leggi finche' non hai l'intestazione completa.
	size_t headerEnd = std::string::npos;
	while (true) {
		headerEnd = buf.find("\r\n\r\n");
		if (headerEnd != std::string::npos)
			break;
		ssize_t n = readFn(tmp, sizeof(tmp));
		if (n <= 0)
			break;
		buf.append(tmp, static_cast<size_t>(n));
	}
	if (headerEnd == std::string::npos) {
		resp.status = 0;
		return resp;
	}

	std::string head = buf.substr(0, headerEnd);
	std::string body = buf.substr(headerEnd + 4);

	// Status line.
	size_t lineEnd = head.find("\r\n");
	std::string statusLine = head.substr(0, lineEnd);
	{
		size_t sp1 = statusLine.find(' ');
		if (sp1 != std::string::npos)
			resp.status = atoi(statusLine.c_str() + sp1 + 1);
	}

	// Headers.
	size_t pos = lineEnd + 2;
	long long contentLength = -1;
	bool chunked = false;
	while (pos < head.size()) {
		size_t e = head.find("\r\n", pos);
		if (e == std::string::npos)
			e = head.size();
		std::string line = head.substr(pos, e - pos);
		pos = e + 2;
		size_t colon = line.find(':');
		if (colon != std::string::npos) {
			std::string key = ToLower(line.substr(0, colon));
			size_t v = colon + 1;
			while (v < line.size() && line[v] == ' ')
				v++;
			std::string val = line.substr(v);
			resp.headers[key] = val;
			if (key == "content-length")
				contentLength = atoll(val.c_str());
			if (key == "transfer-encoding"
				&& ToLower(val).find("chunked") != std::string::npos)
				chunked = true;
		}
	}

	// Body: leggi tutto il raw rimanente, poi decodifica se chunked.
	auto readRemaining = [&](long long limit) {
		if (limit >= 0) {
			while (static_cast<long long>(body.size()) < limit) {
				ssize_t n = readFn(tmp, sizeof(tmp));
				if (n <= 0)
					break;
				body.append(tmp, static_cast<size_t>(n));
			}
			if (static_cast<long long>(body.size()) > limit)
				body.resize(limit);
		} else {
			ssize_t n;
			while ((n = readFn(tmp, sizeof(tmp))) > 0)
				body.append(tmp, static_cast<size_t>(n));
		}
	};

	if (contentLength >= 0) {
		readRemaining(contentLength);
	} else if (chunked) {
		// Leggi tutto fino a chiusura, poi decodifica chunked.
		readRemaining(-1);
		// Decodifica chunked: "SIZE\r\nDATA\r\n...0\r\n\r\n"
		std::string decoded;
		size_t p = 0;
		while (p < body.size()) {
			size_t nl = body.find("\r\n", p);
			if (nl == std::string::npos)
				break;
			long long chunkSize = strtoll(body.c_str() + p, nullptr, 16);
			if (chunkSize <= 0)
				break;
			p = nl + 2;
			if (p + static_cast<size_t>(chunkSize) > body.size())
				chunkSize = static_cast<long long>(body.size() - p);
			decoded.append(body, p, static_cast<size_t>(chunkSize));
			p += static_cast<size_t>(chunkSize) + 2; // skip \r\n
		}
		body = std::move(decoded);
	} else {
		readRemaining(-1);
	}

	resp.body = std::move(body);
	return resp;
}


std::string
BuildHead(const std::string& host, int port, const std::string& path,
	const std::string& contentType, long long contentLength)
{
	char head[1024];
	snprintf(head, sizeof(head),
		"POST %s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lld\r\n"
		"Connection: close\r\n"
		"\r\n",
		path.c_str(), host.c_str(), port, contentType.c_str(),
		contentLength);
	return std::string(head);
}

} // namespace
#endif

HttpClient::HttpClient() {
}

HttpClient::~HttpClient() {
}
HttpResponse
HttpClient::Post(const char* host, int port,
		const char* path, StrBuilder* contentType,
		StrBuilder* body)
{
	HttpResponse resp;

    // StrBuilder resp(2048);
    bool ok = false;
    char* hdr = nullptr;
    DWORD hdrLen = 0;
    HINTERNET hConn = nullptr, hReq = nullptr;
    void* d = nullptr;
    DWORD dLen = 0;
    unsigned int timeoutMs = 15 * 1000;
    DWORD respHttpCode = 0;
    DWORD respHttpCodeSize = sizeof(respHttpCode);
    DWORD dwHeaderSize = 0;
    DWORD dwRead = 0;
    DWORD flags;
    DWORD dwService;
    WCHAR* server = ToWStrTemp(host);
    WCHAR* url = ToWStrTemp(path);
    DWORD infoLevel;

    resp.error = ERROR_SUCCESS;
    DWORD accessType = INTERNET_OPEN_TYPE_PRECONFIG;
    HINTERNET hInet = InternetOpenW(kUserAgent, accessType, nullptr, nullptr, 0);
    if (!hInet) {
        printf("HttpPost: InternetOpen failed\n");
        goto Error;
    }
    dwService = INTERNET_SERVICE_HTTP;
    hConn = InternetConnectW(hInet, server, (INTERNET_PORT)port, nullptr, nullptr, dwService, 0, 1);
    if (!hConn) {
        printf("HttpPost: InternetConnectW failed\n");
        goto Error;
    }

    flags = INTERNET_FLAG_NO_UI;
    if (port == 443) {
        flags |= INTERNET_FLAG_SECURE;
    }
    hReq = HttpOpenRequestW(hConn, L"POST", url, nullptr, nullptr, nullptr, flags, 0);
    if (!hReq) {
        printf("HttpPost: HttpOpenRequestW failed\n");
        goto Error;
    }

    if (contentType && contentType->size() > 0) {
        hdr = contentType->Get();
        hdrLen = (DWORD)contentType->size();
    }
    if (body && body->size() > 0) {
        d = body->Get();
        dLen = (DWORD)body->size();
    }

    InternetSetOptionW(hReq, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    if (!HttpSendRequestA(hReq, hdr, hdrLen, d, dLen)) {
        goto Error;
    }
    // 示例：获取完整的原始响应头（包括状态行）
    // 先调用一次获取所需缓冲区大小
    HttpQueryInfoW(hReq, HTTP_QUERY_RAW_HEADERS_CRLF, nullptr, &dwHeaderSize, nullptr);

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        // std::vector<char> headers(dwHeaderSize);
        // Vec<char> headers(dwHeaderSize);
        char headers[2048];
        if (HttpQueryInfoA(hReq, HTTP_QUERY_RAW_HEADERS_CRLF, headers, &dwHeaderSize, nullptr)) {
            // headers.data() 现在包含完整的响应头，以 "\r\n\r\n" 结尾
            printf("Response Headers:\n%s\n", headers);
        }
    }
#if 0

    // 示例：单独获取状态码
    DWORD statusCode = 0;
    DWORD dwSize = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &statusCode, &dwSize, nullptr);
    printf("Status Code: %lu\n", statusCode);

    // 常用 dwInfoLevel 标志：

    // HTTP_QUERY_RAW_HEADERS_CRLF：完整原始头（推荐）
    // HTTP_QUERY_STATUS_CODE：状态码（配合 HTTP_QUERY_FLAG_NUMBER 得到整数）
    // HTTP_QUERY_CONTENT_LENGTH：内容长度
    // HTTP_QUERY_CONTENT_TYPE：MIME 类型
#endif

    infoLevel = HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER;
    HttpQueryInfoW(hReq, infoLevel, &respHttpCode, &respHttpCodeSize, nullptr);
    resp.httpStatusCode = respHttpCode;

    do {
        char buf[1024];
        if (!InternetReadFile(hReq, buf, sizeof(buf), &dwRead)) {
            goto Error;
        }
        ok = resp.data.Append(buf, dwRead);
        if (!ok) {
            goto Error;
        }
    } while (dwRead > 0);

#if 0
    // it looks like I should be calling HttpEndRequest(), but it always claims
    // a timeout even though the data has been sent, received and we get HTTP 200
    if (!HttpEndRequest(hReq, nullptr, 0, 0)) {
        LogLastError();
        goto Exit;
    }
#endif
    ok = (200 == respHttpCode);

Exit:
    if (hReq) {
        InternetCloseHandle(hReq);
    }
    if (hConn) {
        InternetCloseHandle(hConn);
    }
    if (hInet) {
        InternetCloseHandle(hInet);
    }
    // return ok;
    return resp;

Error:
    resp.error = GetLastError();
    if (0 == resp.error) {
        resp.error = ERROR_GEN_FAILURE;
    }
    goto Exit;
}

#if 1
HttpResponse HttpClient::PostFile(const char* host, int port,
    const char* path, StrBuilder* contentType,
    const char* filePath,
    int maxQueueChunks,
    int chunkSize,
    const Func1<HttpUploadProgress*>& cbProgress)
{
    HttpResponse resp;
    // logf("PostFile: server='%s' port=%d path='%s' file='%s'\n",
         // host, port, path, filePath);

    bool ok = false;
    HINTERNET hInet = nullptr, hConn = nullptr, hReq = nullptr;
    DWORD respCode = 0, respCodeSize = sizeof(respCode);
    DWORD timeoutMs = 60 * 1000;
    // 建立 HTTP 连接
    WCHAR* server  = ToWStrTemp(host);
    WCHAR* urlPath = ToWStrTemp(path);

    resp.error = ERROR_SUCCESS;
    // 获取文件大小（用于 Content-Length 和进度计算）

    i64 fileSize = file::GetSize(filePath);

    // 创建有界并发队列，启动生产者线程
    BoundedByteQueue queue(maxQueueChunks);
    bool readerError = false;

    auto td = new FileReaderData{&queue, str::Dup(filePath), chunkSize, &readerError};
    auto fn = MkFunc0(FileReaderThread, td);

    if (fileSize < 0) {
        // logf("PostFile: file not found or empty\n");
        goto Error;
    }

    RunAsync(fn, "HttpPostFileStreamReader");

    hInet = InternetOpenW(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) {
        // logf("HttpPostFileStream: InternetOpenW failed\n");
        goto Error;
    }

    hConn = InternetConnectW(hInet, server, (INTERNET_PORT)port,
                             nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 1);
    if (!hConn) {
        // logf("HttpPostFileStream: InternetConnectW failed\n");
        goto Error;
    }

    {
        DWORD flags = INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD;
        if (port == 443) {
            flags |= INTERNET_FLAG_SECURE;
        }
        hReq = HttpOpenRequestW(hConn, L"POST", urlPath, nullptr, nullptr, nullptr, flags, 0);
    }
    if (!hReq) {
        // logf("HttpPostFileStream: HttpOpenRequestW failed\n");
        goto Error;
    }

    InternetSetOptionW(hReq, INTERNET_OPTION_SEND_TIMEOUT,    &timeoutMs, sizeof(timeoutMs));
    InternetSetOptionW(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    // 添加 Content-Length 头
    {
        // TempStr hdr = str::FormatTemp("Content-Length: %lld\r\nContent-Type: application/octet-stream\r\n\r\n",
        TempStr hdr = str::FormatTemp("Content-Length: %lld\r\nContent-Type: %s\r\n\r\n",
                                      (long long)fileSize,
                                      contentType->Get());
        WCHAR* hdrW = ToWStrTemp(hdr);
        HttpAddRequestHeadersW(hReq, hdrW, (DWORD)-1, HTTP_ADDREQ_FLAG_ADD);
    }

    // 开始流式发送（不在此处传入 body，后续用 InternetWriteFile 写入）
    {
        INTERNET_BUFFERS bufIn{};
        bufIn.dwStructSize  = sizeof(INTERNET_BUFFERS);
        bufIn.dwBufferTotal = (DWORD)fileSize;
        if (!HttpSendRequestExW(hReq, &bufIn, nullptr, 0, 0)) {
            // logf("HttpPostFileStream: HttpSendRequestExW failed\n");
            LogLastError();
            goto Error;
        }
    }

    // 消费队列，逐块写入 HTTP 请求体
    {
        HttpUploadProgress progress{};
        progress.totalSize = fileSize;

        ByteChunk chunk;
        // int tt = 0;
        for (;;) {
            if (!queue.Pop(chunk)) {
                break; // 队列结束
            }
            DWORD dwWritten = 0;
            // logf("debug: chunk data length: %d\n", chunk.len);
            BOOL writeOk = InternetWriteFile(hReq, chunk.data, chunk.len, &dwWritten);
            free(chunk.data);
            if (!writeOk) {
                // logf("HttpPostFileStream: InternetWriteFile failed\n");
                LogLastError();
                goto Error;
            }
            // tt += dwWritten;
            // logf("debug: send data length: %d\nTotal send: %d\n", dwWritten, tt);
            progress.nUploaded += (i64)dwWritten;
            cbProgress.Call(&progress);
        }
    }

    if (readerError) {
        // logf("HttpPostFileStream: file reader thread reported error\n");
        resp.error = (DWORD)readerError;
        goto Error;
    }

    // 结束请求，等待服务器响应
    if (!HttpEndRequest(hReq, nullptr, 0, 0)) {
        // logf("HttpPostFileStream: HttpEndRequest failed\n");
        LogLastError();
        goto Error;
    }

    HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &respCode, &respCodeSize, nullptr);
    // logf("HttpPostFileStream: response code %d\n", (int)respCode);
    resp.httpStatusCode = respCode;
    ok = (respCode >= 200 && respCode < 300);

Exit:
    if (hReq)  InternetCloseHandle(hReq);
    if (hConn) InternetCloseHandle(hConn);
    if (hInet) InternetCloseHandle(hInet);
	return resp;
Error:
    resp.error = GetLastError();
    if (0 == resp.error) {
        resp.error = ERROR_GEN_FAILURE;
    }
    goto Exit;
}

#endif

} // namespace LocalSend
