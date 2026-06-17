// Da un percorso su disco costruisce i FileMetadata del protocollo: nome, size,
// MIME, tempi modified/accessed in ISO 8601. Il MIME qui e' basato
// sull'estensione (portabile); il MIME nativo Haiku (BNodeInfo) e' un
// miglioramento previsto in L4.
#ifndef _LOCALSEND_FILE_SOURCE_H
#define _LOCALSEND_FILE_SOURCE_H


#include "utils/protocol/Models.h"

namespace LocalSend {

// Costruisce i metadati per il file in 'localPath' con id assegnato.
// Ritorna false se il file non e' leggibile o non e' un file regolare.
bool BuildFileMetadata(const char* localPath, const char* id,
	FileMetadata* out);

// MIME indovinato dall'estensione, fallback application/octet-stream.
const char* GuessMimeType(const char* fileName);

} // namespace LocalSend

#endif // _LOCALSEND_FILE_SOURCE_H
