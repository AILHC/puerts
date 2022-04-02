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
#include "SEHook.h"
#include "Log.h"

#define LIB_VERSION 15

#define API_LEVEL 16

using puerts::FLifeCycleInfo;
using puerts::FResultInfo;
using puerts::FV8Utils;
using puerts::FValue;
using puerts::JSEngine;
using puerts::JSFunction;
using puerts::JsValueType;

typedef void (*SE_CSharpFunctionCallback)(const se::State& state);

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

    V8_EXPORT void SE_SetGlobalFunction(const char* Name, SE_CSharpFunctionCallback Callback)
    {
        SE_BIND_FUNC(Callback)
        se::ScriptEngine::getInstance()->getGlobalObject()->defineFunction(Name, _SE(Callback));
    }
    V8_EXPORT void* GetStateArgs(se::State *state){
        // auto args = state->args();
        // if(args.size()>0){
        //     args.
        // }
    }
    //-------------------------- end debug --------------------------

#ifdef __cplusplus
}
#endif