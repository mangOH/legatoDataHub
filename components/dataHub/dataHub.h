//--------------------------------------------------------------------------------------------------
/**
 * Data type and interface definitions shared between modules in the Data Hub component.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef DATA_HUB_H_INCLUDE_GUARD
#define DATA_HUB_H_INCLUDE_GUARD


/// Maximum number of bytes (including null terminator) in a Resource Tree Entry's name.
#define HUB_MAX_ENTRY_NAME_BYTES (LE_LIMIT_APP_NAME_LEN + 1)

/// Maximum number of bytes (including null terminator) in a Resource's path within its Namespace.
#define HUB_MAX_RESOURCE_PATH_BYTES (IO_MAX_RESOURCE_PATH_LEN + 1)

/// Maximum number of bytes (including null terminator) in a units string.
#define HUB_MAX_UNITS_BYTES (IO_MAX_UNITS_NAME_LEN + 1)

/// Maximum number of bytes (including null terminator) in the value of a string type data sample.
#define HUB_MAX_STRING_BYTES (IO_MAX_STRING_VALUE_LEN + 1)


//--------------------------------------------------------------------------------------------------
/**
 * Reference to a handler function that has been registered with an Input or Output resource.
 */
//--------------------------------------------------------------------------------------------------
typedef struct hub_Handler* hub_HandlerRef_t;


#include "interfaces.h"
#include "dataSample.h"
#include "resTree.h"

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
);


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
);

//--------------------------------------------------------------------------------------------------
/**
 * Set the client app's namespace.
 *
 * @return:
 *  - LE_OK if setting client's namespace was successful.
 *  - LE_DUPLICATE if namespace has already been set.
 */
//--------------------------------------------------------------------------------------------------
le_result_t hub_SetClientNamespace
(
    le_msg_SessionRef_t sessionRef,  ///< [IN] IPC session reference.
    const char* appNamespace         ///< [IN] namespace
);

//--------------------------------------------------------------------------------------------------
/**
 * Get the client app's namespace.
 *
 * @return the reference to the namespace resource tree entry or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
resTree_EntryRef_t hub_GetClientNamespace
(
    le_msg_SessionRef_t sessionRef  ///< [IN] IPC session reference.
);


#endif // DATA_HUB_H_INCLUDE_GUARD
