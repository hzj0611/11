/*
 * Win32 mutexes
 *
 * Copyright 1998 Alexandre Julliard
 */

#include <assert.h>
#include "windows.h"
#include "winerror.h"
#include "k32obj.h"
#include "process.h"
#include "thread.h"
#include "heap.h"
#include "server/request.h"
#include "server.h"

typedef struct _MUTEX
{
    K32OBJ         header;
} MUTEX;

static void MUTEX_Destroy( K32OBJ *obj );

const K32OBJ_OPS MUTEX_Ops =
{
    MUTEX_Destroy      /* destroy */
};


/***********************************************************************
 *           CreateMutex32A   (KERNEL32.166)
 */
HANDLE32 WINAPI CreateMutex32A( SECURITY_ATTRIBUTES *sa, BOOL32 owner,
                                LPCSTR name )
{
    struct create_mutex_request req;
    struct create_mutex_reply reply;
    int len = name ? strlen(name) + 1 : 0;
    HANDLE32 handle;
    MUTEX *mutex;

    req.owned   = owner;
    req.inherit = (sa && (sa->nLength>=sizeof(*sa)) && sa->bInheritHandle);

    CLIENT_SendRequest( REQ_CREATE_MUTEX, -1, 2, &req, sizeof(req), name, len );
    CLIENT_WaitReply( &len, NULL, 1, &reply, sizeof(reply) );
    CHECK_LEN( len, sizeof(reply) );
    if (reply.handle == -1) return 0;

    SYSTEM_LOCK();
    mutex = (MUTEX *)K32OBJ_Create( K32OBJ_MUTEX, sizeof(*mutex),
                                    name, reply.handle, MUTEX_ALL_ACCESS,
                                    sa, &handle );
    if (mutex) K32OBJ_DecCount( &mutex->header );
    if (handle == INVALID_HANDLE_VALUE32) handle = 0;
    SYSTEM_UNLOCK();
    return handle;
}


/***********************************************************************
 *           CreateMutex32W   (KERNEL32.167)
 */
HANDLE32 WINAPI CreateMutex32W( SECURITY_ATTRIBUTES *sa, BOOL32 owner,
                                LPCWSTR name )
{
    LPSTR nameA = HEAP_strdupWtoA( GetProcessHeap(), 0, name );
    HANDLE32 ret = CreateMutex32A( sa, owner, nameA );
    if (nameA) HeapFree( GetProcessHeap(), 0, nameA );
    return ret;
}


/***********************************************************************
 *           OpenMutex32A   (KERNEL32.541)
 */
HANDLE32 WINAPI OpenMutex32A( DWORD access, BOOL32 inherit, LPCSTR name )
{
    HANDLE32 handle = 0;
    K32OBJ *obj;
    struct open_named_obj_request req;
    struct open_named_obj_reply reply;
    int len = name ? strlen(name) + 1 : 0;

    req.type    = OPEN_MUTEX;
    req.access  = access;
    req.inherit = inherit;
    CLIENT_SendRequest( REQ_OPEN_NAMED_OBJ, -1, 2, &req, sizeof(req), name, len );
    CLIENT_WaitReply( &len, NULL, 1, &reply, sizeof(reply) );
    CHECK_LEN( len, sizeof(reply) );
    if (reply.handle != -1)
    {
        SYSTEM_LOCK();
        if ((obj = K32OBJ_FindNameType( name, K32OBJ_MUTEX )) != NULL)
        {
            handle = HANDLE_Alloc( PROCESS_Current(), obj, access, inherit, reply.handle );
            K32OBJ_DecCount( obj );
            if (handle == INVALID_HANDLE_VALUE32)
                handle = 0; /* must return 0 on failure, not -1 */
        }
        else CLIENT_CloseHandle( reply.handle );
        SYSTEM_UNLOCK();
    }
    return handle;
}


/***********************************************************************
 *           OpenMutex32W   (KERNEL32.542)
 */
HANDLE32 WINAPI OpenMutex32W( DWORD access, BOOL32 inherit, LPCWSTR name )
{
    LPSTR nameA = HEAP_strdupWtoA( GetProcessHeap(), 0, name );
    HANDLE32 ret = OpenMutex32A( access, inherit, nameA );
    if (nameA) HeapFree( GetProcessHeap(), 0, nameA );
    return ret;
}


/***********************************************************************
 *           ReleaseMutex   (KERNEL32.582)
 */
BOOL32 WINAPI ReleaseMutex( HANDLE32 handle )
{
    struct release_mutex_request req;

    req.handle = HANDLE_GetServerHandle( PROCESS_Current(), handle,
                                         K32OBJ_MUTEX, MUTEX_MODIFY_STATE );
    if (req.handle == -1) return FALSE;
    CLIENT_SendRequest( REQ_RELEASE_MUTEX, -1, 1, &req, sizeof(req) );
    return !CLIENT_WaitReply( NULL, NULL, 0 );
}


/***********************************************************************
 *           MUTEX_Destroy
 */
static void MUTEX_Destroy( K32OBJ *obj )
{
    MUTEX *mutex = (MUTEX *)obj;
    assert( obj->type == K32OBJ_MUTEX );
    obj->type = K32OBJ_UNKNOWN;
    HeapFree( SystemHeap, 0, mutex );
}
