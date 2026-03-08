/*
 * rasdial2 - drop-in replacement for rasdial
 *
 * Copyright (C) 2026 megakraken
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// gcc rasdial2.c -o rasdial2.exe -lrasapi32 -municode -O2 -static -s
// cl /W3 /O2 /MT rasdial2.c
#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <ras.h>
#include <raserror.h>

#ifdef _MSC_VER
#pragma comment(lib, "rasapi32.lib")
#pragma comment(lib, "shell32.lib")
#endif

typedef enum RASDIAL2_MODE {
    MODE_LIST = 0,
    MODE_CONNECT,
    MODE_DISCONNECT,
    MODE_HELP
} RASDIAL2_MODE;

typedef struct RASDIAL2_ARGS {
    RASDIAL2_MODE mode;
    const wchar_t *entryName;
    const wchar_t *userName;
    const wchar_t *password;
    const wchar_t *domain;
    const wchar_t *phone;
    const wchar_t *callback;
    const wchar_t *phonebook;
    BOOL prefixSuffix;
} RASDIAL2_ARGS;

static int parse_args(int argc, wchar_t **argv, RASDIAL2_ARGS *out);
static void print_usage();
static int do_list();
static int do_disconnect(const RASDIAL2_ARGS *args);
static int do_connect(const RASDIAL2_ARGS *args);
static int read_password(wchar_t *buf, DWORD bufSize);

int wmain(int argc, wchar_t **argv) {
    RASDIAL2_ARGS args;
    int ret, c;
    wchar_t **v;
    /* Use raw command line to avoid CRT wildcard expansion
        (e.g. '*' as password). */
    v = CommandLineToArgvW(GetCommandLineW(), &c);
    if (v == NULL)
        return ERROR_OUTOFMEMORY;
    ret = parse_args(c, v, &args);
    LocalFree(v);
    if (ret != 0) {
        print_usage();
        return ret;
    }
    switch (args.mode) {
        case MODE_LIST:
            ret = do_list();
            break;
        case MODE_DISCONNECT:
            ret = do_disconnect(&args);
            break;
        case MODE_CONNECT:
            ret = do_connect(&args);
            break;
        case MODE_HELP:
        default:
            print_usage();
            return 0;
    }
    if (ret == 0) {
        wprintf(L"Command completed successfully.\n");
    } else {
        WCHAR msg[256];
        if (!RasGetErrorStringW(ret, msg, sizeof(msg) / sizeof(msg[0])))
            fwprintf(stderr, L"\nRemote Access error %lu - %ls\n", ret, msg);
        else
            fwprintf(stderr, L"\nRemote Access error %lu\n", ret);
    }
    return ret;
}

static int do_list() {
    RASCONNW conns[32] = {0};
    DWORD size = sizeof(conns);
    DWORD count;
    DWORD ret;
    DWORD i;
    for (i = 0; i < 32; i++)
        conns[i].dwSize = sizeof(RASCONNW);
    ret = RasEnumConnectionsW(conns, &size, &count);
    if (ret != ERROR_SUCCESS)
        return ret;
    if (count == 0) {
        wprintf(L"No connections\n");
    } else {
        wprintf(L"Connected to\n");
        for (i = 0; i < count; i++) {
            const RASCONNW *c = &conns[i];
            wprintf(L"%ls\n", c->szEntryName);
        }
    }
    return 0;
}

static int do_disconnect(const RASDIAL2_ARGS *args) {
    RASCONNW conns[32] = {0};
    DWORD size = sizeof(conns);
    DWORD count;
    DWORD i;
    DWORD ret;
    DWORD error = 0;
    BOOL found = FALSE;
    const wchar_t *target = args->entryName; /* may be NULL */

    for (i = 0; i < 32; i++)
        conns[i].dwSize = sizeof(RASCONNW);
    ret = RasEnumConnectionsW(conns, &size, &count);
    if (ret != ERROR_SUCCESS)
        return ret;
    for (i = 0; i < count; i++) {
        const RASCONNW *c = &conns[i];
        if (target != NULL) {
            /* only disconnect matching entry name */
            if (_wcsicmp(c->szEntryName, target) != 0)
                continue;
        }
        found = TRUE;
        ret = RasHangUpW(c->hrasconn);
        /* return first error, if any */
        if (error == 0)
            error = ret;
    }
    if (target != NULL && !found)
        return ERROR_NOT_FOUND;
    return error;
}

static int do_connect(const RASDIAL2_ARGS *args) {
    RASDIALPARAMSW p = {0};
    HRASCONN hconn = NULL;
    DWORD ret;
    BOOL hasPassword = FALSE;
    const wchar_t *phonebook = args->phonebook; /* may be NULL */

    p.dwSize = sizeof(p);
    if (args->entryName == NULL)
        return ERROR_INVALID_PARAMETER;
    wcsncpy(p.szEntryName, args->entryName, RAS_MaxEntryName - 1);
    if (args->phone != NULL)
        wcsncpy(p.szPhoneNumber, args->phone, RAS_MaxPhoneNumber - 1);
    if (args->callback != NULL)
        wcsncpy(p.szCallbackNumber, args->callback, RAS_MaxCallbackNumber - 1);
    if (args->domain != NULL)
        wcsncpy(p.szDomain, args->domain, DNLEN);

    if (args->userName != NULL) {
        wcsncpy(p.szUserName, args->userName, UNLEN);
        /* password may be NULL, "*" or real password */
        if (args->password != NULL) {
            if (wcscmp(args->password, L"*") == 0) {
                if((ret = read_password(p.szPassword, PWLEN)) != 0)
                    return ret;
            } else {
                wcsncpy(p.szPassword, args->password, PWLEN);
            }
        } else {
            /* no password argument -> empty password */
            p.szPassword[0] = L'\0';
        }
    } else {
        ret = RasGetEntryDialParamsW(phonebook, &p, &hasPassword);
        if (ret != ERROR_SUCCESS)
            return ret;
        if (!hasPassword)
            return ERROR_ACCESS_DENIED;
    }
    wprintf(L"Connecting to %ls...\n", p.szEntryName);
    wprintf(L"Verifying username and password...\n");
    return RasDialW(NULL, phonebook, &p, 0, NULL, &hconn);
}

static void print_usage() {
    fwprintf(stderr,
        L"USAGE:\n"
        L"        rasdial2 entryname [username [password|*]] [/DOMAIN:domain]\n"
        L"                [/PHONE:phonenumber] [/CALLBACK:callbacknumber]\n"
        L"                [/PHONEBOOK:phonebookfile]\n\n"
        L"        rasdial2 [entryname] /DISCONNECT\n\n"
        L"        rasdial2\n\n");
}

static int parse_args(int argc, wchar_t **argv, RASDIAL2_ARGS *out) {
    int n = 2;
    ZeroMemory(out, sizeof(*out));
    if (argc == 1) {
        out->mode = MODE_LIST;
        return 0;
    }
    if (_wcsicmp(argv[1], L"/?") == 0) {
        out->mode = MODE_HELP;
        return 0;
    }
    if (_wcsicmp(argv[1], L"/DISCONNECT") == 0) {
        out->mode = MODE_DISCONNECT;
        out->entryName = NULL;
        return 0;
    }
    if (argv[1][0] == L'/')
        return ERROR_INVALID_PARAMETER;
    if (argc >= 3 && _wcsicmp(argv[2], L"/DISCONNECT") == 0) {
        out->mode = MODE_DISCONNECT;
        out->entryName = argv[1];
        return 0;
    }
    out->mode = MODE_CONNECT;
    out->entryName = argv[1];
    /* optional username */
    if (n < argc && argv[n][0] != L'/')
        out->userName = argv[n++];
    /* optional password */
    if (n < argc && argv[n][0] != L'/') {
        out->password = argv[n++];
    }
    /* remaining args are all switches */
    for (; n < argc; ++n) {
        const wchar_t *a = argv[n];
        const wchar_t *p;
        if (a[0] != L'/')
            return ERROR_INVALID_PARAMETER;
        p = a + 1;
        if (_wcsnicmp(p, L"DOMAIN:", 7) == 0) {
            out->domain = p + 7;
        } else if (_wcsnicmp(p, L"PHONE:", 6) == 0) {
            out->phone = p + 6;
        } else if (_wcsnicmp(p, L"CALLBACK:", 9) == 0) {
            out->callback = p + 9;
        } else if (_wcsnicmp(p, L"PHONEBOOK:", 10) == 0) {
            out->phonebook = p + 10;
        } else if (_wcsicmp(p, L"PREFIXSUFFIX") == 0) {
            out->prefixSuffix = TRUE;
        } else {
            return ERROR_INVALID_PARAMETER;
        }
    }
    return 0;
}

static int read_password(wchar_t *buf, DWORD bufSize) {
    HANDLE hIn;
    DWORD oldMode, mode;
    DWORD read;
    if (bufSize == 0)
        return ERROR_INVALID_PARAMETER;
    if((hIn = GetStdHandle(STD_INPUT_HANDLE)) == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!GetConsoleMode(hIn, &oldMode))
        return GetLastError();
    mode = oldMode & ~ENABLE_ECHO_INPUT;
    if (!SetConsoleMode(hIn, mode))
        return GetLastError();
    wprintf(L"Password: ");
    fflush(stdout);
    if (!ReadConsoleW(hIn, buf, bufSize - 1, &read, NULL)) {
        SetConsoleMode(hIn, oldMode);
        return GetLastError();
    }
    SetConsoleMode(hIn, oldMode);
    /* strip trailing CR/LF */
    while (read > 0) {
        if(buf[read - 1] != L'\n' && buf[read - 1] != L'\r')
            break;
        read--;
    }
    buf[read] = L'\0';
    wprintf(L"\n");
    return 0;
}
