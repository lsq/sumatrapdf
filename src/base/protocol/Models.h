// Modelli del protocollo LocalSend, indipendenti dal trasporto.
// Condivisi tra mittente (L0) e ricevente (L1): il ricevente genera
// sessionId/token con le stesse strutture, lato opposto.
#ifndef _LOCALSEND_MODELS_H
#define _LOCALSEND_MODELS_H

#include <cstddef>
#include "utils/BaseUtil.h"
#include "utils/SettingsUtil.h"
namespace LocalSend {

// L'oggetto "info" del prepare-upload (identita' del device).
struct DeviceInfo {
	char* alias = nullptr;
	char* version = nullptr;
	char* deviceModel = nullptr;
	char* deviceType = nullptr; // fallback per valori sconosciuti
	char* fingerprint = nullptr;  // in HTTP: stringa casuale persistente
	int port = 53317;
	char* protocol = nullptr;      // "https" da L3
	bool download = false;

	// JsonValue ToJson() const;
	// Lato ricevente (L1): legge il blocco "info" inviato dal mittente.
	// static DeviceInfo FromJson(const JsonValue& v);
};

inline const FieldInfo gDeviceInfoFields[] = {
    {offsetof(DeviceInfo, alias),   SettingType::String, (intptr_t)"Honey Ratel"},
    {offsetof(DeviceInfo, version), SettingType::String, (intptr_t)"2.1"},
    {offsetof(DeviceInfo, deviceModel), SettingType::String, (intptr_t)"Gentoo"},
    {offsetof(DeviceInfo, deviceType), SettingType::String, (intptr_t)"desktop"},
    {offsetof(DeviceInfo, fingerprint), SettingType::String, (intptr_t)nullptr},
    {offsetof(DeviceInfo, port), SettingType::Int, 0},
    {offsetof(DeviceInfo, protocol), SettingType::String, (intptr_t)"http"},
    {offsetof(DeviceInfo, download), SettingType::Bool, (intptr_t)false}
};

inline const StructInfo gDeviceInfoInfo = {
    sizeof(DeviceInfo), 8, gDeviceInfoFields,
    "Alias\0Version\0DeviceModel\0DeviceType\0Fingerpirnt\0Port\0Protocol\0Download"
};

// Metadati di un file nel prepare-upload.
struct FileMetadata {
	char* id;
	char* fileName;
	long long size = 0;
	char* fileType = nullptr;
	char* sha256;   // vuoto -> null
	char* preview;  // vuoto -> null
	char* modified; // ISO 8601 UTC, vuoto -> omesso
	char* accessed; // ISO 8601 UTC, vuoto -> omesso

	char* localPath; // non serializzato: sorgente dei byte (mittente)

	// JsonValue ToJson() const;
	// Lato ricevente (L1): legge i metadati di un file dalla richiesta.
	// static FileMetadata FromJson(const JsonValue& v);
};

inline const FieldInfo gFileMetadataFileds[] = {
    {offsetof(FileMetadata, id),  SettingType::String, (intptr_t)nullptr},
    {offsetof(FileMetadata, fileName),  SettingType::String, (intptr_t)nullptr},
    {offsetof(FileMetadata, size),  SettingType::Int, 0},
    {offsetof(FileMetadata, fileName),  SettingType::String, (intptr_t)"application/octet-stream"},
    {offsetof(FileMetadata, sha256),  SettingType::String, (intptr_t)nullptr},
    {offsetof(FileMetadata, preview),  SettingType::String, (intptr_t)nullptr},
    {offsetof(FileMetadata, modified),  SettingType::String, (intptr_t)nullptr},
    {offsetof(FileMetadata, accessed),  SettingType::String, (intptr_t)nullptr},
    {offsetof(FileMetadata, localPath),  SettingType::String, (intptr_t)nullptr},
};

inline const StructInfo gFileMetadataInfo = {
    sizeof(FileMetadata), 9, gFileMetadataFileds,
    "Id\0FileName\0Size\0Sha256\0Preview\0Modified\0Accessed\0LocalPath"
};

struct UploadInfo {
    const DeviceInfo* info;
    const StrVecWithData<FileMetadata*>* files;
};

// inline const FieldInfo gUploadInfoFileds[] = {
//     {offsetof(UploadInfo, info), SettingType::Struct, }
// }

//UploadProgress
// Costruisce il body completo del prepare-upload (mittente, L0).
TempStr BuildPrepareUpload(const DeviceInfo* info,
	const StrVecWithData<FileMetadata*>* files);

// Esito del prepare-upload: sessionId + token per i file ACCETTATI.
// Struttura SIMMETRICA: il mittente la OTTIENE dalla risposta, il ricevente la
// GENERA prima di rispondere. Stessa forma, lati opposti.
struct PrepareUploadResult {
	const char* sessionId;
	StrVecWithData<char*> fileTokens; // fileId -> token

    // 说明
    // char* token1 = strdup("abc"); // 分配内存，token1 = 0x1000
    // fileTokens.Append("f1", token1);
    // 此时，在 StrVec 的附加数据区（data segment）中，第 0 个元素的位置（假设地址是 0x2000）存的是：
    // 地址 0x2000: [0x10, 0x00, 0x00, 0x00, ...]  // 即值 0x1000（小端）
    // char** p = fileTokens.AtData(0); // p == 0x2000（指向存储位置）
    // char* val = *p;                  // val == 0x1000（即 token1）
    // 我们要释放的是 0x1000 处的内存，所以应该调用：
    // free(val);           // 正确
    // free(*p);            // 正确
    // free(*fileTokens.AtData(0)); // ✅ 正确！


    ~PrepareUploadResult() {
    // 1. 手动释放每个动态分配的 token 字符串
    for (int i = 0; i < fileTokens.Size(); i++) {
        char* token = *fileTokens.AtData(i);
        free(token); // 或 delete[]，取决于分配方式
    }
    if (sessionId) {
        str::Free(sessionId);
        sessionId = nullptr;
    }
    }
};

void FreePpInfo(PrepareUploadResult* ip);
// Mittente (L0): legge la risposta del ricevente.
bool ParsePrepareUploadResponse(const char* body, PrepareUploadResult* out);

// --- Lato ricevente (L1) -------------------------------------------------

#if 0
// Richiesta prepare-upload come la vede il ricevente: chi invia + cosa invia.
struct IncomingPrepareUpload {
	DeviceInfo sender;
	std::vector<FileMetadata> files; // file richiesti, nell'ordine di arrivo
};

// Ricevente (L1): legge il body del prepare-upload ricevuto dal mittente.
IncomingPrepareUpload ParsePrepareUploadRequest(const std::string& body);

// Ricevente (L1): costruisce il body della risposta (sessionId + token dei file
// accettati). Inverso di ParsePrepareUploadResponse.
JsonValue BuildPrepareUploadResponse(const std::string& sessionId,
	const std::map<std::string, std::string>& fileTokens);

#endif
} // namespace LocalSend

#endif // _LOCALSEND_MODELS_H
