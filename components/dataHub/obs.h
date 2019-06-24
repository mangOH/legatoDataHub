//--------------------------------------------------------------------------------------------------
/**
 * @file obs.h
 *
 * Interface definitions exposed by the Observation module to other modules within the Data Hub.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef OBS_H_INCLUDE_GUARD
#define OBS_H_INCLUDE_GUARD


//--------------------------------------------------------------------------------------------------
/**
 * Enumeration of all the supported transform types for observations.
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    OBS_TRANSFORM_TYPE_NONE = 0,
    OBS_TRANSFORM_TYPE_MEAN,
    OBS_TRANSFORM_TYPE_STDDEV,
    OBS_TRANSFORM_TYPE_MAX,
    OBS_TRANSFORM_TYPE_MIN,
}
obs_TransformType_t;


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the Observation module.
 *
 * @warning This must be called before any other function in this module is called.
 */
//--------------------------------------------------------------------------------------------------
void obs_Init
(
    void
);


//--------------------------------------------------------------------------------------------------
/**
 * Create an Observation object.  This allocates the object and initializes the class members,
 * but not the parent class members.
 *
 * @return Pointer to the new object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* obs_Create
(
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
);


//--------------------------------------------------------------------------------------------------
/**
 * Restore an Observation's data buffer from non-volatile backup, if one exists.
 */
//--------------------------------------------------------------------------------------------------
void obs_RestoreBackup
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Perform JSON extraction.  If the data type is not JSON, does nothing.
 *
 * @return LE_OK if successful.
 */
//--------------------------------------------------------------------------------------------------
le_result_t obs_DoJsonExtraction
(
    res_Resource_t* resPtr,
    io_DataType_t* dataTypePtr,     ///< [INOUT] the data type, may be changed by JSON extraction
    dataSample_Ref_t* valueRefPtr   ///< [INOUT] the data sample, may be replaced by JSON extraction
);


//--------------------------------------------------------------------------------------------------
/**
 * Determine whether the value should be accepted by a given Observation.
 *
 * @warning JSON extraction should be performed first if the data type is JSON.
 *
 * @return true if the value should be accepted.
 */
//--------------------------------------------------------------------------------------------------
bool obs_ShouldAccept
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,     ///< [IN] the data type
    dataSample_Ref_t valueRef   ///< [IN] the data sample
);


//--------------------------------------------------------------------------------------------------
/**
 * Perform processing of an accepted pushed data sample that is specific to an Observation
 * resource.
 */
//--------------------------------------------------------------------------------------------------
void obs_ProcessAccepted
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,     ///< Data type of the data sample.
    dataSample_Ref_t sampleRef  ///< Data sample.
);


//--------------------------------------------------------------------------------------------------
/**
 * Perform any post-filtering on a given Observation.
 *
 * @return The processed data sample
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t obs_ApplyTransform
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,     ///< Data type of the data sample.
    dataSample_Ref_t sampleRef  ///< Data sample.
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the minimum period between data samples accepted by a given Observation.
 *
 * This is used to throttle the rate of data passing into and through an Observation.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetMinPeriod
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
double obs_GetMinPeriod
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
void obs_SetHighLimit
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
double obs_GetHighLimit
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
void obs_SetLowLimit
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
double obs_GetLowLimit
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
void obs_SetChangeBy
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
double obs_GetChangeBy
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Perform a transform on buffered data. Value of the observation will be the output of the
 * transform
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetTransform
(
    res_Resource_t* resPtr,
    obs_TransformType_t transformType,
    const double* paramsPtr,
    size_t paramsSize
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the type of transform currently applied to an Observation.
 *
 * @return The TransformType
 */
//--------------------------------------------------------------------------------------------------
obs_TransformType_t obs_GetTransform
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the maximum number of data samples to buffer in a given Observation.  Buffers are FIFO
 * circular buffers. When full, the buffer drops the oldest value to make room for a new addition.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetBufferMaxCount
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
uint32_t obs_GetBufferMaxCount
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
void obs_SetBufferBackupPeriod
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
uint32_t obs_GetBufferBackupPeriod
(
    res_Resource_t* resPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Delete buffer backup files that aren't being used.
 */
//--------------------------------------------------------------------------------------------------
void obs_DeleteUnusedBackupFiles
(
    void
);


//--------------------------------------------------------------------------------------------------
/**
 * Read data out of a buffer.  Data is written to a given file descriptor in JSON-encoded format
 * as an array of objects containing a timestamp and a value (or just a timestamp for triggers).
 * E.g.,
 *
 * @code
 * [{"t":1537483647.125,"v":true},{"t":1537483657.128,"v":true}]
 * @endcode
 */
//--------------------------------------------------------------------------------------------------
void obs_ReadBufferJson
(
    res_Resource_t* resPtr, ///< Ptr to the resource object for the Observation.
    double startAfter,  ///< Start after this many seconds ago, or after an absolute number of
                        ///< seconds since the Epoch (if startafter > 30 years).
                        ///< Use NAN (not a number) to read the whole buffer.
    int outputFile, ///< File descriptor to write the data to.
    query_ReadCompletionFunc_t handlerPtr, ///< Completion callback.
    void* contextPtr    ///< Value to be passed to completion callback.
);


//--------------------------------------------------------------------------------------------------
/**
 * Find the oldest data sample in a given Observation's buffer that is newer than a given timestamp.
 *
 * @return Reference to the sample, or NULL if not found in buffer.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t obs_FindBufferedSampleAfter
(
    res_Resource_t* resPtr, ///< Ptr to the resource object for the Observation.
    double startAfter   ///< Start after this many seconds ago, or after an absolute number of
                        ///< seconds since the Epoch (if startafter > 30 years).
                        ///< Use NAN (not a number) to find the oldest.
);


//--------------------------------------------------------------------------------------------------
/**
 * Set the JSON member/element specifier for extraction of data from within a structured JSON
 * value received by a given Observation.
 *
 * If this is set, all non-JSON data will be ignored, and all JSON data that does not contain the
 * the specified object member or array element will also be ignored.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetJsonExtraction
(
    res_Resource_t* resPtr,  ///< Observation resource.
    const char* extractionSpec    ///< [IN] string specifying the JSON member/element to extract.
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the JSON member/element specifier for extraction of data from within a structured JSON
 * value received by a given Observation.
 *
 * @return Ptr to string containing JSON extraction specifier.  "" if not set.
 */
//--------------------------------------------------------------------------------------------------
const char* obs_GetJsonExtraction
(
    res_Resource_t* resPtr  ///< Observation resource.
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the minimum value found in an Observation's data set within a given time span.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double obs_QueryMin
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the maximum value found within a given time span in an Observation's buffer.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double obs_QueryMax
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the mean (average) of all values found within a given time span in an Observation's buffer.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double obs_QueryMean
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
);


//--------------------------------------------------------------------------------------------------
/**
 * Get the standard deviation of all values found within a given time span in an
 * Observation's buffer.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double obs_QueryStdDev
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
);


#endif // OBS_H_INCLUDE_GUARD
