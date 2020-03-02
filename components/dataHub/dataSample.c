//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the Data Sample class.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "interfaces.h"
#include "dataHub.h"
#include "dataSample.h"
#include "json.h"


typedef double Timestamp_t;


//--------------------------------------------------------------------------------------------------
/**
 * Data sample class. An object of this type can hold various different types of timestamped
 * data sample.
 */
//--------------------------------------------------------------------------------------------------
typedef struct DataSample
{
    Timestamp_t timestamp;      ///< The timestamp on the data sample.

    /// Union of different types of values. Which union member to use depends on the data type
    /// recorded in the resTree_Resource_t.  This is an optimization; Data Samples appear more
    /// frequently than Resources
    union
    {
        bool     boolean;
        double   numeric;
        char    *stringPtr;
    } value;
}
DataSample_t;

/// Size of largest allowed strings in samples.
#define STRING_LARGE_BYTES  HUB_MAX_STRING_BYTES
/// Size of medium sized strings in samples.
#define STRING_MED_BYTES    300
/// Size of small strings in samples.
#define STRING_SMALL_BYTES  50

/// Default sample pool size.  This may be overridden in the .cdef.
#define DEFAULT_SAMPLE_POOL_SIZE 1000

/// Default number of large string pool entries.  This may be overridden in the .cdef.
#define DEFAULT_LARGE_STRING_POOL_SIZE 5

/// Number of medium string pool entries.
#define MED_STRING_POOL_SIZE                                                                \
    (((LE_MEM_BLOCKS(StringPool, DEFAULT_LARGE_STRING_POOL_SIZE) / 2) * STRING_LARGE_BYTES) \
        / STRING_MED_BYTES)

/// Number of small string pool entries.
#define SMALL_STRING_POOL_SIZE \
    (((MED_STRING_POOL_SIZE / 2) * STRING_MED_BYTES) / STRING_SMALL_BYTES)

/// Pool of Data Sample objects.
static le_mem_PoolRef_t DataSamplePool = NULL;
LE_MEM_DEFINE_STATIC_POOL(DataSamplePool, DEFAULT_SAMPLE_POOL_SIZE, sizeof(DataSample_t));

/// Pool for holding strings.
static le_mem_PoolRef_t StringPool = NULL;
LE_MEM_DEFINE_STATIC_POOL(StringPool, DEFAULT_LARGE_STRING_POOL_SIZE, STRING_LARGE_BYTES);


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the Data Sample module.
 */
//--------------------------------------------------------------------------------------------------
void dataSample_Init
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    le_mem_PoolRef_t layeredStringPool;

    DataSamplePool = le_mem_InitStaticPool(DataSamplePool, DEFAULT_SAMPLE_POOL_SIZE,
                        sizeof(DataSample_t));

    layeredStringPool = le_mem_InitStaticPool(StringPool, DEFAULT_LARGE_STRING_POOL_SIZE,
                            STRING_LARGE_BYTES);
    layeredStringPool = le_mem_CreateReducedPool(layeredStringPool, "MedStringPool",
                            MED_STRING_POOL_SIZE, STRING_MED_BYTES);
    StringPool = le_mem_CreateReducedPool(layeredStringPool, "SmallStringPool",
                    SMALL_STRING_POOL_SIZE, STRING_SMALL_BYTES);
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a new Data Sample object and returns a pointer to it.
 *
 * @warning Don't forget to set the value if it's not a trigger type.
 *
 * @return Ptr to the new object.
 */
//--------------------------------------------------------------------------------------------------
static inline DataSample_t* CreateSample
(
    le_mem_PoolRef_t pool,  ///< Pool to allocate the object from.
    Timestamp_t timestamp
)
//--------------------------------------------------------------------------------------------------
{
    DataSample_t* samplePtr = le_mem_Alloc(pool);

    if (timestamp == IO_NOW)
    {
        le_clk_Time_t currentTime = le_clk_GetAbsoluteTime();
        timestamp = (((double)(currentTime.usec)) / 1000000) + currentTime.sec;
    }

    samplePtr->timestamp = timestamp;

    return samplePtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a new Trigger type Data Sample.
 *
 * @return Ptr to the new object (with reference count 1).
 *
 * @note These are reference-counted memory pool objects.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t dataSample_CreateTrigger
(
    Timestamp_t timestamp
)
//--------------------------------------------------------------------------------------------------
{
    DataSample_t* samplePtr = CreateSample(DataSamplePool, timestamp);

    return samplePtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a new Boolean type Data Sample.
 *
 * @return Ptr to the new object.
 *
 * @note These are reference-counted memory pool objects.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t dataSample_CreateBoolean
(
    Timestamp_t timestamp,
    bool value
)
//--------------------------------------------------------------------------------------------------
{
    DataSample_t* samplePtr = CreateSample(DataSamplePool, timestamp);
    samplePtr->value.boolean = value;

    return samplePtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a new Numeric type Data Sample.
 *
 * @return Ptr to the new object.
 *
 * @note These are reference-counted memory pool objects.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t dataSample_CreateNumeric
(
    Timestamp_t timestamp,
    double value
)
//--------------------------------------------------------------------------------------------------
{
    DataSample_t* samplePtr = CreateSample(DataSamplePool, timestamp);
    samplePtr->value.numeric = value;

    return samplePtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a new String type Data Sample.
 *
 * @return Ptr to the new object.
 *
 * @note Copies the string value into the Data Sample.
 *
 * @note These are reference-counted memory pool objects.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t dataSample_CreateString
(
    Timestamp_t timestamp,
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    DataSample_t *samplePtr = CreateSample(DataSamplePool, timestamp);

    samplePtr->value.stringPtr = le_mem_StrDup(StringPool, value);
    LE_FATAL_IF(samplePtr->value.stringPtr == NULL,
        "Could not allocate space for string of size %" PRIuS, le_utf8_NumBytes(value));
    return samplePtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a new JSON type Data Sample.
 *
 * @return Ptr to the new object.
 *
 * @note Copies the JSON value into the Data Sample.
 *
 * @note These are reference-counted memory pool objects.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t dataSample_CreateJson
(
    double timestamp,
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    // Since the data type is not actually stored in the data sample itself, and since the
    // JSON values are stored in the same way that strings are...
    return dataSample_CreateString(timestamp, value);
}


//--------------------------------------------------------------------------------------------------
/**
 * Read the timestamp on a Data Sample.
 *
 * @return The timestamp.
 */
//--------------------------------------------------------------------------------------------------
Timestamp_t dataSample_GetTimestamp
(
    dataSample_Ref_t sampleRef
)
//--------------------------------------------------------------------------------------------------
{
    return sampleRef->timestamp;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read a Boolean value from a Data Sample.
 *
 * @return The value.
 *
 * @warning You had better be sure that this is a Boolean Data Sample.
 */
//--------------------------------------------------------------------------------------------------
bool dataSample_GetBoolean
(
    dataSample_Ref_t sampleRef
)
//--------------------------------------------------------------------------------------------------
{
    return sampleRef->value.boolean;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read a numeric value from a Data Sample.
 *
 * @return The value.
 *
 * @warning You had better be sure that this is a Numeric Data Sample.
 */
//--------------------------------------------------------------------------------------------------
double dataSample_GetNumeric
(
    dataSample_Ref_t sampleRef
)
//--------------------------------------------------------------------------------------------------
{
    return sampleRef->value.numeric;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read a string value from a Data Sample.
 *
 * @return Ptr to the value. DO NOT use this after releasing your reference to the sample.
 *
 * @warning You had better be sure that this is a String Data Sample.
 */
//--------------------------------------------------------------------------------------------------
const char* dataSample_GetString
(
    dataSample_Ref_t sampleRef
)
//--------------------------------------------------------------------------------------------------
{
    return sampleRef->value.stringPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read a JSON value from a Data Sample.
 *
 * @return Ptr to the value. DO NOT use this after releasing your reference to the sample.
 *
 * @warning You had better be sure that this is a JSON Data Sample.
 */
//--------------------------------------------------------------------------------------------------
const char* dataSample_GetJson
(
    dataSample_Ref_t sampleRef
)
//--------------------------------------------------------------------------------------------------
{
    // The data type is not actually stored in the data sample itself, and
    // JSON values are stored in the same way that strings are.
    return sampleRef->value.stringPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read any type of value from a Data Sample, as a printable UTF-8 string.
 *
 * @return
 *  - LE_OK if successful,
 *  - LE_OVERFLOW if the buffer provided is too small to hold the value.
 */
//--------------------------------------------------------------------------------------------------
le_result_t dataSample_ConvertToString
(
    dataSample_Ref_t sampleRef,
    io_DataType_t dataType, ///< [IN] The data type of the data sample.
    char* valueBuffPtr,     ///< [OUT] Ptr to buffer where value will be stored.
    size_t valueBuffSize    ///< [IN] Size of value buffer, in bytes.
)
//--------------------------------------------------------------------------------------------------
{
    if (dataType == IO_DATA_TYPE_STRING)
    {
        return le_utf8_Copy(valueBuffPtr, sampleRef->value.stringPtr, valueBuffSize, NULL);
    }
    else
    {
        return dataSample_ConvertToJson(sampleRef, dataType, valueBuffPtr, valueBuffSize);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Read any type of value from a Data Sample, in JSON format.
 *
 * @return
 *  - LE_OK if successful,
 *  - LE_OVERFLOW if the buffer provided is too small to hold the value.
 */
//--------------------------------------------------------------------------------------------------
le_result_t dataSample_ConvertToJson
(
    dataSample_Ref_t sampleRef,
    io_DataType_t dataType, ///< [IN] The data type of the data sample.
    char* valueBuffPtr,     ///< [OUT] Ptr to buffer where value will be stored.
    size_t valueBuffSize    ///< [IN] Size of value buffer, in bytes.
)
//--------------------------------------------------------------------------------------------------
{
    le_result_t result;
    size_t len;

    switch (dataType)
    {
        case IO_DATA_TYPE_TRIGGER:

            if (valueBuffSize > 0)
            {
                valueBuffPtr[0] = '\0';
                return LE_OK;
            }
            return LE_OVERFLOW;

        case IO_DATA_TYPE_BOOLEAN:
        {
            int i;

            if (sampleRef->value.boolean)
            {
                i = snprintf(valueBuffPtr, valueBuffSize, "true");
            }
            else
            {
                i = snprintf(valueBuffPtr, valueBuffSize, "false");
            }

            if (i >= (int) valueBuffSize)
            {
                return LE_OVERFLOW;
            }
            return LE_OK;
        }

        case IO_DATA_TYPE_NUMERIC:

            if ((int) valueBuffSize <= snprintf(valueBuffPtr, valueBuffSize, "%lf",
                                                sampleRef->value.numeric))
            {
                return LE_OVERFLOW;
            }
            return LE_OK;

        case IO_DATA_TYPE_STRING:

            // Must wrap the string value in quotes.
            // We need at least 3 bytes for the two quotes and a null terminator.
            if (valueBuffSize < 3)
            {
                return LE_OVERFLOW;
            }
            valueBuffPtr[0] = '"';
            valueBuffPtr++;
            valueBuffSize--;
            result = le_utf8_Copy(valueBuffPtr, sampleRef->value.stringPtr, valueBuffSize, &len);
            if ((result != LE_OK) || (len >= (valueBuffSize - 1)))  // need 1 more for the last '"'
            {
                return LE_OVERFLOW;
            }
            valueBuffPtr[len] = '"'; // replace null-terminator with '"'
            valueBuffPtr[len + 1] = '\0'; // null-terminate the string.
            return LE_OK;

        case IO_DATA_TYPE_JSON:

            // Already in JSON format, just copy it into the buffer.
            return le_utf8_Copy(valueBuffPtr, sampleRef->value.stringPtr, valueBuffSize, NULL);
    }

    LE_FATAL("Invalid data type %d.", dataType);
}


//--------------------------------------------------------------------------------------------------
/**
 * Extract an object member or array element from a JSON data value, based on a given
 * extraction specifier.
 *
 * The extraction specifiers look like "x" or "x.y" or "[3]" or "x[3].y", etc.
 *
 * @return Reference to the extracted data sample, or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t dataSample_ExtractJson
(
    dataSample_Ref_t sampleRef, ///< [IN] Original JSON data sample to extract from.
    const char* extractionSpec, ///< [IN] the extraction specification.
    io_DataType_t* dataTypePtr  ///< [OUT] Ptr to where to put the data type of the extracted object
)
//--------------------------------------------------------------------------------------------------
{
    char resultBuff[HUB_MAX_STRING_BYTES];
    json_DataType_t jsonType;

    le_result_t result = json_Extract(resultBuff,
                                      sizeof(resultBuff),
                                      dataSample_GetJson(sampleRef),
                                      extractionSpec,
                                      &jsonType);
    if (result != LE_OK)
    {
        LE_WARN("Failed to extract '%s' from JSON '%s'.",
                extractionSpec,
                dataSample_GetJson(sampleRef));
        return NULL;
    }
    else
    {
        switch (jsonType)
        {
            case JSON_TYPE_NULL:

                *dataTypePtr = IO_DATA_TYPE_TRIGGER;
                return dataSample_CreateTrigger(dataSample_GetTimestamp(sampleRef));

            case JSON_TYPE_BOOLEAN:

                *dataTypePtr = IO_DATA_TYPE_BOOLEAN;
                return dataSample_CreateBoolean(dataSample_GetTimestamp(sampleRef),
                                                json_ConvertToBoolean(resultBuff));

            case JSON_TYPE_NUMBER:

                *dataTypePtr = IO_DATA_TYPE_NUMERIC;
                return dataSample_CreateNumeric(dataSample_GetTimestamp(sampleRef),
                                                json_ConvertToNumber(resultBuff));

            case JSON_TYPE_STRING:

                *dataTypePtr = IO_DATA_TYPE_STRING;
                return dataSample_CreateString(dataSample_GetTimestamp(sampleRef),
                                               resultBuff);

            case JSON_TYPE_OBJECT:
            case JSON_TYPE_ARRAY:

                *dataTypePtr = IO_DATA_TYPE_JSON;
                return dataSample_CreateJson(dataSample_GetTimestamp(sampleRef),
                                             resultBuff);
        }

        LE_FATAL("Unexpected JSON type %d.", jsonType);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Create a copy of a Data Sample.
 *
 * @return Pointer to the new copy.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t dataSample_Copy
(
    io_DataType_t dataType,
    dataSample_Ref_t original
)
//--------------------------------------------------------------------------------------------------
{
    dataSample_Ref_t duplicate = le_mem_Alloc(DataSamplePool);

    *duplicate = *original;
    if ((dataType == IO_DATA_TYPE_STRING) || (dataType == IO_DATA_TYPE_JSON))
    {
        duplicate->value.stringPtr = le_mem_StrDup(StringPool, original->value.stringPtr);
    }

    return duplicate;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the timestamp of a Data Sample.
 */
//--------------------------------------------------------------------------------------------------
void dataSample_SetTimestamp
(
    dataSample_Ref_t sample,
    double timestamp
)
//--------------------------------------------------------------------------------------------------
{
    sample->timestamp = timestamp;
}
