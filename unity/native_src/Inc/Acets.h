#include "Log.h"
#define SE_LOGD(...) PLog(puerts::Log, __VA_ARGS__)

#define SE_LOGE(...) PLog(puerts::Error, __VA_ARGS__)

typedef bool (*SE_CSharpFunctionCallback)(const se::State& state);
namespace acets
{
struct SE_FuncCallbackInfo
{
    SE_FuncCallbackInfo(bool InIsStatic, SE_CSharpFunctionCallback InCallback,const char* name)
        : IsStatic(InIsStatic), Callback(InCallback), name(name)
    {
    }
    
    bool IsStatic;
    SE_CSharpFunctionCallback Callback;
    const char* name;
};
}    // namespace acets