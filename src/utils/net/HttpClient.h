// Client HTTP/1.1 su socket BSD con supporto TLS opzionale. Portabile e
// garantito su Haiku (-lnetwork). Usato dal mittente (L0) per inviare file a
// un ricevente LocalSend. Con EnableTls() si connette via HTTPS (necessario
// per i client moderni che richiedono TLS).
#ifndef _LOCALSEND_SOCKET_HTTP_CLIENT_H
#define _LOCALSEND_SOCKET_HTTP_CLIENT_H

#include "utils/net/IHttpClient.h"

namespace LocalSend {

class HttpClient : public IHttpClient {
public:
	HttpClient();
	~HttpClient();

	// Abilita TLS per tutte le connessioni successive. Crea internamente
	// un SSL_CTX client che accetta certificati self-signed (come da
	// protocollo LocalSend).
	// void EnableTls();

	HttpResponse Post(const char* host, int port,
		const char* path, StrBuilder* contentType,
		StrBuilder* body) override;

#if 1
	HttpResponse PostFile(const char* host, int port,
		const char* path, StrBuilder* contentType,
		const char* filePath,
        int maxQueueChunks,
        int chunkSize,
		const Func1<HttpUploadProgress*>& cbProgress) override;
    inline HttpResponse PostFile(
    const char* host, int port,
    const char* path, StrBuilder* contentType,
    const char* filePath,
    int maxQueueChunks,
    int chunkSize,
    void* /*nullCallback*/
) {
    return PostFile(host, port, path, contentType, filePath,
                    maxQueueChunks, chunkSize,
                    Func1<HttpUploadProgress*>());
}
#endif

// private:
	// void* fSslCtx; // SSL_CTX*, nullptr se TLS disabilitato
};

} // namespace LocalSend

#endif // _LOCALSEND_SOCKET_HTTP_CLIENT_H
