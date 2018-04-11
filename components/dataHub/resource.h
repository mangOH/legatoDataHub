//--------------------------------------------------------------------------------------------------
/**
 * Inter-module interfaces provided by the Resource module, which implements the
 * Resource base class and the Placeholder resource class.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef RESOURCE_H_INCLUDE_GUARD
#define RESOURCE_H_INCLUDE_GUARD


// Forward declaration needed by res_Resource_t.entryRef.  See resTree.h
typedef struct resTree_Entry* resTree_EntryRef_t;


//--------------------------------------------------------------------------------------------------
/**
 * Base class for all types of Resource found in the resource tree.
 *
 * @note Inherit from this by including it as a member in the sub-class struct.
 *
 * @warning DO NOT reference the members of this structure anywhere but this file or resource.c.
 */
//--------------------------------------------------------------------------------------------------
typedef struct res_Resource
{
    resTree_EntryRef_t entryRef;  ///< Reference to the resource tree entry this is attached to.
    char units[HUB_MAX_UNITS_BYTES]; ///< String describing the units, or "" if unspecified.
    io_DataType_t currentType;  ///< Data type of the current value of this resource.
    dataSample_Ref_t currentValue; ///< The current value of this resource; NULL if none yet.
    io_DataType_t pushedType;  ///< Data type of last value pushed to this resource.
    dataSample_Ref_t pushedValue; ///< Last value pushed to resource (& accepted); NULL if none yet.
    struct res_Resource* srcPtr; ///< Ptr to resource that data samples will normally come from.
    le_dls_List_t destList; ///< List of routes to which data samples should be pushed.
    le_dls_Link_t destListLink; ///< Used to link into another resource's destList.
    dataSample_Ref_t overrideValue;///< Ref to override data sample; NULL if no override in effect.
    io_DataType_t overrideType;///< Data type of the override, if overrideRef != NULL.
    dataSample_Ref_t defaultValue; ///< Ref to default value; NULL if no default set.
    io_DataType_t defaultType;///< Data type of the default value, if defaultRef != NULL.
}
res_Resource_t;


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the Resource module.
 *
 * @warning Must be called before any other functions in this module.
 */
//--------------------------------------------------------------------------------------------------
void res_Init
(
    void
);


//--------------------------------------------------------------------------------------------------
/**
 * Create an Input resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreateInput
(
    resTree_EntryRef_t entryRef, ///< The entry that this resource will be attached to.
    io_DataType_t dataType,
    const char* units
);


//--------------------------------------------------------------------------------------------------
/**
 * Create an Output resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreateOutput
(
    resTree_EntryRef_t entryRef, ///< The entry that this resource will be attached to.
    io_DataType_t dataType,
    const char* units
);


//--------------------------------------------------------------------------------------------------
/**
 * Create an Observation resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreateObservation
(
    resTree_EntryRef_t entryRef ///< The entry that this resource will be attached to.
);


//--------------------------------------------------------------------------------------------------
/**
 * Create a Placeholder resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreatePlaceholder
(
    resTree_EntryRef_t entryRef ///< The entry that this resource will be attached to.
);


//--------------------------------------------------------------------------------------------------
/**
 * Destruct a resource object.  This is called by the sub-class's destructor function to destruct
 * the parent class part of the object.
 */
//--------------------------------------------------------------------------------------------------
void res_Destruct
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the Units of a resource.
 *
 * @return
 *  - LE_OK if successful
 *  - LE_OVERFLOW if the units string is too long.
 */
//--------------------------------------------------------------------------------------------------
le_result_t res_SetUnits
(
    res_Resource_t* resPtr,
    const char* units
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the Units of a resource.
 *
 * @return Pointer to the units string (valid as long as the resource exists).
 */
//--------------------------------------------------------------------------------------------------
const char* res_GetUnits
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Find out what data type a given resource currently has.
 *
 * Note that the data type of Inputs and Outputs are set by the app that creates those resources.
 * All other resources will change data types as values are pushed to them.
 *
 * @return the data type.
 */
//--------------------------------------------------------------------------------------------------
io_DataType_t res_GetDataType
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the current value of a resource.
 *
 * @return Reference to the Data Sample object or NULL if the resource doesn't have a current value.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t res_GetCurrentValue
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the source resource of a given resource.
 *
 * Does nothing if the route already exists.
 *
 * @return
 *  - LE_OK if route already existed or new route was successfully created.
 *  - LE_DUPLICATE if the addition of this route would result in a loop.
 */
//--------------------------------------------------------------------------------------------------
le_result_t res_SetSource
(
    res_Resource_t* destPtr, ///< [IN] The destination resource.
    res_Resource_t* srcPtr   ///< [IN] The source resource (NULL to clear).
);


//--------------------------------------------------------------------------------------------------
/**
 * Fetches the data flow source resource entry from which a given resource expects to receive data
 * samples.
 *
 * @return Reference to the source entry or NULL if none configured.
 */
//--------------------------------------------------------------------------------------------------
resTree_EntryRef_t res_GetSource
(
    res_Resource_t* destPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Push a data sample to a resource.
 *
 * @note Takes ownership of the data sample reference.
 */
//--------------------------------------------------------------------------------------------------
void res_Push
(
    res_Resource_t* resPtr,    ///< The resource to push to.
    io_DataType_t dataType,         ///< The data type.
    const char* units,              ///< The units ("" = unspecified)
    dataSample_Ref_t dataSample     ///< The data sample (timestamp + value).
);


//--------------------------------------------------------------------------------------------------
/**
 * Add a Push Handler to an Output resource.
 *
 * @return Reference to the handler added.
 */
//--------------------------------------------------------------------------------------------------
hub_HandlerRef_t res_AddPushHandler
(
    res_Resource_t* resPtr, ///< Ptr to the Output resource.
    io_DataType_t dataType,
    void* callbackPtr,
    void* contextPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Remove a Push Handler from an Output resource.
 */
//--------------------------------------------------------------------------------------------------
void res_RemovePushHandler
(
    hub_HandlerRef_t handlerRef
);


//--------------------------------------------------------------------------------------------------
/**
 * Check whether a given resource has administrative settings.
 *
 * @note Currently ignores Observation-specific settings, because this function isn't needed
 *       for Observations.
 *
 * @return true if there are some administrative settings present, false otherwise.
 */
//--------------------------------------------------------------------------------------------------
bool res_HasAdminSettings
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Move all the administrative settings from one Resource object to another.
 */
//--------------------------------------------------------------------------------------------------
void res_MoveAdminSettings
(
    res_Resource_t* srcPtr,   ///< Move settings from this resource
    res_Resource_t* destPtr   ///< Move settings to this resource
);


//--------------------------------------------------------------------------------------------------
/**
 * Delete an Observation.
 */
//--------------------------------------------------------------------------------------------------
void res_DeleteObservation
(
    res_Resource_t* obsPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the minimum period between data samples accepted by a given Observation.
 *
 * This is used to throttle the rate of data passing into and through an Observation.
 */
//--------------------------------------------------------------------------------------------------
void res_SetMinPeriod
(
    res_Resource_t* resPtr,
    double minPeriod
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the minimum period between data samples accepted by a given Observation.
 *
 * @return The value, or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetMinPeriod
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the highest value in a range that will be accepted by a given Observation.
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void res_SetHighLimit
(
    res_Resource_t* resPtr,
    double highLimit
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the highest value in a range that will be accepted by a given Observation.
 *
 * @return The value, or NAN (not a number) if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetHighLimit
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the lowest value in a range that will be accepted by a given Observation.
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void res_SetLowLimit
(
    res_Resource_t* resPtr,
    double lowLimit
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the lowest value in a range that will be accepted by a given Observation.
 *
 * @return The value, or NAN (not a number) if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetLowLimit
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the magnitude that a new value must vary from the current value to be accepted by
 * a given Observation.
 *
 * Ignored for trigger types.
 *
 * For all other types, any non-zero value means accept any change, but drop if the same as current.
 */
//--------------------------------------------------------------------------------------------------
void res_SetChangeBy
(
    res_Resource_t* resPtr,
    double change
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the magnitude that a new value must vary from the current value to be accepted by
 * a given Observation.
 *
 * @return The value, or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetChangeBy
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the maximum number of data samples to buffer in a given Observation.  Buffers are FIFO
 * circular buffers. When full, the buffer drops the oldest value to make room for a new addition.
 */
//--------------------------------------------------------------------------------------------------
void res_SetBufferMaxCount
(
    res_Resource_t* resPtr,
    uint32_t count
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the buffer size setting for a given Observation.
 *
 * @return The buffer size (in number of samples) or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
uint32_t res_GetBufferMaxCount
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the minimum time between backups of an Observation's buffer to non-volatile storage.
 * If the buffer's size is non-zero and the backup period is non-zero, then the buffer will be
 * backed-up to non-volatile storage when it changes, but never more often than this period setting
 * specifies.
 */
//--------------------------------------------------------------------------------------------------
void res_SetBufferBackupPeriod
(
    res_Resource_t* resPtr,
    uint32_t seconds
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the minimum time between backups of an Observation's buffer to non-volatile storage.
 * See admin_SetBufferBackupPeriod() for more information.
 *
 * @return The buffer backup period (in seconds) or 0 if backups are disabled or the Observation
 *         does not exist.
 */
//--------------------------------------------------------------------------------------------------
uint32_t res_GetBufferBackupPeriod
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the default value of a resource.
 *
 * Will be discarded if setting the default value on an Input or Output that doesn't have the
 * same data type.
 */
//--------------------------------------------------------------------------------------------------
void res_SetDefault
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,
    dataSample_Ref_t value  /// Ownership of this reference is passed to the resource.
);


//--------------------------------------------------------------------------------------------------
/**
 * Discover whether a given resource has a default value.
 *
 * @return true if there is a default value set, false if not.
 */
//--------------------------------------------------------------------------------------------------
bool res_HasDefault
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the data type of the default value that is currently set on a given resource.
 *
 * @return The data type, or IO_DATA_TYPE_TRIGGER if not set.
 */
//--------------------------------------------------------------------------------------------------
io_DataType_t res_GetDefaultDataType
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the default value of a resource.
 *
 * @return the default value or NULL if not set.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t res_GetDefaultValue
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Remove any default value that might be set on a given resource.
 */
//--------------------------------------------------------------------------------------------------
void res_RemoveDefault
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set an override on a given resource.
 *
 * @note Override will be discarded by an Input or Output resource if the override's data type
 *       does not match the data type of the Input or Output.
 */
//--------------------------------------------------------------------------------------------------
void res_SetOverride
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,
    dataSample_Ref_t value
);


//--------------------------------------------------------------------------------------------------
/**
 * Find out whether the resource currently has an override in effect.
 *
 * @return true if the resource is overridden, false otherwise.
 */
//--------------------------------------------------------------------------------------------------
bool res_IsOverridden
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Remove any override that might be in effect for a given resource.
 */
//--------------------------------------------------------------------------------------------------
void res_RemoveOverride
(
    res_Resource_t* resPtr
);


#endif // RESOURCE_H_INCLUDE_GUARD