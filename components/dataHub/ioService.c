//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the I/O API service interfaces served up by the Data Hub.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "dataHub.h"
#include "handler.h"
#include "json.h"


//--------------------------------------------------------------------------------------------------
/**
 * Used to store a registered Update Start/End Handler on the UpdateStartEndHandlerList.
 *
 * These are allocated from the UpdateStartEndHandlerPool.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    le_dls_Link_t link; ///< Used to link into the UpdateStartEndHandlerList
    io_UpdateStartEndHandlerFunc_t callback;
    void* contextPtr;
}
UpdateStartEndHandler_t;


//--------------------------------------------------------------------------------------------------
/**
 * List of Update Start/End Handlers.
 */
//--------------------------------------------------------------------------------------------------
static le_dls_List_t UpdateStartEndHandlerList = LE_DLS_LIST_INIT;


//--------------------------------------------------------------------------------------------------
/**
 * Pool of UpdateStartEndHandler objects.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t UpdateStartEndHandlerPool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Get the resource at a given path within the app's namespace.
 *
 * @return Reference to the entry, or NULL if not found.
 */
//--------------------------------------------------------------------------------------------------
static resTree_EntryRef_t FindResource
(
    const char* path  ///< Resource path within the client app's namespace.
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t entryRef = hub_GetClientNamespace(io_GetClientSessionRef());
    if (entryRef != NULL)
    {
        entryRef = resTree_FindEntry(entryRef, path);
    }
    if (entryRef != NULL)
    {
        admin_EntryType_t entryType = resTree_GetEntryType(entryRef);

        if ((entryType != ADMIN_ENTRY_TYPE_INPUT) && (entryType != ADMIN_ENTRY_TYPE_OUTPUT))
        {
            LE_DEBUG("'%s' is not an Input or an Output.", path);
            entryRef = NULL;
        }
    }

    return entryRef;
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an input resource, which is used to push data into the Data Hub.
 *
 * Does nothing if the resource already exists.
 *
 * @return
 *  - LE_OK if successful.
 *  - LE_DUPLICATE if a resource by that name exists but with different direction, type or units.
 *  - LE_NO_MEMORY if the client is not permitted to create that many resources.
 */
//--------------------------------------------------------------------------------------------------
le_result_t io_CreateInput
(
    const char* path,
        ///< [IN] Path within the client app's resource namespace.
    io_DataType_t dataType,
        ///< [IN] The data type.
    const char* units
        ///< [IN] e.g., "degC" (see senml); "" = unspecified.
)
//--------------------------------------------------------------------------------------------------
{
    LE_DEBUG("'%s' <%s> '%s'.", path, hub_GetDataTypeName(dataType), units);

    resTree_EntryRef_t resRef = NULL;

    resTree_EntryRef_t nsRef = hub_GetClientNamespace(io_GetClientSessionRef());

    // Check for another resource having the same name in the same namespace.
    if (nsRef != NULL)
    {
        resRef = resTree_FindEntry(nsRef, path);
    }
    if (resRef != NULL)
    {
        switch (resTree_GetEntryType(resRef))
        {
            case ADMIN_ENTRY_TYPE_INPUT:

                // Check data type and units to see if they match.
                if (   (resTree_GetDataType(resRef) != dataType)
                    || (strcmp(units, resTree_GetUnits(resRef)) != 0))
                {
                    return LE_DUPLICATE;
                }

                // The object already exists.  Nothing more needs to be done.
                return LE_OK;

            case ADMIN_ENTRY_TYPE_OUTPUT:
            case ADMIN_ENTRY_TYPE_OBSERVATION:

                // These conflict.
                return LE_DUPLICATE;

            case ADMIN_ENTRY_TYPE_NAMESPACE:
            case ADMIN_ENTRY_TYPE_PLACEHOLDER:

                // These can be upgraded to Input objects by resTree_GetInput().
                break;

            case ADMIN_ENTRY_TYPE_NONE:

                LE_FATAL("Unexpected entry type.");
        }
    }

    // Get/Create the Input resource.
    resRef = resTree_GetInput(nsRef, path, dataType, units);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Failed to create Input '/app/%s/%s'.", resTree_GetEntryName(nsRef), path);
        return LE_FAULT;    // Client has been killed, so it doesn't matter what we return.
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the example value for a JSON-type Input resource.
 *
 * Does nothing if the resource is not found, is not an input, or doesn't have a JSON type.
 */
//--------------------------------------------------------------------------------------------------
void io_SetJsonExample
(
    const char* path,
        ///< [IN] Path within the client app's resource namespace.
    const char* example
        ///< [IN] The example JSON value string.
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_ERROR("Resource '%s' does not exist.", path);
    }
    else if (resTree_GetEntryType(resRef) != ADMIN_ENTRY_TYPE_INPUT)
    {
        LE_ERROR("Resource '%s' is not an input.", path);
    }
    else if (resTree_GetDataType(resRef) != IO_DATA_TYPE_JSON)
    {
        LE_ERROR("Resource '%s' does not have JSON data type.", path);
    }
    else
    {
        dataSample_Ref_t sampleRef = dataSample_CreateJson(0, example);

        if (sampleRef != NULL)
        {
            resTree_SetJsonExample(resRef, sampleRef);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an output resource, which is used to push data into the Data Hub.
 *
 * Does nothing if the resource already exists.
 *
 * @return
 *  - LE_OK if successful.
 *  - LE_DUPLICATE if a resource by that name exists but with different direction, type or units.
 *  - LE_NO_MEMORY if the client is not permitted to create that many resources.
 */
//--------------------------------------------------------------------------------------------------
le_result_t io_CreateOutput
(
    const char* path,
        ///< [IN] Path within the client app's resource namespace.
    io_DataType_t dataType,
        ///< [IN] The data type.
    const char* units
        ///< [IN] e.g., "degC" (see senml); "" = unspecified.
)
//--------------------------------------------------------------------------------------------------
{
    LE_DEBUG("'%s' <%s> '%s'.", path, hub_GetDataTypeName(dataType), units);

    resTree_EntryRef_t resRef = NULL;

    resTree_EntryRef_t nsRef = hub_GetClientNamespace(io_GetClientSessionRef());

    // Check for another resource having the same name in the same namespace.
    if (nsRef != NULL)
    {
        resRef = resTree_FindEntry(nsRef, path);
    }
    if (resRef != NULL)
    {
        switch (resTree_GetEntryType(resRef))
        {
            case ADMIN_ENTRY_TYPE_OUTPUT:

                // Check data type and units to see if they match.
                if (   (resTree_GetDataType(resRef) != dataType)
                    || (strcmp(units, resTree_GetUnits(resRef)) != 0))
                {
                    return LE_DUPLICATE;
                }

                // The object already exists.  Nothing more needs to be done.
                return LE_OK;

            case ADMIN_ENTRY_TYPE_INPUT:
            case ADMIN_ENTRY_TYPE_OBSERVATION:

                // These conflict.
                return LE_DUPLICATE;

            case ADMIN_ENTRY_TYPE_NAMESPACE:
            case ADMIN_ENTRY_TYPE_PLACEHOLDER:

                // These can be upgraded to Output objects by resTree_GetOutput().
                break;

            case ADMIN_ENTRY_TYPE_NONE:

                LE_FATAL("Unexpected entry type.");
        }
    }

    // Create the Output resource.
    resRef = resTree_GetOutput(nsRef, path, dataType, units);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Failed to create Output '/app/%s/%s'.", resTree_GetEntryName(nsRef), path);
        return LE_FAULT;    // Client has been killed, so it doesn't matter what we return.
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Delete a resource.
 *
 * Does nothing if the resource doesn't exist.
 */
//--------------------------------------------------------------------------------------------------
void io_DeleteResource
(
    const char* path
        ///< [IN] Resource path within the client app's namespace.
)
//--------------------------------------------------------------------------------------------------
{
    LE_DEBUG("'%s'", path);

    // If the resource exists, delete it.
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef != NULL)
    {
        resTree_DeleteIO(resRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Push a trigger type data sample.
 */
//--------------------------------------------------------------------------------------------------
void io_PushTrigger
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double timestamp
        ///< [IN] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
        ///< Zero = now.
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Client tried to push data to a non-existent resource '%s'.", path);
        return;
    }

    // Create a Data Sample object for this new sample.
    dataSample_Ref_t sampleRef = dataSample_CreateTrigger(timestamp);

    // Push the sample to the Resource.
    resTree_Push(resRef, IO_DATA_TYPE_TRIGGER, sampleRef);
}



//--------------------------------------------------------------------------------------------------
/**
 * Push a Boolean type data sample.
 */
//--------------------------------------------------------------------------------------------------
void io_PushBoolean
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double timestamp,
        ///< [IN] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
        ///< Zero = now.
    bool value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Client tried to push data to a non-existent resource '%s'.", path);
        return;
    }

    // Create a Data Sample object for this new sample.
    dataSample_Ref_t sampleRef = dataSample_CreateBoolean(timestamp, value);

    // Push the sample to the Resource.
    resTree_Push(resRef, IO_DATA_TYPE_BOOLEAN, sampleRef);
}



//--------------------------------------------------------------------------------------------------
/**
 * Push a numeric type data sample.
 */
//--------------------------------------------------------------------------------------------------
void io_PushNumeric
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double timestamp,
        ///< [IN] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
        ///< Zero = now.
    double value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Client tried to push data to a non-existent resource '%s'.", path);
        return;
    }

    // Create a Data Sample object for this new sample.
    dataSample_Ref_t sampleRef = dataSample_CreateNumeric(timestamp, value);

    // Push the sample to the Resource.
    resTree_Push(resRef, IO_DATA_TYPE_NUMERIC, sampleRef);
}



//--------------------------------------------------------------------------------------------------
/**
 * Push a string type data sample.
 */
//--------------------------------------------------------------------------------------------------
void io_PushString
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double timestamp,
        ///< [IN] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
        ///< Zero = now.
    const char* value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Client tried to push data to a non-existent resource '%s'.", path);
        return;
    }

    // Create a Data Sample object for this new sample.
    dataSample_Ref_t sampleRef = dataSample_CreateString(timestamp, value);

    // Push the sample to the Resource.
    resTree_Push(resRef, IO_DATA_TYPE_STRING, sampleRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Push a JSON data sample.
 */
//--------------------------------------------------------------------------------------------------
void io_PushJson
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double timestamp,
        ///< [IN] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
        ///< Zero = now.
    const char* value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Client tried to push data to a non-existent resource '%s'.", path);
        return;
    }

    if (json_IsValid(value))
    {
        // Create a Data Sample object for this new sample.
        dataSample_Ref_t sampleRef = dataSample_CreateJson(timestamp, value);

        // Push the sample to the Resource.
        resTree_Push(resRef, IO_DATA_TYPE_JSON, sampleRef);
    }
    else
    {
        LE_WARN("Rejecting invalid JSON string '%s'.", value);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add a handler function to be called when a value is pushed to (and accepted by) an Input
 * or Output in the client app's namespace.
 *
 * @return A reference to the handler or NULL if failed and client has been killed.
 */
//--------------------------------------------------------------------------------------------------
static hub_HandlerRef_t AddPushHandler
(
    const char* path,   ///< Resource path within the client app's namespace.
    io_DataType_t dataType,
    void* callbackPtr,  ///< Callback function pointer
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t nsRef = hub_GetClientNamespace(io_GetClientSessionRef());
    if (nsRef == NULL)
    {
        LE_KILL_CLIENT("Client tried to register a push handler before creating any resources.");
        return NULL;
    }

    resTree_EntryRef_t resRef = resTree_FindEntry(nsRef, path);

    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Attempt to register Push handler on non-existent resource '/app/%s/%s'.",
                       resTree_GetEntryName(nsRef),
                       path);
        return NULL;
    }

    admin_EntryType_t entryType = resTree_GetEntryType(resRef);
    if ((entryType != ADMIN_ENTRY_TYPE_INPUT) && (entryType != ADMIN_ENTRY_TYPE_OUTPUT))
    {
        LE_KILL_CLIENT("Attempt to register Push handler before creating resource '/app/%s/%s'.",
                       resTree_GetEntryName(nsRef),
                       path);
        return NULL;
    }

    hub_HandlerRef_t handlerRef = resTree_AddPushHandler(resRef, dataType, callbackPtr, contextPtr);

    // If the resource has a current value call the push handler now (if it's a data type match).
    dataSample_Ref_t sampleRef = resTree_GetCurrentValue(resRef);
    if (sampleRef != NULL)
    {
        handler_Call(handlerRef, resTree_GetDataType(resRef), sampleRef);
    }

    return handlerRef;
}


//--------------------------------------------------------------------------------------------------
/**
 * Add handler function for EVENT 'io_TriggerPush'
 */
//--------------------------------------------------------------------------------------------------
io_TriggerPushHandlerRef_t io_AddTriggerPushHandler
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    io_TriggerPushHandlerFunc_t callbackPtr,
        ///< [IN]
    void* contextPtr
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    return (io_TriggerPushHandlerRef_t)AddPushHandler(path,
                                                      IO_DATA_TYPE_TRIGGER,
                                                      callbackPtr,
                                                      contextPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove handler function for EVENT 'io_TriggerPush'
 */
//--------------------------------------------------------------------------------------------------
void io_RemoveTriggerPushHandler
(
    io_TriggerPushHandlerRef_t handlerRef
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_RemovePushHandler((hub_HandlerRef_t)handlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Add handler function for EVENT 'io_BooleanPush'
 */
//--------------------------------------------------------------------------------------------------
io_BooleanPushHandlerRef_t io_AddBooleanPushHandler
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    io_BooleanPushHandlerFunc_t callbackPtr,
        ///< [IN]
    void* contextPtr
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    return (io_BooleanPushHandlerRef_t)AddPushHandler(path,
                                                      IO_DATA_TYPE_BOOLEAN,
                                                      callbackPtr,
                                                      contextPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove handler function for EVENT 'io_BooleanPush'
 */
//--------------------------------------------------------------------------------------------------
void io_RemoveBooleanPushHandler
(
    io_BooleanPushHandlerRef_t handlerRef
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_RemovePushHandler((hub_HandlerRef_t)handlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Add handler function for EVENT 'io_NumericPush'
 */
//--------------------------------------------------------------------------------------------------
io_NumericPushHandlerRef_t io_AddNumericPushHandler
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    io_NumericPushHandlerFunc_t callbackPtr,
        ///< [IN]
    void* contextPtr
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    return (io_NumericPushHandlerRef_t)AddPushHandler(path,
                                                      IO_DATA_TYPE_NUMERIC,
                                                      callbackPtr,
                                                      contextPtr);
}



//--------------------------------------------------------------------------------------------------
/**
 * Remove handler function for EVENT 'io_NumericPush'
 */
//--------------------------------------------------------------------------------------------------
void io_RemoveNumericPushHandler
(
    io_NumericPushHandlerRef_t handlerRef
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_RemovePushHandler((hub_HandlerRef_t)handlerRef);
}



//--------------------------------------------------------------------------------------------------
/**
 * Add handler function for EVENT 'io_StringPush'
 */
//--------------------------------------------------------------------------------------------------
io_StringPushHandlerRef_t io_AddStringPushHandler
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    io_StringPushHandlerFunc_t callbackPtr,
        ///< [IN]
    void* contextPtr
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    return (io_StringPushHandlerRef_t)AddPushHandler(path,
                                                     IO_DATA_TYPE_STRING,
                                                     callbackPtr,
                                                     contextPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove handler function for EVENT 'io_StringPush'
 */
//--------------------------------------------------------------------------------------------------
void io_RemoveStringPushHandler
(
    io_StringPushHandlerRef_t handlerRef
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_RemovePushHandler((hub_HandlerRef_t)handlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Add handler function for EVENT 'io_JsonPush'
 */
//--------------------------------------------------------------------------------------------------
io_JsonPushHandlerRef_t io_AddJsonPushHandler
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    io_JsonPushHandlerFunc_t callbackPtr,
        ///< [IN]
    void* contextPtr
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    return (io_JsonPushHandlerRef_t)AddPushHandler(path,
                                                   IO_DATA_TYPE_JSON,
                                                   callbackPtr,
                                                   contextPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove handler function for EVENT 'io_JsonPush'
 */
//--------------------------------------------------------------------------------------------------
void io_RemoveJsonPushHandler
(
    io_JsonPushHandlerRef_t handlerRef
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_RemovePushHandler((hub_HandlerRef_t)handlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Mark an Output resource "optional".  (By default, they are marked "mandatory".)
 */
//--------------------------------------------------------------------------------------------------
void io_MarkOptional
(
    const char* path
        ///< [IN] Resource path within the client app's namespace.
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Attempt to mark non-existent resource optional at '%s'.", path);
    }
    else if (resTree_GetEntryType(resRef) != ADMIN_ENTRY_TYPE_OUTPUT)
    {
        LE_KILL_CLIENT("Attempt to mark non-Output resource optional at '%s'.", path);
    }
    else
    {
        resTree_MarkOptional(resRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set a Boolean type value as the default value of a given resource.
 *
 * @note This will be ignored if the resource already has a default value.
 */
//--------------------------------------------------------------------------------------------------
void io_SetBooleanDefault
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    bool value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Attempt to set default value of non-existent resource '%s'.", path);
    }
    else if (resTree_GetDataType(resRef) != IO_DATA_TYPE_BOOLEAN)
    {
        LE_KILL_CLIENT("Attempt to set default value to wrong type for resource '%s'.", path);
    }
    else if (!resTree_HasDefault(resRef))
    {
        // Create a Data Sample object for this new sample.
        dataSample_Ref_t sampleRef = dataSample_CreateBoolean(0.0, value);

        resTree_SetDefault(resRef, IO_DATA_TYPE_BOOLEAN, sampleRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set a numeric type value as the default value of a given resource.
 *
 * @note This will be ignored if the resource already has a default value.
 */
//--------------------------------------------------------------------------------------------------
void io_SetNumericDefault
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Attempt to set default value of non-existent resource '%s'.", path);
    }
    else if (resTree_GetDataType(resRef) != IO_DATA_TYPE_NUMERIC)
    {
        LE_KILL_CLIENT("Attempt to set default value to wrong type for resource '%s'.", path);
    }
    else if (!resTree_HasDefault(resRef))
    {
        // Create a Data Sample object for this new sample.
        dataSample_Ref_t sampleRef = dataSample_CreateNumeric(0.0, value);

        resTree_SetDefault(resRef, IO_DATA_TYPE_NUMERIC, sampleRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set a string type value as the default value of a given resource.
 *
 * @note This will be ignored if the resource already has a default value.
 */
//--------------------------------------------------------------------------------------------------
void io_SetStringDefault
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    const char* value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Attempt to set default value of non-existent resource '%s'.", path);
    }
    else if (resTree_GetDataType(resRef) != IO_DATA_TYPE_STRING)
    {
        LE_KILL_CLIENT("Attempt to set default value to wrong type for resource '%s'.", path);
    }
    else if (!resTree_HasDefault(resRef))
    {
        // Create a Data Sample object for this new sample.
        dataSample_Ref_t sampleRef = dataSample_CreateString(0.0, value);

        resTree_SetDefault(resRef, IO_DATA_TYPE_STRING, sampleRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set a JSON type value as the default value of a given resource.
 *
 * @note This will be ignored if the resource already has a default value.
 */
//--------------------------------------------------------------------------------------------------
void io_SetJsonDefault
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    const char* value
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        LE_KILL_CLIENT("Attempt to set default value of non-existent resource '%s'.", path);
    }
    else if (resTree_GetDataType(resRef) != IO_DATA_TYPE_JSON)
    {
        LE_KILL_CLIENT("Attempt to set default value to wrong type for resource '%s'.", path);
    }
    else if (!resTree_HasDefault(resRef))
    {
        if (json_IsValid(value))
        {
            // Create a Data Sample object for this new sample.
            dataSample_Ref_t sampleRef = dataSample_CreateJson(0.0, value);

            resTree_SetDefault(resRef, IO_DATA_TYPE_JSON, sampleRef);
        }
        else
        {
            LE_KILL_CLIENT("Invalid JSON string as default value for resource '%s' (%s).",
                           path,
                           value);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the current value of a given resource, with type check.
 *
 * @return A reference to the data sample, or NULL if not available.
 *
 * @note Kills the client if the type doesn't match.
 */
//--------------------------------------------------------------------------------------------------
static dataSample_Ref_t GetCurrentValue
(
    resTree_EntryRef_t resRef,
    io_DataType_t dataType
)
//--------------------------------------------------------------------------------------------------
{
    if (resTree_GetDataType(resRef) != dataType)
    {
        LE_KILL_CLIENT("Fetch of wrong data type on resource.");
        return NULL;
    }

    return resTree_GetCurrentValue(resRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Fetch the timestamp of the current value of an Input or Output resource with any data type.
 *
 * @return
 *  - LE_OK if successful.
 *  - LE_NOT_FOUND if the resource does not exist.
 *  - LE_UNAVAILABLE if the resource does not currently have a value.
 */
//--------------------------------------------------------------------------------------------------
le_result_t io_GetTimestamp
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double* timestampPtr
        ///< [OUT] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        return LE_NOT_FOUND;
    }

    dataSample_Ref_t currentValue = resTree_GetCurrentValue(resRef);
    if (currentValue == NULL)
    {
        return LE_UNAVAILABLE;
    }

    *timestampPtr = dataSample_GetTimestamp(currentValue);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Fetch the current value of a Boolean type Input or Output resource.
 *
 * @return
 *  - LE_OK if successful.
 *  - LE_NOT_FOUND if the resource does not exist.
 *  - LE_UNAVAILABLE if the resource does not currently have a value.
 */
//--------------------------------------------------------------------------------------------------
le_result_t io_GetBoolean
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double* timestampPtr,
        ///< [OUT] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
    bool* valuePtr
        ///< [OUT]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        return LE_NOT_FOUND;
    }

    dataSample_Ref_t currentValue = GetCurrentValue(resRef, IO_DATA_TYPE_BOOLEAN);
    if (currentValue == NULL)
    {
        return LE_UNAVAILABLE;
    }

    *valuePtr = dataSample_GetBoolean(currentValue);
    *timestampPtr = dataSample_GetTimestamp(currentValue);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Fetch the current value of a numeric type Input or Output resource.
 *
 * @return
 *  - LE_OK if successful.
 *  - LE_NOT_FOUND if the resource does not exist.
 *  - LE_UNAVAILABLE if the resource does not currently have a value.
 */
//--------------------------------------------------------------------------------------------------
le_result_t io_GetNumeric
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double* timestampPtr,
        ///< [OUT] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
    double* valuePtr
        ///< [OUT]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        return LE_NOT_FOUND;
    }

    dataSample_Ref_t currentValue = GetCurrentValue(resRef, IO_DATA_TYPE_NUMERIC);
    if (currentValue == NULL)
    {
        return LE_UNAVAILABLE;
    }

    *valuePtr = dataSample_GetNumeric(currentValue);
    *timestampPtr = dataSample_GetTimestamp(currentValue);

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Fetch the current value of a string type Input or Output resource.
 *
 * @return
 *  - LE_OK if successful.
 *  - LE_OVERFLOW if the value buffer was too small to hold the value.
 *  - LE_NOT_FOUND if the resource does not exist.
 *  - LE_UNAVAILABLE if the resource does not currently have a value.
 */
//--------------------------------------------------------------------------------------------------
le_result_t io_GetString
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double* timestampPtr,
        ///< [OUT] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
    char* value,
        ///< [OUT]
    size_t valueSize
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        return LE_NOT_FOUND;
    }

    dataSample_Ref_t currentValue = GetCurrentValue(resRef, IO_DATA_TYPE_STRING);
    if (currentValue == NULL)
    {
        return LE_UNAVAILABLE;
    }

    *timestampPtr = dataSample_GetTimestamp(currentValue);
    return le_utf8_Copy(value, dataSample_GetString(currentValue), valueSize, NULL);
}


//--------------------------------------------------------------------------------------------------
/**
 * Fetch the current value of an Input or Output resource (of any data type) in JSON format.
 *
 * @return
 *  - LE_OK if successful.
 *  - LE_OVERFLOW if the value buffer was too small to hold the value.
 *  - LE_NOT_FOUND if the resource does not exist.
 *  - LE_UNAVAILABLE if the resource does not currently have a value.
 */
//--------------------------------------------------------------------------------------------------
le_result_t io_GetJson
(
    const char* path,
        ///< [IN] Resource path within the client app's namespace.
    double* timestampPtr,
        ///< [OUT] Timestamp in seconds since the Epoch 1970-01-01 00:00:00 +0000 (UTC).
    char* value,
        ///< [OUT]
    size_t valueSize
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t resRef = FindResource(path);
    if (resRef == NULL)
    {
        return LE_NOT_FOUND;
    }

    dataSample_Ref_t currentValue = resTree_GetCurrentValue(resRef);
    if (currentValue == NULL)
    {
        return LE_UNAVAILABLE;
    }

    *timestampPtr = dataSample_GetTimestamp(currentValue);
    return dataSample_ConvertToJson(currentValue, resTree_GetDataType(resRef), value, valueSize);
}


//--------------------------------------------------------------------------------------------------
/**
 * Add handler function for EVENT 'io_UpdateStartEnd'
 */
//--------------------------------------------------------------------------------------------------
io_UpdateStartEndHandlerRef_t io_AddUpdateStartEndHandler
(
    io_UpdateStartEndHandlerFunc_t callbackPtr,
        ///< [IN]
    void* contextPtr
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    UpdateStartEndHandler_t* handlerPtr = le_mem_ForceAlloc(UpdateStartEndHandlerPool);

    handlerPtr->link = LE_DLS_LINK_INIT;

    handlerPtr->callback = callbackPtr;
    handlerPtr->contextPtr = contextPtr;

    le_dls_Queue(&UpdateStartEndHandlerList, &handlerPtr->link);

    return (io_UpdateStartEndHandlerRef_t)handlerPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove handler function for EVENT 'io_UpdateStartEnd'
 */
//--------------------------------------------------------------------------------------------------
void io_RemoveUpdateStartEndHandler
(
    io_UpdateStartEndHandlerRef_t handlerRef
        ///< [IN]
)
//--------------------------------------------------------------------------------------------------
{
    UpdateStartEndHandler_t* handlerPtr = (UpdateStartEndHandler_t*)handlerRef;

    le_dls_Remove(&UpdateStartEndHandlerList, &handlerPtr->link);

    le_mem_Release(handlerPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Call all the registered Update Start/End Handlers.
 */
//--------------------------------------------------------------------------------------------------
static void CallUpdateStartEndHandlers
(
    bool isStarting
)
//--------------------------------------------------------------------------------------------------
{
    le_dls_Link_t* linkPtr = le_dls_Peek(&UpdateStartEndHandlerList);

    while (linkPtr != NULL)
    {
        UpdateStartEndHandler_t* handlerPtr = CONTAINER_OF(linkPtr, UpdateStartEndHandler_t, link);

        handlerPtr->callback(isStarting, handlerPtr->contextPtr);

        linkPtr = le_dls_PeekNext(&UpdateStartEndHandlerList, linkPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Recursively walk the branch of the resource tree rooted at a given node, deleting anything that
 * doesn't have admin settings or children and converting anything that does have admin settings or
 * children into a placeholder or namespace.
 */
//--------------------------------------------------------------------------------------------------
static void CleanUp
(
    resTree_EntryRef_t resource
)
//--------------------------------------------------------------------------------------------------
{
    // Do depth-first recursion.
    resTree_EntryRef_t child = resTree_GetFirstChild(resource);
    while (child != NULL)
    {
        resTree_EntryRef_t nextChild = resTree_GetNextSibling(child);
        CleanUp(child);
        child = nextChild;
    }

    switch (resTree_GetEntryType(resource))
    {
        case ADMIN_ENTRY_TYPE_NAMESPACE:
        case ADMIN_ENTRY_TYPE_PLACEHOLDER:

            // These don't need to be deleted.
            // A Namespace will automatically be cleaned up when all its children go away.
            // A Placeholder should be kept until the Admin app removes all its admin settings.
            break;

        case ADMIN_ENTRY_TYPE_INPUT:
        case ADMIN_ENTRY_TYPE_OUTPUT:

            // If this is an Input or Output resource, delete it now.
            // This will convert it to a Placeholder if it has administrative settings.
            resTree_DeleteIO(resource);
            break;

        case ADMIN_ENTRY_TYPE_OBSERVATION:
        case ADMIN_ENTRY_TYPE_NONE:

            // These should never be seen in an I/O API app namespace.
            LE_FATAL("Unexpected resource type found in app's namespace.");
            break;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function that gets called when an API client session closes.
 * This allows us to clean up after a client that has gone away.
 */
//--------------------------------------------------------------------------------------------------
static void SessionCloseHandler
(
    le_msg_SessionRef_t sessionRef,
    void* contextPtr // not used
)
//--------------------------------------------------------------------------------------------------
{
    // Get the resource node at the root of this client's namespace.
    resTree_EntryRef_t nsRef = hub_GetClientNamespace(sessionRef);

    if (nsRef != NULL)
    {
        // NOTE: If the namespace cannot be retrieved, then we can rest assured that no
        //       namespace was ever created for this client.  You can't create nodes under
        //       a namespace without first fetching that namespace at least once, and
        //       if the app's namespace is *ever* fetched, it is cached in the IPC session's
        //       context, so it will be found even if the whole client app is dead and gone.

        LE_DEBUG("App '%s' closed its I/O API session.", resTree_GetEntryName(nsRef));

        // Walk the tree, deleting anything that doesn't have admin settings and converting
        // anything that does have admin settings into a placeholder.
        CleanUp(nsRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Initializes the module.  Must be called before any other functions in the module are called.
 */
//--------------------------------------------------------------------------------------------------
void ioService_Init
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    UpdateStartEndHandlerPool = le_mem_CreatePool("UpdateStartEndHandlers",
                                                  sizeof(UpdateStartEndHandler_t));

    // Register for notification of client sessions closing, so we can convert Input and Output
    // objects into placeholders (or delete them) when the clients that created them go away.
    le_msg_AddServiceCloseHandler(io_GetServiceRef(), SessionCloseHandler, NULL);
}


//--------------------------------------------------------------------------------------------------
/**
 * Notify apps that care that administrative changes are about to be performed.
 *
 * This will result in call-backs to any handlers registered using io_AddUpdateStartEndHandler().
 */
//--------------------------------------------------------------------------------------------------
void ioService_StartUpdate
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    CallUpdateStartEndHandlers(true);
}


//--------------------------------------------------------------------------------------------------
/**
 * Notify apps that care that all pending administrative changes have been applied and that
 * normal operation may resume.
 *
 * This will result in call-backs to any handlers registered using io_AddUpdateStartEndHandler().
 */
//--------------------------------------------------------------------------------------------------
void ioService_EndUpdate
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    CallUpdateStartEndHandlers(false);
}
