#define COBJMACROS
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
bool create_task_from_xml()
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

    (void)swprintf(
        xml_fmt,
        sizeof(xml_fmt),
        xml,
        user,
        user,
        user,
        elevated ? L"HighestAvailable" : L"LeastPrivilege",
        exe,
        dir);
    wprintf(xml_fmt);

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return false;

    ITaskService* service = NULL;
    ITaskFolder* root_folder = NULL;
    IRegisteredTask* task = NULL;
    BSTR xml_bstr = NULL;

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
        if (FAILED(hr)) {
            printf("error is %x", (int)hr);
            ASSERT(false);
        }
    } while (false);

    // Cleanup
    {
        if (xml_bstr)
            SysFreeString(xml_bstr);
        if (task)
            IRegisteredTask_Release(task);
        if (root_folder)
            ITaskFolder_Release(root_folder);
        if (service)
            ITaskService_Release(service);
    }

    CoUninitialize();

    return SUCCEEDED(hr);
}

int main(int argc, char* argv[])
{
    create_task_from_xml();
    (void)argc;
    (void)argv;
    return 0;
}
