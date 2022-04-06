#include "../jswrapper/include/jswrapper/SeApi.h"
typedef void (*SE_CSharpFunctionCallback)(const se::State& state);
namespace Acets
{
namespace
{
void CSharpFunctionCallbackWrap(const v8::FunctionCallbackInfo<v8::Value>& info)
{
}
bool defineJsObjFunction(se::Object obj, const char* Name,SE_CSharpFunctionCallback callback)
{
    
}
}    // namespace

}    // namespace Acets