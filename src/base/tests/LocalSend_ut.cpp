#include "base/Base.h"
#include "base/client/UploadSession.h"
#include "base/net/IHttpClient.h"
#include "base/protocol/Constants.h"
#include "base/client/FileSource.h"
#include "base/protocol/Fingerprint.h"
#include "base/protocol/Models.h"
#include "base/net/HttpClient.h"
#include "base/DirIter.h"

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
    res.url = str::Dup("https://127.0.0.1:8080");
    res.data.Append("hello");
    printf("Inside: data=%s\n", ToCStr(res.data)); // 假设有 c_str()
    return res;
}

void TestHttpResponse() {
    HttpResponse r = test();
    printf("Outside:url=%s\n data=%s\n", r.url.s, ToCStr(r.data)); // 能正常打印吗？
}

void TestHttpPostFile() {
    HttpClient hc;
    str::Builder header;

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
    str::Builder header;
    str::Builder body;
    header.Append("Content-Type: application/json");
    // body.Append("{\"titile\": \"foo\", \"body\": \"bar\", \"userId\": 1}");
    body.Append(R"({"titile": "foo", "body": "bar", "userId": 1})");
    HttpResponse rsp = httpc.Post("localhost", 3000, "/post", &header, &body);

    if (rsp.IsOk()) {
        printf("LocalHost Response: %s\n", ToCStr(rsp.data));
    } else {
        printf("LocalHost response code: %d, error no: %d\n", (int)rsp.httpStatusCode, (int)rsp.error);
    }

    TempStr url = fmt("/post?sid=1&file=%s", url::UrlEncodeTemp("文件ID1"));
    // rsp = httpc.Post("localhost", 3000, "/post?sid=1&hello=lsq" ,&header,  &body);
    rsp = httpc.Post("localhost", 3000, url, &header, &body);

    if (rsp.IsOk()) {
        printf("LocalHost Response: %s\n", ToCStr(rsp.data));
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
        printf("pur->sessionId=%s\n", pur.sessionId.s);
        printf("pur.fileToken.size=%d\n", len(pur.fileTokens));

        for (int i = 0; i < len(pur.fileTokens); i++) {
            Str key = pur.fileTokens.At(i);
            Str* value = (Str*)pur.fileTokens.AtData(i);
            printf("pur.fileToken.key=%s pur.fileToken.value=%s\n", key.s, (*value).s);
        }
    }
}

void TestBuildPrepareUpload(const DeviceInfo* info, const StrVecWithData<FileMetadata*>* fm) {
    TempStr js = BuildPrepareUpload(info, fm);

    printf("BuildPrepareUpload->json: %s\n", js.s);
}

int TestUploadSession(DeviceInfo* info, const StrVecWithData<FileMetadata*>* fm) {
    int port = kDefaultPort;
    Str host = str::Dup("192.168.124.3");

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

    printf("Sessione: %s\n", report.sessionId.s);
    for (const auto& f : report.files) {
        printf("  [%-9s] %s%s%s\n", StatusText(f.status), f.fileName.s, str::IsEmpty(f.detail) ? "" : "  -> ",
               f.detail.s);
        str::Free(f.detail);
    }

    printf("report->AllSent: %s\n", report.AllSent() ? "true" : "false");
    return report.AllSent() ? 0 : 1;
}

void TestDirIter() {
    Str st = ("E:\\temp\\报销");
    // Str st = ("E:\\temp\\t1");
    auto dr = DirIter{st};
    // dr.includeDirs = false;
    // dr.includeFiles = true;
    dr.recurse = true;

    for (DirIterEntry* de : dr) {
        printf("d->filepath:%s\n", de->filePath.s);
    }
}

void TestLocalSend() {
    printf("Begining to run LocalSend test ...\n");

    printf("Testing TestDirIter\n--------------------\n");
    TestDirIter();

    int port = kDefaultPort;
    Str alias = str::Dup("Honey Ratel");
    StrVec p;
    StrVec* paths = &p;
    // paths->Append(str::DupTemp("out/image/file1.Png"));
    // paths->Append(str::DupTemp("out/image/file2.pnG"));
    paths->Append(("out/image/file1.Png"));
    paths->Append(("out/image/file2.pnG"));
    // paths->Append(("d:/Backup/Downloads/FlClash-0.8.94-android-arm64-v8a.apk"));

    if (paths->IsEmpty()) {
        printf("path is empty!\n");
        // return 2;
    }

    StrVecWithData<FileMetadata*> files;

    for (int i = 0; i < len(*paths); ++i) {
        FileMetadata* m = (FileMetadata*)DeserializeStruct(&gFileMetadataInfo, nullptr);
        Str id = str::Dup(fmt("file-%d", i + 1));
        if (!BuildFileMetadata(paths->At(i), paths->At(i), id.s, m)) {
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

    printf("before free:\n alias=%s\n", alias.s);
    printf(
        "DeviceInfo:\n alias=%s\n version=%s\n deviceModel=%s\n deviceType=%s\n fingerprint=%s\n prot=%d\n "
        "protocol=%s\n download=%s\n",
        info->alias.s, info->version.s, info->deviceModel.s, info->deviceType.s,
        info->fingerprint ? info->fingerprint.s : "null", info->port, info->protocol.s,
        info->download ? "true" : "false");

    // FreeStruct(&gDeviceInfoInfo, info);
    alias = nullptr;
    if (!alias) {
        printf("after free:\n alias=%s\n", alias.s);
        printf(
            "DeviceInfo:\n alias=%s\n version=%s\n deviceModel=%s\n deviceType=%s\n fingerprint=%s\n prot=%d\n "
            "protocol=%s\n download=%s\n",
            info->alias.s, info->version.s, info->deviceModel.s, info->deviceType.s,
            info->fingerprint ? info->fingerprint.s : "null", info->port, info->protocol.s,
            info->download ? "true" : "false");
    } else {
        printf("Free successful!\n");
    }

    printf("files: %d - FileMetadatas: %d\n", len(*paths), len(files));
    FileMetadata* t;
    for (int i = 0; i < len(files); i++) {
        printf("i=%d\n", i);
        t = *files.AtData(i);
        printf(
            "FileMetadata:\n id=%s\n fileName=%s\n size=%lld\n fileType=%s\n sha256=%s\n preview=%s\n modified=%s\n "
            "accessed=%s\n localPath=%s\n",
            t->id.s, t->fileName.s, t->size, t->fileType.s, t->sha256.s, t->preview.s, t->modified.s, t->accessed.s,
            t->localPath.s);
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
