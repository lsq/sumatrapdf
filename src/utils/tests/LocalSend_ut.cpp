#include "utils/BaseUtil.h"
#include "utils/client/UploadSession.h"
#include "utils/net/IHttpClient.h"
#include "utils/protocol/Constants.h"
#include "utils/client/FileSource.h"
#include "utils/protocol/Fingerprint.h"
#include "utils/protocol/Models.h"
#include "utils/net/HttpClient.h"

using namespace LocalSend;

struct MyStruct {
    i64 fileSize = 0;
    i64 timestamp = 0;
};

static const FieldInfo gMyStructFields[] = {
    {offsetof(MyStruct, fileSize), SettingType::Int64, 31474836470},
    {offsetof(MyStruct, timestamp), SettingType::Int64, 131474836470},
};
static const StructInfo gMyStructInfo = {sizeof(MyStruct), 2, gMyStructFields, "FileSize\0Timestamp\0"};

HttpResponse test() {
    HttpResponse res;
    res.url.Append("https://127.0.0.1:8080");
    res.data.Append("hello");
    printf("Inside: data=%s\n", res.data.Get()); // 假设有 c_str()
    return res;
}

void TestHttpResponse() {
    HttpResponse r = test();
    printf("Outside:url=%s\n data=%s\n", r.url.Get(), r.data.Get()); // 能正常打印吗？
}

void TestHttpPostFile() {
    HttpClient hc;
    StrBuilder header;

    header.Append("Content-Type: images/png");

    HttpUploadProgress hup{};

    auto perChunkCb = MkFunc1(
        +[](HttpUploadProgress* args, HttpUploadProgress* p) {
            i64 up = args->nUploaded;
            i64 gp = args->totalSize;
            if (gp != p->totalSize) args->totalSize = p->totalSize;

            // 本次新增字节数
            // printf("p->nUploaded: %lld \ngUp: %lld\n", p->nUploaded, up);
            i64 delta = p->nUploaded - up;
            // printf("delta=%lld\n", delta);
            if (delta > 0) {
                args->nUploaded += delta;
                // 触发全局进度回调
                // gp += delta;
            }
        },
        &hup);

    HttpResponse rsp =
        hc.PostFile("localhost", 8880, "/upload", &header, "./out/image/file1.png", 4, 64 * 1024, perChunkCb);
    // HttpResponse rsp = hc.PostFile("localhost", 8880, "/upload", &header, "./out/image/file1.png", 4, 64 * 1024,
    // nullptr);

    if (rsp.IsOk()) {
        printf("totalSize: %lld Uploaded: %lld\n", hup.totalSize, hup.nUploaded);
    } else {
        printf("LocalHost response code: %d, error no: %d\n", (int)rsp.httpStatusCode, (int)rsp.error);
    }
}

void TestHttpPost() {
    // 可以参考 UpdateCheckAsync
    HttpClient httpc;
    StrBuilder header;
    StrBuilder body;
    header.Append("Content-Type: application/json");
    // body.Append("{\"titile\": \"foo\", \"body\": \"bar\", \"userId\": 1}");
    body.Append(R"({"titile": "foo", "body": "bar", "userId": 1})");
    HttpResponse rsp = httpc.Post("localhost", 3000, "/post", &header, &body);

    if (rsp.IsOk()) {
        printf("LocalHost Response: %s\n", rsp.data.Get());
    } else {
        printf("LocalHost response code: %d, error no: %d\n", (int)rsp.httpStatusCode, (int)rsp.error);
    }

    TempStr url = str::FormatTemp("/post?sid=1&file=%s", url::UrlEncodeTemp("文件ID1"));
    // rsp = httpc.Post("localhost", 3000, "/post?sid=1&hello=lsq" ,&header,  &body);
    rsp = httpc.Post("localhost", 3000, url, &header, &body);

    if (rsp.IsOk()) {
        printf("LocalHost Response: %s\n", rsp.data.Get());
    } else {
        printf("LocalHost response code: %d, error no: %d\n", (int)rsp.httpStatusCode, (int)rsp.error);
    }
}

void TestPrepareUpload() {
    const char* preUPloadResult = R"(
        {
            "sessionId": "039728a5-757a-4b0d-a967-126665134142",
            "files": {
                "文件ID1": "c542bc63-d233-4be1-9232-99647bc9f3e8",
                "文件ID2": "4d3ece77-ebd0-4f45-9324-1209c57edc5c"
            }
        }
    )";

    PrepareUploadResult pur{};
    bool ok = ParsePrepareUploadResponse(preUPloadResult, &pur);
    if (ok) {
        printf("pur->sessionId=%s\n", pur.sessionId);
        printf("pur.fileToken.size=%d\n", pur.fileTokens.Size());

        for (int i = 0; i < pur.fileTokens.Size(); i++) {
            char* key = pur.fileTokens.At(i);
            char** value = (char**)pur.fileTokens.AtData(i);
            printf("pur.fileToken.key=%s pur.fileToken.value=%s\n", key, *value);
        }
    }
}

void TestBuildPrepareUpload(const DeviceInfo* info, const StrVecWithData<FileMetadata*>* fm) {
    TempStr js = BuildPrepareUpload(info, fm);

    printf("BuildPrepareUpload->json: %s\n", js);
}

int TestUploadSession(DeviceInfo* info, const StrVecWithData<FileMetadata*>* fm) {
    int port = kDefaultPort;
    char* host = str::Dup("192.168.124.27");

    HttpClient http;

    UploadSession session(http, info);
    SendReport report = session.Send(host, port, 4, 64 * 1024, fm, nullptr);

    if (!report.prepared) {
        const char* hint = "";
        switch (report.prepareStatus) {
            case 0:
                hint = " (connessione fallita: host/port?)";
                break;
            case 204:
                hint = " (Finished (No file transfer needed))";
                break;
            case 400:
                hint = " (Invalid body)";
                break;
            case 401:
                hint = " (PIN required / Invalid PIN)";
                break;
            case 403:
                hint = " (Rejected)";
                break;
            case 409:
                hint = " (Blocked by another session)";
                break;
            case 429:
                hint = " (Too many requests)";
                break;
            case 500:
                hint = " (Unknown error by receiver)";
                break;
        }
        fprintf(stderr, "prepare-upload fallito: HTTP %d%s\n", report.prepareStatus, hint);
        return 1;
    }

    printf("Sessione: %s\n", report.sessionId);
    for (const auto& f : report.files) {
        printf("  [%-9s] %s%s%s\n", StatusText(f.status), f.fileName, str::IsEmpty(f.detail) ? "" : "  -> ", f.detail);
        free(f.detail);
    }

    printf("report->AllSent: %s\n", report.AllSent() ? "true" : "false");
    return report.AllSent() ? 0 : 1;
}

void TestLocalSend() {
    printf("Begining to run LocalSend test ...\n");
    int port = kDefaultPort;
    char* alias = str::Dup("Honey Ratel");
    StrVec p;
    StrVec* paths = &p;
    // paths->Append(str::DupTemp("out/image/file1.Png"));
    // paths->Append(str::DupTemp("out/image/file2.pnG"));
    paths->Append(("out/image/file1.Png"));
    paths->Append(("out/image/file2.pnG"));

    if (paths->IsEmpty()) {
        printf("path is empty!\n");
        // return 2;
    }

    StrVecWithData<FileMetadata*> files;

    for (int i = 0; i < paths->Size(); ++i) {
        FileMetadata* m = (FileMetadata*)DeserializeStruct(&gFileMetadataInfo, nullptr);
        char* id = str::Format("file-%d", i + 1);
        if (!BuildFileMetadata(paths->At(i), id, m)) {
            // return 1;
            printf("Error: BuildFileMetadata failed!\n");
        }
        files.Append(id, m);
    }

    DeviceInfo* info = (DeviceInfo*)DeserializeStruct(&gDeviceInfoInfo, nullptr);
    info->alias = alias;
    info->port = port;
    // info->protocol = str::Dup("http");
    // info->fingerprint = GenerateFingerprint();
    info->fingerprint = LoadOrCreateFingerprint("./localsend_fingerprint");

    printf("before free:\n alias=%s\n", alias);
    printf(
        "DeviceInfo:\n alias=%s\n version=%s\n deviceModel=%s\n deviceType=%s\n fingerprint=%s\n prot=%d\n "
        "protocol=%s\n download=%s\n",
        info->alias, info->version, info->deviceModel, info->deviceType, info->fingerprint ? info->fingerprint : "null",
        info->port, info->protocol, info->download ? "true" : "false");

    // FreeStruct(&gDeviceInfoInfo, info);
    alias = nullptr;
    if (alias != nullptr) {
        printf("after free:\n alias=%s\n", alias);
        printf(
            "DeviceInfo:\n alias=%s\n version=%s\n deviceModel=%s\n deviceType=%s\n fingerprint=%s\n prot=%d\n "
            "protocol=%s\n download=%s\n",
            info->alias, info->version, info->deviceModel, info->deviceType,
            info->fingerprint ? info->fingerprint : "null", info->port, info->protocol,
            info->download ? "true" : "false");
    } else {
        printf("Free successful!\n");
    }

    printf("files: %d - FileMetadatas: %d\n", paths->Size(), files.Size());
    FileMetadata* t;
    for (int i = 0; i < files.Size(); i++) {
        printf("i=%d\n", i);
        t = *files.AtData(i);
        printf(
            "FileMetadata:\n id=%s\n fileName=%s\n size=%lld\n fileType=%s\n sha256=%s\n preview=%s\n modified=%s\n "
            "accessed=%s\n localPath=%s\n",
            t->id, t->fileName, t->size, t->fileType, t->sha256, t->preview, t->modified, t->accessed, t->localPath);
        // FreeStruct(&gFileMetadataInfo, t);
    }

    printf("Testing TestBuildPrepareUpload\n--------------------\n");
    TestBuildPrepareUpload(info, &files);

    printf("Testing TestUploadSession\n--------------------\n");
    TestUploadSession(info, &files);

    MyStruct* mst = (MyStruct*)DeserializeStruct(&gMyStructInfo, nullptr);
    mst->fileSize = 11111111111111111;
    printf("MyStruct:\n fileSize=%lld\n timestamp=%lld\n", mst->fileSize, mst->timestamp);
    FreeStruct(&gMyStructInfo, mst);

    printf("Testing TestHttpResponse\n--------------------\n");
    TestHttpResponse();

    printf("Testing TestHttpPost\n--------------------\n");
    TestHttpPost();

    printf("Testing TestHttpPostFile\n--------------------\n");
    TestHttpPostFile();

    printf("Testing TestPrepareUpload\n--------------------\n");
    TestPrepareUpload();
}
