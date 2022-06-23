/*
* Tencent is pleased to support the open source community by making Puerts available.
* Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
* Puerts is licensed under the BSD 3-Clause License, except for the third-party components listed in the file 'LICENSE' which may be subject to their corresponding license terms. 
* This file is subject to the terms and conditions defined in file 'LICENSE', which is part of this source code package.
*/

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Acets
{
#pragma warning disable 414
    public class MonoPInvokeCallbackAttribute : System.Attribute
    {
        private Type type;
        public MonoPInvokeCallbackAttribute(Type t)
        {
            type = t;
        }
    }
#pragma warning restore 414

#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN || PUERTS_GENERAL || (UNITY_WSA && !UNITY_EDITOR)
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
#endif
    public delegate bool SEV8FunctionCallback(IntPtr state);



#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN || PUERTS_GENERAL || (UNITY_WSA && !UNITY_EDITOR)
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
#endif
    public delegate IntPtr V8ConstructorCallback(IntPtr isolate, IntPtr info, int paramLen, long data);

#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN || PUERTS_GENERAL || (UNITY_WSA && !UNITY_EDITOR)
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
#endif
    public delegate string ModuleResolveCallback(string identifer, int jsEnvIdx);

#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN || PUERTS_GENERAL || (UNITY_WSA && !UNITY_EDITOR)
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
#endif
    public delegate void PushJSFunctionArgumentsCallback(IntPtr isolate, int jsEnvIdx, IntPtr nativeJsFuncPtr);

#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN || PUERTS_GENERAL || (UNITY_WSA && !UNITY_EDITOR)
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
#endif
    public delegate void V8DestructorCallback(IntPtr self, long data);

#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN || PUERTS_GENERAL || (UNITY_WSA && !UNITY_EDITOR)
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
#endif
    public delegate void LogCallback(string content);

    [Flags]
    public enum JsValueType
    {
        Invalid = 0,
        NullOrUndefined = 1,
        BigInt = 2,
        Number = 4,
        String = 8,
        Boolean = 16,
        NativeObject = 32,
        JsObject = 64,
        Array = 128,
        Function = 256,
        Date = 512,
        ArrayBuffer = 1024,
        Unknow = 2048,
        Any = NullOrUndefined | BigInt | Number | String | Boolean | NativeObject | JsObject | Array | Function | Date | ArrayBuffer,
    };

    public class AcetsDLL
    {
#if (UNITY_IPHONE || UNITY_TVOS || UNITY_WEBGL || UNITY_SWITCH) && !UNITY_EDITOR
        const string DLLNAME = "__Internal";
#else
        const string DLLNAME = "puerts";
#endif

        [DllImport(DLLNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "GetApiLevel")]
        protected static extern int _GetApiLevel();

        [DllImport(DLLNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GetLibVersion();

        public static int GetApiLevel()
        {
            try
            {
                return _GetApiLevel();
            }
            catch (Exception e)
            {
                return GetLibVersion();
            }
        }
        //AILHC 创建cocosjs引擎 -DLL
        [DllImport(DLLNAME, CallingConvention = CallingConvention.Cdecl, EntryPoint = "SE_Setup")]
        public static extern void SE_Setup();

        //AILHC 销毁cocosjs引擎 -DLL

        [DllImport(DLLNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SE_Destroy();
        //AILHC 执行js代码
        [DllImport(DLLNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool SE_EvalString(string str);



        //AILHC cocosjs引擎设置全局函数 -DLL
        [DllImport(DLLNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SE_SetGlobalFunction(string name, IntPtr seV8FunctionCallback);
        // AILHC 向cocosjs引擎注入C#方法
        public static void SE_SetGlobalFunction(string name, SEV8FunctionCallback seV8FunctionCallback)
        {
#if PUERTS_GENERAL || (UNITY_WSA && !UNITY_EDITOR)
            GCHandle.Alloc(seV8FunctionCallback);
#endif
            IntPtr fn = seV8FunctionCallback == null ? IntPtr.Zero : Marshal.GetFunctionPointerForDelegate(seV8FunctionCallback);
            SE_SetGlobalFunction(name, fn);
        }


        [DllImport(DLLNAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SE_ReturnNumber(IntPtr state, double number);
    }
}


