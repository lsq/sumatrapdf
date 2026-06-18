// Orchestrazione del mittente (L0): prepare-upload -> upload -> cancel.
// Tiene sessionId + mappa fileId->token. La stessa nozione serve al ricevente
// in L1, lato opposto (li genera e li valida).
#ifndef _LOCALSEND_UPLOAD_SESSION_H
#define _LOCALSEND_UPLOAD_SESSION_H


#include "utils/BaseUtil.h"
#include "utils/net/IHttpClient.h"
#include "utils/protocol/Models.h"

namespace LocalSend {

struct FileOutcome {
	char* fileId;
	char* fileName;
	enum class Status { Sent, Rejected, Error } status = Status::Error;
	char* detail;
};

struct SendReport {
	bool prepared = false;
	int prepareStatus = 0; // codice HTTP del prepare-upload
	const char* sessionId;
	Vec<FileOutcome> files;
	bool AllSent() const;
};

class UploadSession {
public:
	UploadSession(IHttpClient& http, DeviceInfo* info);

	// Callback di progresso: (byteInviati, byteTotaliSessione).
	// typedef std::function<void(long long, long long)> ProgressFn;

	// Invia i file al ricevente host:port. 'pin' opzionale (vuoto = nessuno).
	// 'progress' viene chiamato durante l'invio con i byte trasferiti.
	SendReport Send(const char* host, int port,
		const StrVecWithData<FileMetadata*>* files,
		// const char* pin = "",
		const Func1<UploadProgress*>& cbProgress);
	SendReport Send(const char* host, int port,
		const StrVecWithData<FileMetadata*>* files,
		// const char* pin = "",
		void*) {
        return Send(host, port, files, Func1<UploadProgress*>());
    };


private:
	void Cancel(const char* host, int port,
		const char* sessionId);

	IHttpClient& fHttp;
	DeviceInfo* fInfo;
};

} // namespace LocalSend

#endif // _LOCALSEND_UPLOAD_SESSION_H
