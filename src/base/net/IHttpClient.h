// Interfaccia del client HTTP usata da L0. Separata da IHttpServer (L1) di
// proposito: L0 usa solo questa, L1 aggiunge il server senza toccare l'invio.
#ifndef _LOCALSEND_IHTTP_CLIENT_H
#define _LOCALSEND_IHTTP_CLIENT_H

#include "base/Base.h"
#include "base/Http.h"
namespace LocalSend {

// Callback di progresso a livello di byte: (bytesTrasferiti, byteTotali).
// typedef std::function<void(long long, long long)> TransferProgressFn;

struct HttpResponse {
    // int status = 0;
    // std::map<std::string, std::string> headers; // chiavi in minuscolo
    // std::string body;
    // AutoFreeStr url;
    Str url;
    str::Builder data;
    DWORD error = (DWORD)-1;
    DWORD httpStatusCode = (DWORD)-1;

    HttpResponse() = default;
    // HttpResponse(HttpResponse&&) = default;             // 允许移动
    // HttpResponse& operator=(HttpResponse&&) = default;  // 允许移动赋值

    // 禁用拷贝
    // HttpResponse(const HttpResponse&) = delete;
    // HttpResponse& operator=(const HttpResponse&) = delete;

    bool IsOk() const { return httpStatusCode >= 200 && httpStatusCode < 300; }

    ~HttpResponse() { str::Free(url); }
};

class IHttpClient {
  public:
    virtual ~IHttpClient() {}

    // POST con body in memoria. 'path' include gia' l'eventuale query string.
    virtual HttpResponse Post(Str host, int port, Str path, str::Builder* contentType, str::Builder* body) = 0;
#if 1
    // POST che invia in streaming il contenuto di un file come body.
    // 'progress' (opzionale) viene chiamato dopo ogni chunk inviato.
    virtual HttpResponse PostFile(Str host, int port, Str path, str::Builder* contentType, Str filePath,
                                  int maxQueueChunks, int chunkSize, const Func1<HttpUploadProgress*>& cbProgress) = 0;

    // HttpResponse PostFile(const char* host, int port,
    // 	const char* path, str::Builder* contentType,
    // 	const char* filePath,
    //     int maxQueueChunks,
    //     int chunkSize, void*) {
    // 	    return PostFile(host, port, path, contentType, filePath,
    //                 maxQueueChunks, chunkSize,
    //                 Func1<HttpUploadProgress*>());
    // };
    //

#endif
};

// Codifica percentuale per i valori di query (sessionId, fileId, token, pin).
// char* UrlEncode(const char* s);

} // namespace LocalSend

#endif // _LOCALSEND_IHTTP_CLIENT_H
