﻿/*
* Tencent is pleased to support the open source community by making Puerts available.
* Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
* Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may be subject to their corresponding license terms.
* This file is subject to the terms and conditions defined in file 'LICENSE', which is part of this source code package.
*/
#include "JSEngine.h"
#include "V8Utils.h"
#include "Log.h"
#include <memory>
#include "PromiseRejectCallback.hpp"
#include <stdarg.h>

namespace puerts
{
    v8::Local<v8::ArrayBuffer> NewArrayBuffer(v8::Isolate* Isolate, void *Ptr, size_t Size)
    {
        v8::Local<v8::ArrayBuffer> Ab = v8::ArrayBuffer::New(Isolate, Size);
        void* Buff = Ab->GetBackingStore()->Data();
        ::memcpy(Buff, Ptr, Size);
        return Ab;
    }

    static void EvalWithPath(const v8::FunctionCallbackInfo<v8::Value>& Info)
    {
        v8::Isolate* Isolate = Info.GetIsolate();
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = Isolate->GetCurrentContext();
        v8::Context::Scope ContextScope(Context);

        if (Info.Length() != 2 || !Info[0]->IsString() || !Info[1]->IsString())
        {
            FV8Utils::ThrowException(Isolate, "invalid argument for evalScript");
            return;
        }

        v8::Local<v8::String> Source = Info[0]->ToString(Context).ToLocalChecked();
        v8::Local<v8::String> Name = Info[1]->ToString(Context).ToLocalChecked();
        v8::ScriptOrigin Origin(Name);
        auto Script = v8::Script::Compile(Context, Source, &Origin);
        if (Script.IsEmpty())
        {
            return;
        }
        auto Result = Script.ToLocalChecked()->Run(Context);
        if (Result.IsEmpty())
        {
            return;
        }
        Info.GetReturnValue().Set(Result.ToLocalChecked());
    }

    static void GetLastException(const v8::FunctionCallbackInfo<v8::Value>& Info)
    {
        v8::Isolate* Isolate = Info.GetIsolate();
        auto JsEngine = FV8Utils::IsolateData<JSEngine>(Isolate);
        Info.GetReturnValue().Set(JsEngine->LastException.Get(Isolate));
    }

    void JSEngine::SetLastException(v8::Local<v8::Value> Exception)
    {
        LastException.Reset(MainIsolate, Exception);
        LastExceptionInfo = FV8Utils::ExceptionToString(MainIsolate, Exception);
    }

#if WITH_NODEJS
    void JSEngine::JSEngineWithNode()
    {
        // PLog(puerts::Log, "[PuertsDLL][JSEngineWithNode]start");
        if (!GPlatform)
        {
            // PLog(puerts::Log, "[PuertsDLL][JSEngineWithNode]GPlatform");
            int Argc = 2;
            char* ArgvIn[] = {"puerts", "--no-harmony-top-level-await"};
            char ** Argv = uv_setup_args(Argc, ArgvIn);
            Args = new std::vector<std::string>(Argv, Argv + Argc);
            ExecArgs = new std::vector<std::string>();
            Errors = new std::vector<std::string>();

            GPlatform = node::MultiIsolatePlatform::Create(4);
            v8::V8::InitializePlatform(GPlatform.get());
            v8::V8::Initialize();
            int ExitCode = node::InitializeNodeWithArgs(Args, ExecArgs, Errors);
            for (const std::string& error : *Errors)
            {
                printf("InitializeNodeWithArgs failed\n");
            }
        }
        std::string Flags = "";
#if PUERTS_DEBUG
        Flags += "--expose-gc";
#endif
#if PLATFORM_IOS
        Flags += " --jitless --no-expose-wasm";
#endif
        v8::V8::SetFlagsFromString(Flags.c_str(), static_cast<int>(Flags.size()));
        
        NodeUVLoop = new uv_loop_t;
        const int Ret = uv_loop_init(NodeUVLoop);
        if (Ret != 0)
        {
            // TODO log
            printf("uv_loop_init failed\n");
            return;
        }

        NodeArrayBufferAllocator = node::ArrayBufferAllocator::Create();
        // PLog(puerts::Log, "[PuertsDLL][JSEngineWithNode]isolate");

        auto Platform = static_cast<node::MultiIsolatePlatform*>(GPlatform.get());
        MainIsolate = node::NewIsolate(NodeArrayBufferAllocator.get(), NodeUVLoop,
            Platform);

        auto Isolate = MainIsolate;
        ResultInfo.Isolate = MainIsolate;

#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);

        v8::HandleScope HandleScope(Isolate);

        v8::Local<v8::Context> Context = node::NewContext(Isolate);
        // PLog(puerts::Log, "[PuertsDLL][JSEngineWithNode]context");

        v8::Context::Scope ContextScope(Context);
        ResultInfo.Context.Reset(MainIsolate, Context);

        // PLog(puerts::Log, "[PuertsDLL][JSEngineWithNode]isolatedata start");
        NodeIsolateData = node::CreateIsolateData(Isolate, NodeUVLoop, Platform, NodeArrayBufferAllocator.get()); // node::FreeIsolateData
    
        //kDefaultFlags = kOwnsProcessState | kOwnsInspector, if kOwnsInspector set, inspector_agent.cc:681 CHECK_EQ(start_io_thread_async_initialized.exchange(true), false) fail!
        NodeEnv = CreateEnvironment(NodeIsolateData, Context, *Args, *ExecArgs, node::EnvironmentFlags::kOwnsProcessState);

        v8::MaybeLocal<v8::Value> LoadenvRet = node::LoadEnvironment(
            NodeEnv,
            "const publicRequire ="
            "  require('module').createRequire(process.cwd() + '/');"
            "globalThis.require = publicRequire;"
            "require('vm').runInThisContext(process.argv[1]);");

        if (LoadenvRet.IsEmpty())  // There has been a JS exception.
        {
            return;
        }
        // PLog(puerts::Log, "[PuertsDLL][JSEngineWithNode]isolatedata done");

        MainIsolate->SetData(0, this);
        v8::Local<v8::Object> Global = Context->Global();

        Global->Set(Context, FV8Utils::V8String(MainIsolate, "__tgjsEvalScript"), v8::FunctionTemplate::New(MainIsolate, &EvalWithPath)->GetFunction(Context).ToLocalChecked()).Check();

        MainIsolate->SetPromiseRejectCallback(&PromiseRejectCallback<JSEngine>);
        MainIsolate->SetHostInitializeImportMetaObjectCallback(&JSEngine::HostInitializeImportMetaObject);

        Global->Set(Context, FV8Utils::V8String(MainIsolate, "__tgjsSetPromiseRejectCallback"), v8::FunctionTemplate::New(MainIsolate, &SetPromiseRejectCallback<JSEngine>)->GetFunction(Context).ToLocalChecked()).Check();
        Global->Set(Context, FV8Utils::V8String(Isolate, "__puertsGetLastException"), v8::FunctionTemplate::New(Isolate, &GetLastException)->GetFunction(Context).ToLocalChecked()).Check();

        JSObjectIdMap.Reset(MainIsolate, v8::Map::New(MainIsolate));

        //the same as raw v8
        MainIsolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kAuto);
    }
#endif        

#if !WITH_NODEJS
    void JSEngine::JSEngineWithoutNode(void* external_quickjs_runtime, void* external_quickjs_context)
    {
        if (!GPlatform)
        {
            GPlatform = v8::platform::NewDefaultPlatform();
            v8::V8::InitializePlatform(GPlatform.get());
            v8::V8::Initialize();
        }

        std::string Flags = "--no-harmony-top-level-await";
#if PUERTS_DEBUG
        Flags += " --expose-gc";
#endif
#if PLATFORM_IOS
        Flags += " --jitless --no-expose-wasm";
#endif
        v8::V8::SetFlagsFromString(Flags.c_str(), static_cast<int>(Flags.size()));

        v8::StartupData SnapshotBlob;
        SnapshotBlob.data = (const char *)SnapshotBlobCode;
        SnapshotBlob.raw_size = sizeof(SnapshotBlobCode);
        v8::V8::SetSnapshotDataBlob(&SnapshotBlob);

        // 初始化Isolate和DefaultContext
        CreateParams = new v8::Isolate::CreateParams();
        CreateParams->array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
#if WITH_QUICKJS
        MainIsolate = (external_quickjs_runtime == nullptr) ? v8::Isolate::New(*CreateParams) : v8::Isolate::New(external_quickjs_runtime);
#else
        MainIsolate = v8::Isolate::New(*CreateParams);
#endif
        auto Isolate = MainIsolate;
        ResultInfo.Isolate = MainIsolate;
        Isolate->SetData(0, this);

#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope Isolatescope(Isolate);
        v8::HandleScope HandleScope(Isolate);

#if WITH_QUICKJS
        v8::Local<v8::Context> Context = (external_quickjs_runtime && external_quickjs_context) ? v8::Context::New(Isolate, external_quickjs_context) : v8::Context::New(Isolate);
#else
        v8::Local<v8::Context> Context = v8::Context::New(Isolate);
#endif
        v8::Context::Scope ContextScope(Context);
        ResultInfo.Context.Reset(Isolate, Context);
        v8::Local<v8::Object> Global = Context->Global();

        Global->Set(Context, FV8Utils::V8String(Isolate, "__tgjsEvalScript"), v8::FunctionTemplate::New(Isolate, &EvalWithPath)->GetFunction(Context).ToLocalChecked()).Check();

        if (external_quickjs_runtime == nullptr) 
        {
            Isolate->SetPromiseRejectCallback(&PromiseRejectCallback<JSEngine>);
            Global->Set(Context, FV8Utils::V8String(Isolate, "__tgjsSetPromiseRejectCallback"), v8::FunctionTemplate::New(Isolate, &SetPromiseRejectCallback<JSEngine>)->GetFunction(Context).ToLocalChecked()).Check();
            Global->Set(Context, FV8Utils::V8String(Isolate, "__puertsGetLastException"), v8::FunctionTemplate::New(Isolate, &GetLastException)->GetFunction(Context).ToLocalChecked()).Check();
        }
#if !WITH_QUICKJS
        Isolate->SetHostInitializeImportMetaObjectCallback(&JSEngine::HostInitializeImportMetaObject);
#endif

        JSObjectIdMap.Reset(Isolate, v8::Map::New(Isolate));
    }
#endif

    JSEngine::JSEngine(void* external_quickjs_runtime, void* external_quickjs_context)
    {
        GeneralDestructor = nullptr;
        Inspector = nullptr;
#if WITH_NODEJS
        JSEngineWithNode();
#else
        JSEngineWithoutNode(external_quickjs_runtime, external_quickjs_context);
#endif
    }

    JSEngine::~JSEngine()
    {
        if (Inspector)
        {
            delete Inspector;
            Inspector = nullptr;
        }

        JSObjectIdMap.Reset();
        JsPromiseRejectCallback.Reset();
        LastException.Reset();

        for (int i = 0; i < Templates.size(); ++i)
        {
            Templates[i].Reset();
        }
        for (int i = 0; i < Metadatas.size(); ++i)
        {
            Metadatas[i].Reset();
        }

        {
            auto Isolate = MainIsolate;
#ifdef THREAD_SAFE
            v8::Locker Locker(Isolate);
#endif
            v8::Isolate::Scope IsolateScope(Isolate);
            v8::HandleScope HandleScope(Isolate);
            auto Context = ResultInfo.Context.Get(Isolate);
            v8::Context::Scope ContextScope(Context);

            for (auto Iter = ObjectMap.begin(); Iter != ObjectMap.end(); ++Iter)
            {
                auto Value = Iter->second.Get(MainIsolate);
                if (Value->IsObject())
                {
                    auto Object = Value->ToObject(Context).ToLocalChecked();
                    auto LifeCycleInfo = static_cast<FLifeCycleInfo *>(FV8Utils::GetPoninter(Object, 1));
                    if (LifeCycleInfo && LifeCycleInfo->Size > 0)
                    {
                        auto Ptr = FV8Utils::GetPoninter(Object);
                        free(Ptr);
                    }
                }
                Iter->second.Reset();
            }
#if !WITH_QUICKJS
            for (auto Iter = PathToModuleMap.begin(); Iter != PathToModuleMap.end(); ++Iter)
            {
                Iter->second.Reset();
            }
#endif
            PathToModuleMap.clear();
        }
        {
            std::lock_guard<std::mutex> guard(JSFunctionsMutex);
            for (auto Iter = JSFunctions.begin(); Iter != JSFunctions.end(); ++Iter)
            {
                delete *Iter;
            }
        }
        {
            std::lock_guard<std::mutex> guard(JSObjectsMutex);
            for (auto Iter = JSObjectMap.begin(); Iter != JSObjectMap.end(); ++Iter)
            {
                delete Iter->second;
            }
        }
        
#if WITH_NODEJS
        // node::EmitExit(NodeEnv);
        node::Stop(NodeEnv);
        node::FreeEnvironment(NodeEnv);
        node::FreeIsolateData(NodeIsolateData);
        auto Platform = static_cast<node::MultiIsolatePlatform*>(GPlatform.get());
        bool platform_finished = false;
        Platform->AddIsolateFinishedCallback(MainIsolate, [](void* data) {
            *static_cast<bool*>(data) = true;
        }, &platform_finished);
        Platform->UnregisterIsolate(MainIsolate);
#endif

        ResultInfo.Context.Reset();
        ResultInfo.Result.Reset();
        MainIsolate->Dispose();
        MainIsolate = nullptr;

#if WITH_NODEJS
        // Wait until the platform has cleaned up all relevant resources.
        while (!platform_finished)
        {
            uv_run(NodeUVLoop, UV_RUN_ONCE);
        }

        int err = uv_loop_close(NodeUVLoop);
        assert(err == 0);
        delete NodeUVLoop;
#else
        delete CreateParams->array_buffer_allocator;
        delete CreateParams;
#endif

        for (int i = 0; i < CallbackInfos.size(); ++i)
        {
            delete CallbackInfos[i];
        }

        for (int i = 0; i < LifeCycleInfos.size(); ++i)
        {
            delete LifeCycleInfos[i];
        }
    }

    JSObject *JSEngine::CreateJSObject(v8::Isolate *InIsolate, v8::Local<v8::Context> InContext, v8::Local<v8::Object> InObject)
    {
        // PLog(puerts::Log, "[PuertsDLL][CreateJSObject]mutex");
        std::lock_guard<std::mutex> guard(JSObjectsMutex);

        // PLog(puerts::Log, "[PuertsDLL][CreateJSObject]ContextScope");
#ifdef THREAD_SAFE
        v8::Locker Locker(InIsolate);
#endif
        v8::Isolate::Scope IsolateScope(InIsolate);
        v8::HandleScope HandleScope(InIsolate);
        v8::Context::Scope ContextScope(InContext);

        // PLog(puerts::Log, "[PuertsDLL][CreateJSObject]map get");
        v8::Local<v8::Map> idmap = JSObjectIdMap.Get(InIsolate);
        
        // PLog(puerts::Log, "[PuertsDLL][CreateJSObject]get v8object id");
        // 从idmap尝试取出该jsObject的id
        v8::Local<v8::Value> v8ObjectIndex = idmap->Get(InContext, InObject).ToLocalChecked();
        JSObject* jsObject = nullptr;

        // PLog(puerts::Log, "[PuertsDLL][CreateJSObject]get jsobject");
        // 如果存在该id，则从objectmap里取出该对象
        if (!v8ObjectIndex->IsNullOrUndefined())
        {
            int32_t mapIndex = (int32_t)v8::Number::Cast(*v8ObjectIndex)->Value();
            auto iter = JSObjectMap.find(mapIndex);
            if (iter != JSObjectMap.end())
            {
                jsObject = iter->second;
            }
        }

        // 如果不存在id，则创建新对象
        if (jsObject == nullptr) 
        {
            int32_t id = 0;
            size_t freeIDSize = ObjectMapFreeIndex.size();
            if (freeIDSize > 0) {
                id = ObjectMapFreeIndex[freeIDSize - 1];
                ObjectMapFreeIndex.pop_back();
            }
            else
            {
                id = JSObjectMap.size();
            }
            jsObject = new JSObject(InIsolate, InContext, InObject, id);
            JSObjectMap[id] = jsObject;
            idmap->Set(InContext, InObject, v8::Number::New(InIsolate, id));
        }

        return jsObject;
    }

    void JSEngine::ReleaseJSObject(JSObject *InObject)
    {
        std::lock_guard<std::mutex> guard(JSObjectsMutex);

        // PLog(puerts::Log, std::to_string((long)InObject));
        v8::Isolate* Isolate = InObject->Isolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = InObject->Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        v8::Local<v8::Map> idmap = JSObjectIdMap.Get(InObject->Isolate);
        idmap->Delete(InObject->Context.Get(Isolate), InObject->GObject.Get(Isolate));
        JSObjectMap.erase(InObject->Index);

        ObjectMapFreeIndex.push_back(InObject->Index);
        delete InObject;
    }

    JSFunction* JSEngine::CreateJSFunction(v8::Isolate* InIsolate, v8::Local<v8::Context> InContext, v8::Local<v8::Function> InFunction)
    {
        std::lock_guard<std::mutex> guard(JSFunctionsMutex);
        auto maybeId = InFunction->Get(InContext, FV8Utils::V8String(InIsolate, FUNCTION_INDEX_KEY));
        if (!maybeId.IsEmpty()) {
            auto id = maybeId.ToLocalChecked();
            if (id->IsNumber()) {
                int32_t index = id->Int32Value(InContext).ToChecked();
                return JSFunctions[index];
            }
        }
        JSFunction* Function = nullptr;
        for (int i = 0; i < JSFunctions.size(); i++) {
            if (!JSFunctions[i]) {
                Function = new JSFunction(InIsolate, InContext, InFunction, i);
                JSFunctions[i] = Function;
                break;
            }
        }
        if (!Function) {
            Function = new JSFunction(InIsolate, InContext, InFunction, static_cast<int32_t>(JSFunctions.size()));
            JSFunctions.push_back(Function);
        }
        InFunction->Set(InContext, FV8Utils::V8String(InIsolate, FUNCTION_INDEX_KEY), v8::Integer::New(InIsolate, Function->Index));
        return Function;
    }

    void JSEngine::ReleaseJSFunction(JSFunction* InFunction)
    {
        std::lock_guard<std::mutex> guard(JSFunctionsMutex);
        JSFunctions[InFunction->Index] = nullptr;
        delete InFunction;
    }

    static void CSharpFunctionCallbackWrap(const v8::FunctionCallbackInfo<v8::Value>& Info)
    {
        v8::Isolate* Isolate = Info.GetIsolate();
        v8::Local<v8::Context> Context = Isolate->GetCurrentContext();

        FCallbackInfo* CallbackInfo = reinterpret_cast<FCallbackInfo*>((v8::Local<v8::External>::Cast(Info.Data()))->Value());

        void* Ptr = CallbackInfo->IsStatic ? nullptr : FV8Utils::GetPoninter(Info.Holder());

        CallbackInfo->Callback(Isolate, Info, Ptr, Info.Length(), CallbackInfo->Data);
    }

    v8::Local<v8::FunctionTemplate> JSEngine::ToTemplate(v8::Isolate* Isolate, bool IsStatic, CSharpFunctionCallback Callback, int64_t Data)
    {
        auto Pos = CallbackInfos.size();
        auto CallbackInfo = new FCallbackInfo(IsStatic, Callback, Data);
        CallbackInfos.push_back(CallbackInfo);
        return v8::FunctionTemplate::New(Isolate, CSharpFunctionCallbackWrap, v8::External::New(Isolate, CallbackInfos[Pos]));
    }

    void JSEngine::SetGlobalFunction(const char *Name, CSharpFunctionCallback Callback, int64_t Data)
    {
        v8::Isolate* Isolate = MainIsolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = ResultInfo.Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        v8::Local<v8::Object> Global = Context->Global();

        Global->Set(Context, FV8Utils::V8String(Isolate, Name), ToTemplate(Isolate, true, Callback, Data)->GetFunction(Context).ToLocalChecked()).Check();
    }

    static void NewWrap(const v8::FunctionCallbackInfo<v8::Value>& Info)
    {
        v8::Isolate* Isolate = Info.GetIsolate();
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = Isolate->GetCurrentContext();
        v8::Context::Scope ContextScope(Context);

        if (Info.IsConstructCall())
        {
            auto Self = Info.This();
            auto LifeCycleInfo = FV8Utils::ExternalData<FLifeCycleInfo>(Info);
            void *Ptr = nullptr;

            if (Info[0]->IsExternal()) //Call by Native
            {
                Ptr = v8::Local<v8::External>::Cast(Info[0])->Value();
            }
            else // Call by js new
            {
                if (LifeCycleInfo->Constructor) Ptr = LifeCycleInfo->Constructor(Isolate, Info, Info.Length(), LifeCycleInfo->Data);
            }
            FV8Utils::IsolateData<JSEngine>(Isolate)->BindObject(LifeCycleInfo, Ptr, Self);
        }
        else
        {
            FV8Utils::ThrowException(Isolate, "only call as Construct is supported!");
        }
    }

    static void OnGarbageCollected(const v8::WeakCallbackInfo<FLifeCycleInfo>& Data)
    {
        FV8Utils::IsolateData<JSEngine>(Data.GetIsolate())->UnBindObject(Data.GetParameter(), Data.GetInternalField(0));
    }

    int JSEngine::RegisterClass(const char *FullName, int BaseClassId, CSharpConstructorCallback Constructor, CSharpDestructorCallback Destructor, int64_t Data, int Size)
    {
        auto Iter = NameToTemplateID.find(FullName);
        if (Iter != NameToTemplateID.end())
        {
            return Iter->second;
        }

        v8::Isolate* Isolate = MainIsolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = ResultInfo.Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        int ClassId = static_cast<int>(Templates.size());

        auto Pos = LifeCycleInfos.size();
        auto LifeCycleInfo = new FLifeCycleInfo(ClassId, Constructor, Destructor ? Destructor : GeneralDestructor, Data, Size);
        LifeCycleInfos.push_back(LifeCycleInfo);
        
        auto Template = v8::FunctionTemplate::New(Isolate, NewWrap, v8::External::New(Isolate, LifeCycleInfos[Pos]));
        
        Template->InstanceTemplate()->SetInternalFieldCount(3);//1: object id, 2: type id, 3: magic
        Templates.push_back(v8::UniquePersistent<v8::FunctionTemplate>(Isolate, Template));
        auto Map = v8::Map::New(Isolate);
        Metadatas.push_back(v8::UniquePersistent<v8::Map>(Isolate, Map));

        NameToTemplateID[FullName] = ClassId;
        Map->Set(Context, FV8Utils::V8String(Isolate, "classid"), v8::Number::New(Isolate, ClassId));
        Template->SetClassName(FV8Utils::V8String(Isolate, FullName));

        if (BaseClassId >= 0)
        {
            Template->Inherit(Templates[BaseClassId].Get(Isolate));
        }
        return ClassId;
    }

    bool JSEngine::RegisterFunction(int ClassID, const char *Name, bool IsStatic, CSharpFunctionCallback Callback, int64_t Data)
    {
        v8::Isolate* Isolate = MainIsolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = ResultInfo.Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        if (ClassID >= Templates.size()) return false;

        if (IsStatic)
        {
            Templates[ClassID].Get(Isolate)->Set(FV8Utils::V8String(Isolate, Name), ToTemplate(Isolate, IsStatic, Callback, Data));
        }
        else
        {
            Templates[ClassID].Get(Isolate)->PrototypeTemplate()->Set(FV8Utils::V8String(Isolate, Name), ToTemplate(Isolate, IsStatic, Callback, Data));
        }

        return true;
    }

    bool JSEngine::RegisterProperty(int ClassID, const char *Name, bool IsStatic, CSharpFunctionCallback Getter, int64_t GetterData, CSharpFunctionCallback Setter, int64_t SetterData, bool NotReadonlyStatic)
    {
        v8::Isolate* Isolate = MainIsolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = ResultInfo.Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        if (ClassID >= Templates.size()) return false;

        auto Attr = (Setter == nullptr) ? v8::ReadOnly : v8::None;

        if (!NotReadonlyStatic) 
        {
            v8::Local<v8::Map> Metadata = Metadatas[ClassID].Get(Isolate);
            v8::Local<v8::Set> ReadonlyStaticMembersSet;
            v8::Local<v8::Value> NameOfTheSet = FV8Utils::V8String(Isolate, "readonlyStaticMembers");
            v8::Local<v8::Value> ReadonlyStaticMembersSetValue = Metadata->Get(Context, NameOfTheSet).ToLocalChecked();
            if (ReadonlyStaticMembersSetValue->IsNullOrUndefined())
            {
                ReadonlyStaticMembersSet = v8::Set::New(Isolate);
                Metadata->Set(Context, NameOfTheSet, ReadonlyStaticMembersSet);
            }
            else
            {
                ReadonlyStaticMembersSet = v8::Local<v8::Set>::Cast(ReadonlyStaticMembersSetValue);
            }
            ReadonlyStaticMembersSet->Add(Context, FV8Utils::V8String(Isolate, Name));
        }

        if (IsStatic)
        {
            Templates[ClassID].Get(Isolate)->SetAccessorProperty(FV8Utils::V8String(Isolate, Name), ToTemplate(Isolate, IsStatic, Getter, GetterData)
                , Setter == nullptr ? v8::Local<v8::FunctionTemplate>() : ToTemplate(Isolate, IsStatic, Setter, SetterData), Attr);
        }
        else
        {
            Templates[ClassID].Get(Isolate)->PrototypeTemplate()->SetAccessorProperty(FV8Utils::V8String(Isolate, Name),
                ToTemplate(Isolate, IsStatic, Getter, GetterData)
                , Setter == nullptr ? v8::Local<v8::FunctionTemplate>() : ToTemplate(Isolate, IsStatic, Setter, SetterData), Attr);
        }

        return true;
    }

    v8::Local<v8::Value> JSEngine::GetClassConstructor(int ClassID)
    {
        v8::Isolate* Isolate = MainIsolate;
        if (ClassID >= Templates.size()) return v8::Undefined(Isolate);

        auto Context = Isolate->GetCurrentContext();

        auto Result = Templates[ClassID].Get(Isolate)->GetFunction(Context).ToLocalChecked();
        Result->Set(Context, FV8Utils::V8String(Isolate, "__puertsMetadata"), Metadatas[ClassID].Get(Isolate));
        return Result;
    }

    v8::Local<v8::Value> JSEngine::FindOrAddObject(v8::Isolate* Isolate, v8::Local<v8::Context> Context, int ClassID, void *Ptr)
    {
        if (!Ptr)
        {
            return v8::Undefined(Isolate);
        }

        auto Iter = ObjectMap.find(Ptr);
        if (Iter == ObjectMap.end())//create and link
        {
            auto BindTo = v8::External::New(Context->GetIsolate(), Ptr);
            v8::Local<v8::Value> Args[] = { BindTo };
            return Templates[ClassID].Get(Isolate)->GetFunction(Context).ToLocalChecked()->NewInstance(Context, 1, Args).ToLocalChecked();
        }
        else
        {
            return v8::Local<v8::Value>::New(Isolate, Iter->second);
        }
    }

    void JSEngine::BindObject(FLifeCycleInfo* LifeCycleInfo, void* Ptr, v8::Local<v8::Object> JSObject)
    {
        if (LifeCycleInfo->Size > 0)
        {
            void *Val = malloc(LifeCycleInfo->Size);
            if (Ptr != nullptr)
            {
                memcpy(Val, Ptr, LifeCycleInfo->Size);
            }
            JSObject->SetAlignedPointerInInternalField(0, Val);
            Ptr = Val;
        }
        else
        {
            JSObject->SetAlignedPointerInInternalField(0, Ptr);
        }
        
        JSObject->SetAlignedPointerInInternalField(1, LifeCycleInfo);
        JSObject->SetAlignedPointerInInternalField(2, reinterpret_cast<void *>(OBJECT_MAGIC));
        v8::UniquePersistent<v8::Value> persistent(MainIsolate, JSObject);
        persistent.SetWeak<FLifeCycleInfo>(LifeCycleInfo, OnGarbageCollected, v8::WeakCallbackType::kInternalFields);
        ObjectMap[Ptr] = std::move(persistent);
    }

    void JSEngine::UnBindObject(FLifeCycleInfo* LifeCycleInfo, void* Ptr)
    {
        ObjectMap.erase(Ptr);

        if (LifeCycleInfo->Size > 0)
        {
            free(Ptr);
        }
        else
        {
            if (LifeCycleInfo->Destructor)
            {
                LifeCycleInfo->Destructor(Ptr, LifeCycleInfo->Data);
            }
        }
    }

    void JSEngine::LowMemoryNotification()
    {
        MainIsolate->LowMemoryNotification();
    }

    bool JSEngine::IdleNotificationDeadline(double DeadlineInSeconds)
    {
#ifndef WITH_QUICKJS
        return MainIsolate->IdleNotificationDeadline(DeadlineInSeconds);
#else
        return true;
#endif
    }

    void JSEngine::RequestMinorGarbageCollectionForTesting()
    {
#ifndef WITH_QUICKJS
        MainIsolate->RequestGarbageCollectionForTesting(v8::Isolate::kMinorGarbageCollection);
#endif
    }

    void JSEngine::RequestFullGarbageCollectionForTesting()
    {
#ifndef WITH_QUICKJS
        MainIsolate->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection);
#endif
    }

    void JSEngine::CreateInspector(int32_t Port)
    {
        v8::Isolate* Isolate = MainIsolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = ResultInfo.Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        if (Inspector == nullptr)
        {
            Inspector = CreateV8Inspector(Port, &Context);
        }
    }

    void JSEngine::DestroyInspector()
    {
        v8::Isolate* Isolate = MainIsolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = ResultInfo.Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        if (Inspector != nullptr)
        {
            delete Inspector;
            Inspector = nullptr;
        }
    }

    void JSEngine::LogicTick()
    {
#if WITH_NODEJS

        v8::Isolate* Isolate = MainIsolate;
#ifdef THREAD_SAFE
        v8::Locker Locker(Isolate);
#endif
        v8::Isolate::Scope IsolateScope(Isolate);
        v8::HandleScope HandleScope(Isolate);
        v8::Local<v8::Context> Context = ResultInfo.Context.Get(Isolate);
        v8::Context::Scope ContextScope(Context);

        uv_run(NodeUVLoop, UV_RUN_NOWAIT);
        static_cast<node::MultiIsolatePlatform*>(GPlatform.get())->DrainTasks(Isolate);
#endif
    }

    bool JSEngine::InspectorTick()
    {
        if (Inspector != nullptr)
        {
            return Inspector->Tick();
        }
        return true;
    }
}
