#define COBJMACROS
#include <initguid.h>
#include <stdbool.h>
#include <shlobj.h>
#include <taskschd.h>
#include <stdio.h>
#include <sddl.h>
#include "Utils/Error.h"
#include "Utils/File.h"

static const wchar_t* xml = L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
                            L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
                            L"<Triggers>\n"
                            L"    <LogonTrigger>\n"
                            L"    <UserId>%ls</UserId>\n"
                            L"        <Enabled>true</Enabled>\n"
                            L"    </LogonTrigger>\n"
                            L"    <SessionStateChangeTrigger>\n"
                            L"    <Enabled>true</Enabled>\n"
                            L"    <StateChange>SessionUnlock</StateChange>\n"
                            L"    <UserId>%ls</UserId>"
                            L"    </SessionStateChangeTrigger>\n"
                            L"</Triggers>\n"
                            L"<Principals>\n"
                            L"    <Principal id=\"Author\">\n"
                            L"    <UserId>%ls</UserId>\n"
                            L"        <LogonType>InteractiveToken</LogonType>\n"
                            L"        <RunLevel>%ls</RunLevel>\n"
                            L"    </Principal>\n"
                            L"</Principals>\n"
                            L"<Settings>\n"
                            L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
                            L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
                            L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
                            L"    <Priority>7</Priority>\n"
                            L"   </Settings>\n"
                            L"<Actions>\n"
                            L"    <Exec>\n"
                            L"    <Command>%ls</Command>\n"
                            L"    <WorkingDirectory>%ls</WorkingDirectory>\n"
                            L"     </Exec>\n"
                            L"   </Actions>\n"
                            L" </Task>";

static wchar_t xml_fmt[2048] = { };

static void cs_to_wcs(wchar_t* dst, const char* src)
{
    while (*src != '\0') {
        *dst = (wchar_t)*src;
        dst++;
        src++;
    }
    *dst = L'\0';
}

static const char remove_msg_default[] = "Remove startup task failed.";
static const char remove_msg_no_task[] = "No startup task to remove.";
static const char remove_msg_success[] = "Startup task removed.";
static const char remove_msg_insufficient_right[] = "Could not remove startup task. A task is already registered as administrator. Re-run this as administrator to remove it.";

static const char create_msg_default[] = "Create startup task failed.";
static const char create_msg_success_update[] = "Startup task succesfully updated.";
static const char create_msg_success_new[] = "Startup task succesfully created.";
static const char create_msg_insufficient_right[] = "Could not update startup task. A task is already registered as administrator. Re-run this as administrator to update it.";

static bool task_is_elevated(IRegisteredTask* task, bool* out_elevated)
{
    ITaskDefinition* definition = NULL;
    IPrincipal* principal = NULL;
    *out_elevated = false;
    HRESULT hr = -1;
    ASSERT(FAILED(hr)); // Just makes sure -1 means failure
    do {
        hr = IRegisteredTask_get_Definition(task, &definition);
        if (!SUCCEEDED(hr))
            break;
        hr = ITaskDefinition_get_Principal(definition, &principal);
        if (!SUCCEEDED(hr))
            break;
        TASK_RUNLEVEL_TYPE run_level;
        hr = IPrincipal_get_RunLevel(principal, &run_level);
        if (!SUCCEEDED(hr))
            break;
        *out_elevated = run_level == TASK_RUNLEVEL_HIGHEST;
    } while (false);
    if (principal)
        IPrincipal_Release(principal);
    if (definition)
        ITaskDefinition_Release(definition);
    return hr;
}

static bool create_task(const char** out_msg)
{
    wchar_t exe[MAX_PATH] = { };
    wchar_t dir[MAX_PATH] = { };
    {
        char x[MAX_PATH] = { };
        char y[MAX_PATH] = { };
        aas_path(x);
        cs_to_wcs(exe, x);
        ParentDir(x, y);
        cs_to_wcs(dir, y);
    }
    wchar_t user[256] = { };
    bool elevated = false;
    {
        HANDLE tok;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        TOKEN_ELEVATION elTok;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        GetTokenInformation(tok, TokenElevation, &elTok, sizeof(elTok), &cbSize);
        elevated = elTok.TokenIsElevated;
        // Max align heap alloc:
        unsigned long long buf[(sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE) / sizeof(unsigned long long)] = { };
        TOKEN_USER* user_tok = (TOKEN_USER*)buf;
        cbSize = sizeof(buf);
        GetTokenInformation(tok, TokenUser, user_tok, sizeof(buf), &cbSize);
        wchar_t* sid = NULL;
        // This does alloc, mus call free afterwards
        ConvertSidToStringSidW(user_tok->User.Sid, &sid);
        wcscpy(user, sid);
        LocalFree(sid);
        CloseHandle(tok);
    }

    (void)swprintf_s(
        xml_fmt,
        sizeof(xml_fmt),
        xml,
        user,
        user,
        user,
        elevated ? L"HighestAvailable" : L"LeastPrivilege",
        exe,
        dir);
#if 0
    wprintf(xml_fmt);
#endif

    ITaskService* service = NULL;
    ITaskFolder* root_folder = NULL;
    IRegisteredTask* task = NULL;
    IRegisteredTask* prev_task = NULL;
    BSTR xml_bstr = NULL;
    *out_msg = create_msg_default;
    HRESULT hr = -1;
    ASSERT(FAILED(hr)); // Just makes sure -1 means failure
    do {
        hr = CoCreateInstance(
            &CLSID_TaskScheduler,
            NULL,
            CLSCTX_INPROC_SERVER,
            &IID_ITaskService,
            (void**)&service);
        if (FAILED(hr))
            break;
        hr = ITaskService_Connect(service,
            (VARIANT) { },
            (VARIANT) { },
            (VARIANT) { },
            (VARIANT) { });
        if (FAILED(hr))
            break;
        hr = ITaskService_GetFolder(service, L"\\", &root_folder);
        if (FAILED(hr))
            break;

        bool already_exists = false;
        ITaskFolder_GetTask(root_folder, L"AltAppSwitcher", &prev_task);
        if (prev_task) {
            already_exists = true;
        }

        if (already_exists) {
            bool prev_task_is_elevated = false;
            hr = task_is_elevated(prev_task, &prev_task_is_elevated);
            if (FAILED(hr))
                break;
            if (prev_task_is_elevated && !elevated) {
                *out_msg = create_msg_insufficient_right;
                break;
            }
        }

        xml_bstr = SysAllocString(xml_fmt);
        hr = ITaskFolder_RegisterTask(root_folder,
            L"AltAppSwitcher",
            xml_bstr,
            TASK_CREATE_OR_UPDATE, // equivalent to /F
            (VARIANT) { },
            (VARIANT) { },
            TASK_LOGON_INTERACTIVE_TOKEN,
            (VARIANT) { },
            &task);
        if (SUCCEEDED(hr)) {
            if (already_exists)
                *out_msg = create_msg_success_update;
            else
                *out_msg = create_msg_success_new;
        }
    } while (false);

    // Cleanup
    {
        if (xml_bstr)
            SysFreeString(xml_bstr);
        if (task)
            IRegisteredTask_Release(task);
        if (prev_task)
            IRegisteredTask_Release(prev_task);
        if (root_folder)
            ITaskFolder_Release(root_folder);
        if (service)
            ITaskService_Release(service);
    }

    return SUCCEEDED(hr);
}

static bool remove_task(const char** out_msg)
{
    ITaskService* service = NULL;
    ITaskFolder* root_folder = NULL;
    IRegisteredTask* prev_task = NULL;

    bool elevated = false;
    {
        HANDLE tok;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        TOKEN_ELEVATION elTok;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        GetTokenInformation(tok, TokenElevation, &elTok, sizeof(elTok), &cbSize);
        elevated = elTok.TokenIsElevated;
    }

    HRESULT hr = -1;
    *out_msg = remove_msg_default;
    ASSERT(FAILED(hr)); // Just makes sure -1 means failure
    do {
        hr = CoCreateInstance(
            &CLSID_TaskScheduler,
            NULL,
            CLSCTX_INPROC_SERVER,
            &IID_ITaskService,
            (void**)&service);
        if (FAILED(hr))
            break;
        hr = ITaskService_Connect(
            service,
            (VARIANT) { },
            (VARIANT) { },
            (VARIANT) { },
            (VARIANT) { });
        if (FAILED(hr))
            break;
        hr = ITaskService_GetFolder(service, L"\\", &root_folder);
        if (FAILED(hr))
            break;

        hr = ITaskFolder_GetTask(root_folder, L"AltAppSwitcher", &prev_task);
        if (!prev_task) {
            *out_msg = remove_msg_no_task;
            break;
        }

        bool prev_task_is_elevated = false;
        hr = task_is_elevated(prev_task, &prev_task_is_elevated);
        if (FAILED(hr))
            break;

        if (prev_task_is_elevated && !elevated) {
            *out_msg = remove_msg_insufficient_right;
            break;
        }

        hr = ITaskFolder_DeleteTask(
            root_folder,
            L"AltAppSwitcher",
            0);
        if (SUCCEEDED(hr)) {
            *out_msg = remove_msg_success;
        }
    } while (false);

    // Cleanup
    {
        if (prev_task)
            IRegisteredTask_Release(prev_task);
        if (root_folder)
            ITaskFolder_Release(root_folder);
        if (service)
            ITaskService_Release(service);
    }

    return SUCCEEDED(hr);
}

int main(int argc, char* argv[])
{
    bool remove = false;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--remove"))
            remove = true;
    }
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ASSERT(!FAILED(hr));
    const char* msg = NULL;
    if (remove)
        remove_task(&msg);
    else
        create_task(&msg);
    MessageBox(0, msg, "AltAppSwitcher", MB_OK | MB_SETFOREGROUND);
    CoUninitialize();
    return 0;
}
