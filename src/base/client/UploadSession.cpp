#include "utils/client/UploadSession.h"
#include "utils/BaseUtil.h"
#include "utils/net/IHttpClient.h"
#include "utils/net/HttpClient.h"
#include "utils/protocol/Constants.h"

namespace LocalSend {

bool
SendReport::AllSent() const
{
	if (files.empty())
		return false;
	for (const auto& f : files) {
		if (f.status != FileOutcome::Status::Sent)
			return false;
	}
	return true;
}


UploadSession::UploadSession(IHttpClient& http, DeviceInfo* info)
	:
	fHttp(http),
	fInfo(info)
{
}


void
UploadSession::Cancel(const char* host, int port,
	const char* sessionId)
{
	char* path = str::Format("%s?sessionId=%s", kApiCancel,
		  UrlEncode(sessionId));
	fHttp.Post(host, port, path, "application/json", "");
}


SendReport
UploadSession::Send(const char* host, int port,
	const StrVecWithData<FileMetadata*>& files, //const std::string& pin,
	const Func1<UploadProgress*>& progress)
{
	SendReport report;

	// Passo 1: prepare-upload (solo metadati).
	TempStr preparePath = str::DupTemp(kApiPrepareUpload);
	// if (!pin.empty())
	// 	preparePath += "?pin=" + UrlEncode(pin);

    StrBuilder body(BuildPrepareUpload(fInfo, &files));
    StrBuilder headers("application/json");
	HttpResponse resp = fHttp.Post(host, port, preparePath, &headers,
		&body);
	report.prepareStatus = resp.httpStatusCode;

	if (!resp.IsOk()) {
		// 204 = Finished (No file transfer needed);
        // 400 Invalid body
        // 401 PIN required/Invalid PIN
        // 403 = Rejected
		// 409 = Blocked by another session
        // 429 = Too many requests
        // 500 = Unknown error by receiver

		return report;
	}

	PrepareUploadResult prep = ParsePrepareUploadResponse(resp.data);
	report.prepared = true;
	report.sessionId = prep.sessionId;

	// Calcola il totale dei byte accettati per il progresso.
	long long totalBytes = 0;
    {
    int idx = 0;
	for (const auto& f : files) {
        int i = prep.fileTokens.Find(f);
		if (i > -1) {
            FileMetadata* m = *files.AtData(idx);
            i64 sz =  m->size;
			totalBytes += sz;
        }
        idx++;
	}
    }

	// Separa file accettati e rifiutati.
	struct UploadTask {
		const FileMetadata* file;
		std::string token;
		FileOutcome outcome;
	};
	std::vector<UploadTask> tasks;
	for (const auto& f : files) {
		auto it = prep.fileTokens.find(f.id);
		if (it == prep.fileTokens.end()) {
			FileOutcome out;
			out.fileId = f.id;
			out.fileName = f.fileName;
			out.status = FileOutcome::Status::Rejected;
			out.detail = "non accettato dal ricevente";
			report.files.push_back(out);
		} else {
			UploadTask t;
			t.file = &f;
			t.token = it->second;
			t.outcome.fileId = f.id;
			t.outcome.fileName = f.fileName;
			tasks.push_back(t);
		}
	}

	// Passo 2: upload parallelo. Ogni thread crea la propria connessione.
	std::atomic<long long> sentBytes(0);
	bool anyError = false;

	auto uploadOne = [&](UploadTask& t) {
		std::string path = std::string(kApiUpload)
			+ "?sessionId=" + UrlEncode(prep.sessionId)
			+ "&fileId=" + UrlEncode(t.file->id)
			+ "&token=" + UrlEncode(t.token);

		TransferProgressFn fileProgress;
		if (progress) {
			fileProgress = [&](long long fileSent, long long) {
				progress(sentBytes + fileSent, totalBytes);
			};
		}

		SocketHttpClient http;
		http.EnableTls();
		HttpResponse up = http.PostFile(host, port, path,
			t.file->fileType, t.file->localPath, fileProgress);
		if (up.IsOk()) {
			t.outcome.status = FileOutcome::Status::Sent;
			sentBytes += t.file->size;
		} else {
			t.outcome.status = FileOutcome::Status::Error;
			t.outcome.detail = up.status == 0
				? "connessione/IO fallita"
				: ("HTTP " + std::to_string(up.status));
			anyError = true;
		}
	};

	if (tasks.size() == 1) {
		// Un solo file: niente thread extra.
		uploadOne(tasks[0]);
	} else {
		// Piu' file: upload parallelo.
		std::vector<std::thread> threads;
		for (auto& t : tasks)
			threads.emplace_back(uploadOne, std::ref(t));
		for (auto& th : threads)
			th.join();
	}

	for (auto& t : tasks)
		report.files.push_back(t.outcome);

	// Passo 3: in caso di errore a meta' sessione, annulla.
	if (anyError && !prep.sessionId.empty())
		Cancel(host, port, prep.sessionId);

	return report;
}

} // namespace LocalSend
