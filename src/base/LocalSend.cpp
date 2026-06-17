#include "utils/BaseUtil.h"
#include "utils/client/FileSource.h"
// #include "utils/client/UploadSession.h"
#include "utils/HttpUtil.h"
#include "utils/protocol/Constants.h"
// #include "protocol/Fingerprint.h"


using namespace LocalSend;

int send(char* host, StrVec* p) {
    int port = kDefaultPort;
    char* alias = str::Dup("Honey Ratel");
    StrVec* paths = p;

    if (paths->IsEmpty()) {
        return 2;
    }

    Vec<FileMetadata> files;

    for (size_t i = 0; i < paths->Size(); ++i) {
        FileMetadata* m = (FileMetadata*)DeserializeStruct(&gFileMetadataInfo, nullptr);
        char* id = str::Format("file%d", i);
        if (!BuildFileMetadata(paths->At(i), id, m)) {
            return 1;
        }
    }

    DeviceInfo* info = (DeviceInfo*)DeserializeStruct(&gDeviceInfoInfo, nullptr);
    info->alias = alias;
    info->port = port;
    // info->protocol = str::Dup("http");

    // printf("")

}
