#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "utils/BaseUtil.h"
#include "utils/FileUtil.h"
#include "utils/protocol/Fingerprint.h"

namespace LocalSend {

// 支持 Windows, macOS, Linux
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
#include <stdlib.h> // arc4random_buf
#else
// Assume POSIX (Linux, BSD, etc.)
#include <sys/random.h> // getrandom (Linux >=3.17)
#include <errno.h>
#endif

// 尝试从系统获取加密安全的随机字节
static int secure_random_bytes(void* buf, size_t len) {
#if defined(_WIN32)
    NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return (status == 0) ? 0 : -1;

#elif defined(__APPLE__)
    arc4random_buf(buf, len);
    return 0;

#elif defined(__linux__) && defined(SYS_getrandom)
    // Try getrandom first (Linux >=3.17)
    ssize_t ret;
    while ((ret = getrandom(buf, len, GRND_NONBLOCK)) != (ssize_t)len) {
        if (ret < 0) {
            if (errno == EINTR) continue;
            break; // fallback to /dev/urandom
        }
        buf = (char*)buf + ret;
        len -= ret;
    }
    if (ret == (ssize_t)len) return 0;
    // fallback below
#else

    // Fallback: /dev/urandom (POSIX)
    FILE* fp = fopen("/dev/urandom", "rb");
    if (!fp) return -1;
    size_t read = fread(buf, 1, len, fp);
    fclose(fp);
    return (read == len) ? 0 : -1;
#endif
}

// 将字节数组转为小写十六进制字符串
static void bytes_to_hex(const uint8_t* bytes, size_t len, char* hex_out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex_out[i * 2] = hex_chars[(bytes[i] >> 4) & 0xF];
        hex_out[i * 2 + 1] = hex_chars[bytes[i] & 0xF];
    }
    hex_out[len * 2] = '\0';
}

// 主函数：生成 64 字符十六进制指纹（对应 32 字节随机数据）
char* GenerateFingerprint(void) {
    const size_t raw_len = 32; // 32 bytes → 64 hex chars
    const size_t hex_len = raw_len * 2;

    uint8_t* raw = (uint8_t*)malloc(raw_len);
    char* hex = (char*)malloc(hex_len + 1); // +1 for null terminator

    if (!raw || !hex) {
        free(raw);
        free(hex);
        return NULL;
    }

    if (secure_random_bytes(raw, raw_len) != 0) {
        free(raw);
        free(hex);
        return NULL; // 随机源失败
    }

    bytes_to_hex(raw, raw_len, hex);
    free(raw);
    return hex; // 调用者负责 free()
}

// 假设已有此函数（C 风格）
// extern char* GenerateFingerprint(void);

// 辅助：从 ByteSlice 提取第一行（不含换行符），返回 malloc 分配的 null-terminated 字符串
static char* extract_first_line(const ByteSlice* bs) {
    if (!bs || !bs->data() || bs->size() == 0) {
        return nullptr;
    }

    const char* data = (const char*)bs->data();
    size_t len = bs->size();

    // 查找第一个换行符
    size_t line_len = 0;
    while (line_len < len && data[line_len] != '\n' && data[line_len] != '\r') {
        line_len++;
    }

    // 如果整行为空
    if (line_len == 0) {
        return nullptr;
    }

    // char* line = (char*)malloc(line_len + 1);
    // if (!line) {
    //     return NULL;
    // }
    // memcpy(line, data, line_len);
    // line[line_len] = '\0';

    // 可选：trim trailing \r (for \r\n)
    // if (line_len > 0 && line[line_len - 1] == '\r') {
    //     line[line_len - 1] = '\0';
    // }
    char* line = str::Dup(data, line_len);

    return line;
}

// 主函数：加载或创建指纹（Windows + file:: API）
char* LoadOrCreateFingerprint(const char* path) {
    if (!path) {
        return nullptr;
    }

    // Step 1: 尝试读取文件
    ByteSlice bs = file::ReadFile(path);
    if (bs.data() != nullptr) {
        char* first_line = extract_first_line(&bs);
        if (first_line && str::Len(first_line) > 0) {
            // 成功加载非空指纹
            return first_line;
        }
        str::Free(first_line); // 可能为 NULL，但 safe
    }

    // Step 2: 生成新指纹
    char* fp = GenerateFingerprint();
    if (!fp) {
        return nullptr;
    }

    // Step 3: 构造 "fp\n" 内容用于写入
    size_t fp_len = strlen(fp);
    size_t total_len = fp_len + 1; // +1 for '\n'
    // char* buf = (char*)malloc(total_len);
    // if (!buf) {
    //     free(fp);
    //     return NULL;
    // }
    // memcpy(buf, fp, fp_len);
    // buf[fp_len] = '\n';
    TempStr buf = str::FormatTemp("%s\n", fp);

    // Step 4: 写入文件
    ByteSlice to_write = {(u8*)buf, total_len};
    bool ok = file::WriteFile(path, to_write);
    if (!ok) {
        // printf("Writing fingerprint: %s", buf);
        printf("Failed writing fingerprint: %s", buf);
    }
    // free(buf); // WriteFile 应已拷贝数据（根据其实现）

    // 即使写入失败，仍返回生成的指纹（与原逻辑一致）
    return fp;
}

// char*
// GenerateFingerprint()
// {
// 	std::random_device rd;
// 	std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd());
// 	std::uniform_int_distribution<int> dist(0, 15);
// 	static const char* hex = "0123456789abcdef";
// 	char* out;
// 	out.reserve(64);
// 	for (int k = 0; k < 64; ++k)
// 		out += hex[dist(gen)];
// 	return out;
// }

// char*
// LoadOrCreateFingerprint(const std::string& path)
// {
// 	{
// 		std::ifstream in(path);
// 		if (in) {
// 			std::string fp;
// 			std::getline(in, fp);
// 			if (!fp.empty())
// 				return fp;
// 		}
// 	}
// 	char* fp = GenerateFingerprint();
// 	std::ofstream out(path);
// 	if (out)
// 		out << fp << "\n";
// 	return fp;
// }
//
} // namespace LocalSend
