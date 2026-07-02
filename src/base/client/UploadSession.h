// Orchestrazione del mittente (L0): prepare-upload -> upload -> cancel.
// Tiene sessionId + mappa fileId->token. La stessa nozione serve al ricevente
// in L1, lato opposto (li genera e li valida).
#ifndef _LOCALSEND_UPLOAD_SESSION_H
#define _LOCALSEND_UPLOAD_SESSION_H

#include "base/Base.h"
#include "base/net/IHttpClient.h"
#include "base/protocol/Models.h"

namespace LocalSend {

struct FileOutcome {
    Str fileId = nullptr;
    Str fileName = nullptr;
    enum class Status {
        Sent,
        Rejected,
        Error
    } status = Status::Error;
    Str detail = nullptr;
};

struct SendReport {
    bool prepared = false;
    int prepareStatus = 0; // codice HTTP del prepare-upload
    Str sessionId = nullptr;
    Vec<FileOutcome> files;
    bool AllSent() const;
};

const char* StatusText(FileOutcome::Status s);

class UploadSession {
  public:
    UploadSession(IHttpClient& http, DeviceInfo* info);

    // Callback di progresso: (byteInviati, byteTotaliSessione).
    // typedef std::function<void(long long, long long)> ProgressFn;

    // Invia i file al ricevente host:port. 'pin' opzionale (vuoto = nessuno).
    // 'progress' viene chiamato durante l'invio con i byte trasferiti.
    SendReport Send(Str host, int port, int workerCount, int chunkSize, const StrVecWithData<FileMetadata*>* files,
                    // Str pin = "",
                    const Func1<UploadProgress*>& cbProgress);
    SendReport Send(Str host, int port, int workerCount, int chunkSize, const StrVecWithData<FileMetadata*>* files,
                    // Str pin = "",
                    void*) {
        return Send(host, port, workerCount, chunkSize, files, Func1<UploadProgress*>());
    };

  private:
    void Cancel(Str host, int port, Str sessionId);

    IHttpClient& fHttp;
    DeviceInfo* fInfo;
};

} // namespace LocalSend

#endif // _LOCALSEND_UPLOAD_SESSION_H
