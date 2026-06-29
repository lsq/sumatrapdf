#include "utils/HttpUtil.h"
int LocalSender(const char* host, StrVec& p, const Func1<UploadProgress*>& cbProgress);
bool ToRelativePaths(StrVec& absPaths, StrVec& relPaths);
