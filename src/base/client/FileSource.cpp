#include "utils/BaseUtil.h"
#include "utils/FileUtil.h"
#include "utils/WinUtil.h"
#include "utils/client/FileSource.h"
#include <cstdio>

namespace LocalSend {

namespace {


char*
ExtLower(const char* fileName)
{
	char* ext = str::DupTemp(path::GetExtTemp(fileName));
	return str::ToLowerInPlace(ext);
}


char*
ToISO8601(FILETIME ft)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&ft, &st)){
        return nullptr;
    }

	// char* buf = str::Format("%Y-%m-%dT%H:%M:%SZ", );
	char* buf = str::Format("%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
	return buf;
}

} // namespace


const char*
GuessMimeType(const char* fileName)
{
	char* ext = ExtLower(fileName) + 1;
	constexpr struct { const char* ext; const char* mime; } table[] = {
		{"png", "image/png"},   {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
		{"gif", "image/gif"},   {"webp", "image/webp"},{"bmp", "image/bmp"},
		{"svg", "image/svg+xml"},
		{"pdf", "application/pdf"},
		{"txt", "text/plain"},  {"md", "text/markdown"},{"csv", "text/csv"},
		{"html","text/html"},   {"htm", "text/html"},
		{"json","application/json"}, {"xml", "application/xml"},
		{"zip", "application/zip"},  {"gz", "application/gzip"},
		{"tar", "application/x-tar"},{"7z", "application/x-7z-compressed"},
		{"mp3", "audio/mpeg"},  {"wav", "audio/wav"},  {"flac","audio/flac"},
		{"ogg", "audio/ogg"},   {"m4a", "audio/mp4"},
		{"mp4", "video/mp4"},   {"mkv", "video/x-matroska"},
		{"webm","video/webm"},  {"mov", "video/quicktime"},
		{"avi", "video/x-msvideo"},
	};
	for (const auto& e : table) {
    // printf("ext: %s - %s - %s\n", ext, e.ext, str::EqIS(ext, e.ext) ? "true" : "false");
		if (str::EqIS(ext, e.ext))
			return e.mime;
	}
	return "application/octet-stream";
}


bool
BuildFileMetadata(const char* localPath, const char* id,
	FileMetadata* out)
{
	// struct stat st;
	// if (stat(localPath, &st) != 0)
	// 	return false;
	// if (!S_ISREG(st.st_mode))
		// return false;
    if (!file::Exists(localPath)) {
        printf("Error: %s not exists!\n", localPath);
		return false;
    }

	out->id = const_cast<char*>(id);
	out->fileName = str::Dup(path::GetBaseNameTemp(localPath));
	out->size = static_cast<long long>(file::GetSize(localPath));
    const char* ft = out->fileName;
	out->fileType = const_cast<char*>(GuessMimeType(ft));
	out->modified = ToISO8601(file::GetModificationTime(localPath));
	out->accessed = ToISO8601(file::GetAccessTime(localPath));
	out->localPath = const_cast<char*>(localPath);
	return true;
}

} // namespace LocalSend
