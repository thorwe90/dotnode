#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <iostream>

#include <napi.h>

#include "coreclrhost.h"

#define MANAGED_ASSEMBLY "ManagedLibrary.dll"

// Define OS-specific items like the CoreCLR library's name and path elements
#if WINDOWS
#include <Windows.h>
#define FS_SEPARATOR "\\"
#define PATH_DELIMITER ";"
#define CORECLR_FILE_NAME "coreclr.dll"
#elif LINUX
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#define FS_SEPARATOR "/"
#define PATH_DELIMITER ":"
#define MAX_PATH PATH_MAX
#if OSX
// For OSX, use Linux defines except that the CoreCLR runtime
// library has a different name
#define CORECLR_FILE_NAME "libcoreclr.dylib"
#else
#define CORECLR_FILE_NAME "libcoreclr.so"
#endif
#endif

// Function pointer types for the managed call and callback
typedef int (*report_callback_ptr)(int progress);
typedef char *(*dotnode_function)(const char *json);

void BuildTpaList(const char *directory, const char *extension, std::string &tpaList);

struct CoreClrInfo
{
    coreclr_initialize_ptr initializeCoreClr;
    coreclr_create_delegate_ptr createManagedDelegate;
    coreclr_shutdown_ptr shutdownCoreClr;
    unsigned int domainId;
    void *hostHandle;
};

static CoreClrInfo *coreClrInfo = nullptr;

Napi::Value
initializeDotnode(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (coreClrInfo != nullptr)
    {
        Napi::TypeError::New(env, "dotnode was already initialized.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() != 1)
    {
        Napi::TypeError::New(env, "Invalid argument count.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::String jsRuntimePath = info[0].ToString();
    char runtimePath[MAX_PATH];
    strcpy(runtimePath, jsRuntimePath.Utf8Value().c_str());

    // Construct the CoreCLR path
    // For this sample, we know CoreCLR's path. For other hosts,
    // it may be necessary to probe for coreclr.dll/libcoreclr.so
    std::string coreClrPath(runtimePath);
    coreClrPath.append(FS_SEPARATOR);
    coreClrPath.append(CORECLR_FILE_NAME);

    // Construct the managed library path
    std::string managedLibraryPath(runtimePath);
    managedLibraryPath.append(FS_SEPARATOR);
    managedLibraryPath.append(MANAGED_ASSEMBLY);

#if WINDOWS
    // <Snippet1>
    HMODULE coreClr = LoadLibraryExA(coreClrPath.c_str(), NULL, 0);
    // </Snippet1>
#elif LINUX
    void *coreClr = dlopen(coreClrPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (coreClr == nullptr)
    {
        Napi::Error::New(env, "Failed to load CoreCLR.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    coreClrInfo = new CoreClrInfo();

#if WINDOWS
    // <Snippet2>
    coreClrInfo->initializeCoreClr = (coreclr_initialize_ptr)GetProcAddress(coreClr, "coreclr_initialize");
    coreClrInfo->createManagedDelegate = () GetProcAddress(coreClr, "coreclr_create_delegate");
    coreClrInfo->shutdownCoreClr = (coreclr_shutdown_ptr)GetProcAddress(coreClr, "coreclr_shutdown");
    // </Snippet2>
#elif LINUX
    coreClrInfo->initializeCoreClr = (coreclr_initialize_ptr)dlsym(coreClr, "coreclr_initialize");
    coreClrInfo->createManagedDelegate = (coreclr_create_delegate_ptr)dlsym(coreClr, "coreclr_create_delegate");
    coreClrInfo->shutdownCoreClr = (coreclr_shutdown_ptr)dlsym(coreClr, "coreclr_shutdown");
#endif

    if (coreClrInfo->initializeCoreClr == NULL)
    {
        delete coreClrInfo;
        Napi::Error::New(env, "coreclr_initialize not found.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (coreClrInfo->createManagedDelegate == NULL)
    {
        delete coreClrInfo;
        Napi::Error::New(env, "coreclr_create_delegate not found.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (coreClrInfo->shutdownCoreClr == NULL)
    {
        delete coreClrInfo;
        Napi::Error::New(env, "coreclr_shutdown not found.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Construct the trusted platform assemblies (TPA) list
    // This is the list of assemblies that .NET Core can load as
    // trusted system assemblies.
    // For this host (as with most), assemblies next to CoreCLR will
    // be included in the TPA list
    std::string tpaList;
    BuildTpaList(runtimePath, ".dll", tpaList);

    // Define CoreCLR properties
    // Other properties related to assembly loading are common here,
    // but for this simple sample, TRUSTED_PLATFORM_ASSEMBLIES is all
    // that is needed. Check hosting documentation for other common properties.
    const char *propertyKeys[] = {
        "TRUSTED_PLATFORM_ASSEMBLIES" // Trusted assemblies
    };

    const char *propertyValues[] = {
        tpaList.c_str()};

    // This function both starts the .NET Core runtime and creates
    // the default (and only) AppDomain
    int hr = coreClrInfo->initializeCoreClr(
        runtimePath,                           // App base path
        "DOTNODE_APP_DOMAIN",                  // AppDomain friendly name
        sizeof(propertyKeys) / sizeof(char *), // Property count
        propertyKeys,                          // Property names
        propertyValues,                        // Property values
        &coreClrInfo->hostHandle,              // Host handle
        &coreClrInfo->domainId);               // AppDomain ID

    if (hr < 0)
    {
        delete coreClrInfo;
        Napi::Error::New(env, "coreclr_initialize failed.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    return env.Undefined();
}

Napi::Value callFunction(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 3 || info.Length() > 4)
    {
        Napi::TypeError::New(env, "Invalid argument count.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::String jsAssemblyName = info[0].ToString();
    Napi::String jsClassName = info[1].ToString();
    Napi::String jsMethodName = info[2].ToString();

    if (!info[0].IsString() || !info[1].IsString() || !info[2].IsString())
    {
        Napi::TypeError::New(env, "Invalid argument type(s).").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // <Snippet5>
    dotnode_function managedDelegate;

    // The assembly name passed in the third parameter is a managed assembly name
    // as described at https://docs.microsoft.com/dotnet/framework/app-domains/assembly-names
    int hr = coreClrInfo->createManagedDelegate(
        coreClrInfo->hostHandle,
        coreClrInfo->domainId,
        jsAssemblyName.Utf8Value().c_str(),
        jsClassName.Utf8Value().c_str(),
        jsMethodName.Utf8Value().c_str(),
        (void **)&managedDelegate);
    // </Snippet5>

    if (hr < 0)
    {
        Napi::Error::New(env, "coreclr_create_delegate failed.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() == 4 && !info[3].IsNull() && !info[3].IsUndefined())
    {
        const char *result = managedDelegate(info[3].ToString().Utf8Value().c_str());
        if (result == nullptr)
        {
            return env.Undefined();
        }

        Napi::String jsResult = Napi::String::New(env, result);
        free((void *)result);
        return jsResult;
    }
    else
    {
        const char *result = managedDelegate(nullptr);
        if (result == nullptr)
        {
            return env.Undefined();
        }

        return Napi::String::New(env, result);
    }
}

Napi::Value shutdownDotnode(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    int hr = coreClrInfo->shutdownCoreClr(coreClrInfo->hostHandle, coreClrInfo->domainId);

    if (hr < 0)
    {
        Napi::Error::New(env, "coreclr_shutdown failed.").ThrowAsJavaScriptException();
    }

    return env.Undefined();
}

#if WINDOWS
// Win32 directory search for .dll files
// <Snippet7>
void BuildTpaList(const char *directory, const char *extension, std::string &tpaList)
{
    // This will add all files with a .dll extension to the TPA list.
    // This will include unmanaged assemblies (coreclr.dll, for example) that don't
    // belong on the TPA list. In a real host, only managed assemblies that the host
    // expects to load should be included. Having extra unmanaged assemblies doesn't
    // cause anything to fail, though, so this function just enumerates all dll's in
    // order to keep this sample concise.
    std::string searchPath(directory);
    searchPath.append(FS_SEPARATOR);
    searchPath.append("*");
    searchPath.append(extension);

    WIN32_FIND_DATAA findData;
    HANDLE fileHandle = FindFirstFileA(searchPath.c_str(), &findData);

    if (fileHandle != INVALID_HANDLE_VALUE)
    {
        do
        {
            // Append the assembly to the list
            tpaList.append(directory);
            tpaList.append(FS_SEPARATOR);
            tpaList.append(findData.cFileName);
            tpaList.append(PATH_DELIMITER);

            // Note that the CLR does not guarantee which assembly will be loaded if an assembly
            // is in the TPA list multiple times (perhaps from different paths or perhaps with different NI/NI.dll
            // extensions. Therefore, a real host should probably add items to the list in priority order and only
            // add a file if it's not already present on the list.
            //
            // For this simple sample, though, and because we're only loading TPA assemblies from a single path,
            // and have no native images, we can ignore that complication.
        } while (FindNextFileA(fileHandle, &findData));
        FindClose(fileHandle);
    }
}
// </Snippet7>
#elif LINUX
// POSIX directory search for .dll files
void BuildTpaList(const char *directory, const char *extension, std::string &tpaList)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    int extLength = strlen(extension);

    while ((entry = readdir(dir)) != NULL)
    {
        // This simple sample doesn't check for symlinks
        std::string filename(entry->d_name);

        // Check if the file has the right extension
        int extPos = filename.length() - extLength;
        if (extPos <= 0 || filename.compare(extPos, extLength, extension) != 0)
        {
            continue;
        }

        // Append the assembly to the list
        tpaList.append(directory);
        tpaList.append(FS_SEPARATOR);
        tpaList.append(filename);
        tpaList.append(PATH_DELIMITER);

        // TODO: Check for multiple *.dll with same name.
    }
}
#endif

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set(Napi::String::New(env, "initialize"), Napi::Function::New(env, initializeDotnode));
    exports.Set(Napi::String::New(env, "invokeMethod"), Napi::Function::New(env, callFunction));
    exports.Set(Napi::String::New(env, "shutdown"), Napi::Function::New(env, shutdownDotnode));

    return exports;
}

NODE_API_MODULE(dotnode, Init)
