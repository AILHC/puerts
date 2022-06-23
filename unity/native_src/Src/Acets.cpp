/*
 * Tencent is pleased to support the open source community by making Puerts available.
 * Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
 * Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may
 * be subject to their corresponding license terms. This file is subject to the terms and conditions defined in file 'LICENSE',
 * which is part of this source code package.
 */
#include "JSEngine.h"
#include <cstring>
#include "V8Utils.h"
#include "Base.h"
#include "jswrapper/include/jswrapper/SeApi.h"

#include "Log.h"
#include "Acets.h"
#include "jswrapper/include/jswrapper/v8/Utils.h"

#define LIB_VERSION 15

#define API_LEVEL 16

using acets::SE_FuncCallbackInfo;
using puerts::FLifeCycleInfo;
using puerts::FResultInfo;
using puerts::FV8Utils;
using puerts::FValue;
using puerts::JSEngine;
using puerts::JSFunction;
using puerts::JsValueType;
#ifdef __cplusplus
extern "C"
{
#endif

    V8_EXPORT void SE_Setup()
    {
        auto se = se::ScriptEngine::getInstance();
        se->start();
    }

    V8_EXPORT bool SE_EvalString(const char* str)
    {
        return se::ScriptEngine::getInstance()->evalString(str);
    }

    V8_EXPORT void SE_Destroy(se::ScriptEngine* se)
    {
        se::ScriptEngine::destroyInstance();
    }
    void CSharpFunctionCallbackWrap(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* _isolate = info.GetIsolate();
        v8::HandleScope _hs(_isolate);
        se::ValueArray& args = se::gValueArrayPool.get(info.Length());
        se::CallbackDepthGuard depthGuard{args, se::gValueArrayPool._depth};
        se::internal::jsToSeArgs(info, args);
        se::PrivateObjectBase* privateObject =
            static_cast<se::PrivateObjectBase*>(se::internal::getPrivate(_isolate, info.This(), 0));
        se::Object* thisObject = reinterpret_cast<se::Object*>(se::internal::getPrivate(_isolate, info.This(), 1));
        se::State state(thisObject, privateObject, args);

        SE_FuncCallbackInfo* cbInfo = reinterpret_cast<SE_FuncCallbackInfo*>((v8::Local<v8::External>::Cast(info.Data()))->Value());
        bool ret = false;
        ret = cbInfo->Callback(state);
        if (!ret)
        {
            SE_LOGE("[ERROR] Failed to invoke %s, location: %s:%d\n", cbInfo->name, __FILE__, __LINE__);
        }

        se::internal::setReturnValue(state.rval(), info);
    }
    V8_EXPORT void SE_SetGlobalFunction(const char* name, SE_CSharpFunctionCallback callback)
    {
        auto info = new SE_FuncCallbackInfo(false, callback, name);
        SE_LOGD("define func0");
        se::AutoHandleScope hs;
        se::ScriptEngine::getInstance()->getGlobalObject()->defineFunction(name, CSharpFunctionCallbackWrap, info);
    }
    V8_EXPORT void SE_InvokeJSFunction(se::State* state, void* args)
    {
        
    }
    //--------------------------------------------return value to js -----------------------------------------------
    // V8_EXPORT void ReturnClass(v8::Isolate* Isolate, const v8::FunctionCallbackInfo<v8::Value>& Info, int ClassID)
    // {
    //     auto JsEngine = FV8Utils::IsolateData<JSEngine>(Isolate);
    //     Info.GetReturnValue().Set(JsEngine->GetClassConstructor(ClassID));
    // }

    // V8_EXPORT void ReturnObject(v8::Isolate* Isolate, const v8::FunctionCallbackInfo<v8::Value>& Info, int ClassID, void* Ptr)
    // {
    //     auto JsEngine = FV8Utils::IsolateData<JSEngine>(Isolate);
    //     Info.GetReturnValue().Set(JsEngine->FindOrAddObject(Isolate, Isolate->GetCurrentContext(), ClassID, Ptr));
    // }

    V8_EXPORT void SE_ReturnNumber(se::State& state, double Number)
    {
        state.rval().setDouble(Number);
    }

    V8_EXPORT void SE_ReturnString(se::State& state, const char* String)
    {
        state.rval().setString(String);
    }

    V8_EXPORT void SE_ReturnBigInt(se::State& state, int64_t BigInt)
    {
        state.rval().setInt64(BigInt);
    }

    V8_EXPORT void SE_ReturnArrayBuffer(se::State& state, unsigned char* Bytes, int Length)
    {
        se::HandleObject obj(se::Object::createArrayBufferObject(Bytes, Length));
        state.rval().setObject(obj);
    }

    V8_EXPORT void SE_ReturnBoolean(se::State& state, bool Bool)
    {
        state.rval().setBoolean(Bool);
    }

    // V8_EXPORT void SE_ReturnDate(se::State&, double Date)
    // {
    //     Info.GetReturnValue().Set(v8::Date::New(Isolate->GetCurrentContext(), Date).ToLocalChecked());
    // }

    V8_EXPORT void SE_ReturnNull(se::State& state)
    {
        state.rval().setNull();
    }

    // V8_EXPORT void ReturnFunction(se::State& state)
    // {
    //     Info.GetReturnValue().Set(Function->GFunction.Get(Isolate));
    // }

    V8_EXPORT void SE_ReturnJSObject(se::State& state, se::Object* o)
    {
        state.rval().setObject(o);
    }

    //--------------------------------------------return value to js end-----------------------------------------------

#ifdef __cplusplus
}
#endif