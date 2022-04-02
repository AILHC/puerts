#include "Log.h"
#define SE_LOGD(...) \
    PLog(puerts::Log,__VA_ARGS__) \

#define SE_LOGE(...)                \
    PLog(puerts::Error,__VA_ARGS__)         \
