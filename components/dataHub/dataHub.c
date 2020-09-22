//--------------------------------------------------------------------------------------------------
/**
 * @file dataHub.c
 *
 * Data Hub component initializer and utilities shared by other modules.
 *
 * The Resource Tree structure and Namespaces are implemented by the resTree module.
 *
 * The Resource base class and Placeholder resource are implemented by the resource module
 * (prefix = res_).
 *
 * Inputs and Outputs are implemented by the ioRes module.
 *
 * Observations are implemented by the obs module.
 *
 * Data Samples are implemented by the dataSample module.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "dataHub.h"
#include "nan.h"
#include "dataSample.h"
#include "handler.h"
#include "resource.h"
#include "resTree.h"
#include "ioPoint.h"
#include "obs.h"
#include "ioService.h"
#include "adminService.h"
#include "snapshot.h"


//--------------------------------------------------------------------------------------------------
/**
 * Component initializer.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    dataSample_Init();
    handler_Init();
    res_Init();
    ioPoint_Init();
    obs_Init();
    resTree_Init();
    ioService_Init();
    adminService_Init();
    snapshot_Init();

    LE_INFO("Data Hub started.");
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string name for a given data type (e.g., "numeric").
 *
 * @return Pointer to the name.
 */
//--------------------------------------------------------------------------------------------------
const char* hub_GetDataTypeName
(
    io_DataType_t type
)
//--------------------------------------------------------------------------------------------------
{
    switch (type)
    {
        case IO_DATA_TYPE_TRIGGER:
            return "trigger";

        case IO_DATA_TYPE_BOOLEAN:
            return "Boolean";

        case IO_DATA_TYPE_NUMERIC:
            return "numeric";

        case IO_DATA_TYPE_STRING:
            return "string";

        case IO_DATA_TYPE_JSON:
            return "JSON";
    }

    LE_FATAL("Unknown data type %d.", type);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string name for a given resource tree entry type (e.g., "observation").
 *
 * @return Pointer to the name.
 */
//--------------------------------------------------------------------------------------------------
const char* hub_GetEntryTypeName
(
    admin_EntryType_t type
)
//--------------------------------------------------------------------------------------------------
{
    switch (type)
    {
        case ADMIN_ENTRY_TYPE_NONE:
            return "** none **";

        case ADMIN_ENTRY_TYPE_NAMESPACE:
            return "namespace";

        case ADMIN_ENTRY_TYPE_PLACEHOLDER:
            return "placeholder";

        case ADMIN_ENTRY_TYPE_INPUT:
            return "input";

        case ADMIN_ENTRY_TYPE_OUTPUT:
            return "output";

        case ADMIN_ENTRY_TYPE_OBSERVATION:
            return "observation";
    }

    LE_FATAL("Unknown entry type %d.", type);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the client app's namespace.
 *
 * @return the reference to the namespace resource tree entry or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
resTree_EntryRef_t hub_GetClientNamespace
(
    le_msg_SessionRef_t sessionRef  ///< IPC session reference.
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t nsRef;

#if LE_CONFIG_LINUX
    // See if we already looked up the app name, etc. and saved it as the IPC session context ptr.
    nsRef = le_msg_GetSessionContextPtr(sessionRef);
    if (nsRef != NULL)
    {
        return nsRef;
    }
#endif /* end LE_CONFIG_LINUX */

    // Get the client app name.
    pid_t clientPid;
    le_result_t result = le_msg_GetClientProcessId(sessionRef, &clientPid);
    if (result != LE_OK)
    {
        LE_KILL_CLIENT("Unable to retrieve client process ID (%s)", LE_RESULT_TXT(result));
        return NULL;
    }
    char appName[LE_LIMIT_APP_NAME_LEN + 1];
    result = le_appInfo_GetName(clientPid, appName, sizeof(appName));
    if (result != LE_OK)
    {
        LE_KILL_CLIENT("Unable to retrieve client app name (%s)", LE_RESULT_TXT(result));
        return NULL;
    }

#if LE_CONFIG_TARGET_HL78MCU && WITH_OCTAVE
    //
    // JV: hack away rpcProxy and cmdTools app names until LE-15082
    //

    //LE_INFO("is it rpcProxy?: '%s'",appName);
    if (strncmp(appName, "rpcProxy", LE_LIMIT_APP_NAME_LEN) == 0)
    {
        strncpy(appName, "cloudInterface", LE_LIMIT_APP_NAME_LEN);
        LE_DEBUG("changed rpcProxy to '%s'",appName);
    }
    else if (strncmp(appName, "cmdTools", LE_LIMIT_APP_NAME_LEN) == 0)
    {
        strncpy(appName, "orp", LE_LIMIT_APP_NAME_LEN);
        LE_DEBUG("changed cmdTools to '%s'",appName);
    }
#endif /* end LE_CONFIG_TARGET_HL78MCU && WITH_OCTAVE */

    // Get the "/app" namespace first.
    nsRef = resTree_GetEntry(resTree_GetRoot(), "app");

    // Now get the app's namespace under the /app namespace.
    nsRef = resTree_GetEntry(nsRef, appName);

#if LE_CONFIG_LINUX
    // Store the namespace entry reference as the IPC session Context Ptr to speed things up
    // next time.
    le_msg_SetSessionContextPtr(sessionRef, nsRef);
#endif /* end LE_CONFIG_LINUX */

    return nsRef;
}

