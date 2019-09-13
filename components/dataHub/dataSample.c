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
        bool boolean;
        double numeric;
        char string[0]; ///< A string type data sample has space for the array after this struct.
                        ///< @warning This union MUST BE the LAST MEMBER of DataSample_t.
    } value;
}
DataSample_t;

/// The maximum number of bytes in a small string, including the null terminator.
/// Strings longer than this are considered "huge".
#define SMALL_STRING_BYTES 300

/// The number of bytes to allocate in a (small) String Data Sample Pool block.
#define SMALL_STRING_SAMPLE_OBJECT_BYTES (offsetof(DataSample_t, value) + SMALL_STRING_BYTES)

/// The number of bytes to allocate in a Huge String Data Sample Pool block.
#define HUGE_STRING_SAMPLE_OBJECT_BYTES (offsetof(DataSample_t, value) + HUB_MAX_STRING_BYTES)

/// Pool of Data Sample objects that don't hold strings.
static le_mem_PoolRef_t NonStringSamplePool = NULL;

/// Pool of Data Sample objects that hold strings.
static le_mem_PoolRef_t SmallStringSamplePool = NULL;

/// Pool of Data Sample objects that hold huge strings.
/// @note This needs to be a separate pool because otherwise the majority case of small strings
///       results in massive unnecessary memory consumption (internal fragmentation).
static le_mem_PoolRef_t HugeStringSamplePool = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Compute the length of the escaped character
 *
 * @return The length of the escaped character in bytes
 *
 * @note UTF-8 characters are supported. Only the first character is required to compute the length.
 */
//--------------------------------------------------------------------------------------------------
static size_t ComputeEscapedCharLength
(
    char inputChar ///< [IN] The character we want to know the length when it is escaped
)
{
    if ((inputChar > 31) && (inputChar != '\"') && (inputChar != '\\')) {
        // The character doesn't need to be escaped, so the length
        // to return is the character length
        return le_utf8_NumBytesInChar(inputChar);
    }

    switch (inputChar)
    {
        case '\\':
        case '\"':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            return 2;
            break;

        default:
            /* Unicode codepoint */
            return 5;
            break;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Escape a character
 *
 * This function transforms a UTF-8 character into its escaped version.
 * If the character doesn't need to be escaped then the character is copied as is.
 *
 * @warning escapedChar must point to a char array that has enough room to received the escaped
 *          character. To know the needed room prior to call this function, you can compute
 *          the escaped character length with ComputeEscapedCharLength().
 *
 * @note UTF-8 characters are supported.
 * @note This function manipules characters and not string, therefore no null-termination character
 *       is added at the end of the escaped character.
 *
 * @return The input character length in bytes (can be > 1 in UTF-8)
 *
 */
//--------------------------------------------------------------------------------------------------
static size_t EscapeCharacter
(
    const char* inputChar,  ///< [IN] The character we want to escape
    char* escapedChar       ///< [OUT] The escaped character
)
{
    if ((inputChar[0] > 31) && (inputChar[0] != '\"') && (inputChar[0] != '\\')) {
        // The input character don't need to be escaped
        size_t charLength = le_utf8_NumBytesInChar(inputChar[0]);
        strncpy (escapedChar, inputChar, charLength);
        return charLength;
    }

    // Write reverse solidus
    escapedChar[0] = '\\';

    switch (inputChar[0])
    {
        case '\\':
            escapedChar[1] = '\\';
            break;
        case '\"':
            escapedChar[1] = '\"';
            break;
        case '\b':
            escapedChar[1] = 'b';
            break;
        case '\f':
            escapedChar[1] = 'f';
            break;
        case '\n':
            escapedChar[1] = 'n';
            break;
        case '\r':
            escapedChar[1] = 'r';
            break;
        case '\t':
            escapedChar[1] = 't';
            break;
        default:
        {
            /* Unicode codepoint */
            int len = snprintf(&escapedChar[1], 4,"u%04x", inputChar[0]);
            LE_ASSERT(len == 4);
            break;
        }
    }

    return 1;
 }

//--------------------------------------------------------------------------------------------------
/**
 * PRELIMINARY NOTE :
 * ------------------
 * This function is similar to le_utf8_Copy. The main difference is that characters that need to
 * be escaped are escaped to produce a valid JSON file.
 *
 * A JSON string begins and ends with quotation marks.  All Unicode characters may be placed within
 * the quotation marks, except for the characters that must be escaped:
 * quotation mark, reverse solidus, and the control characters (U+0001 through U+001F).
 *
 * Source : https://tools.ietf.org/html/rfc7159#section-7
 *
 * This function copies the string in srcStr to the start of destStr and returns the number of bytes
 * copied (not including the NULL-terminator) in numBytesPtr.  Null can be passed into numBytesPtr
 * if the number of bytes copied is not needed.  The srcStr must be in UTF-8 format.
 *
 * If the size of srcStr is less than or equal to the destination buffer size then the entire srcStr
 * will be copied including the null-character.  The rest of the destination buffer is not modified.
 *
 * If the size of srcStr is larger than the destination buffer then the maximum number of characters
 * (from srcStr) plus a null-character that will fit in the destination buffer is copied.
 *
 * UTF-8 characters may be more than one byte long and this function will only copy whole characters
 * not partial characters.  Therefore, even if srcStr is larger than the destination buffer the
 * copied characters may not fill the entire destination buffer because the last character copied
 * may not align exactly with the end of the destination buffer.
 *
 * The destination string will always be Null-terminated, unless destSize is zero.
 *
 * If destStr and srcStr overlap the behaviour of this function is undefined.
 *
 * @return
 *      - LE_OK if srcStr was completely copied to the destStr.
 *      - LE_OVERFLOW if srcStr was truncated when it was copied to destStr.
 */
//--------------------------------------------------------------------------------------------------
le_result_t dataSample_StringToJson
(
    char* destStr,          ///< [IN] The destination where the srcStr is to be copied.
    const char* srcStr,     ///< [IN] The UTF-8 source string.
    const size_t destSize,  ///< [IN] Size of the destination buffer in bytes.
    size_t* numBytesPtr     ///< [OUT] The number of bytes copied not including the NULL-terminator.
                            ///        This parameter can be set to NULL if the number of bytes
                            ///        copied is not needed.
)
{
    // Check parameters.
    LE_ASSERT( (destStr != NULL) && (srcStr != NULL) && (destSize > 0) );

    // Go through the string copying one character at a time.
    size_t i = 0;
    size_t j = 0;
    size_t charLength = 0;
    size_t escapedCharLength = 0;
    while (1)
    {
        if (srcStr[i] == '\0')
        {
            // NULL character found.  Complete the copy and return.
            destStr[j] = '\0';

            if (numBytesPtr)
            {
                *numBytesPtr = j;
            }

            return LE_OK;
        }
        else
        {
            // Normal character, copy.

            // First check if we have enough room to store the escaped version of the character.
            escapedCharLength = ComputeEscapedCharLength(srcStr[i]);

            if (escapedCharLength == 0)
            {
                // This is an error in the string format. Zero out the destStr and return.
                destStr[0] = '\0';

                if (numBytesPtr)
                {
                    *numBytesPtr = 0;
                }

                return LE_OK;
            }

            if (escapedCharLength + j >= destSize)
            {
                // It will not fit in the available space, so stop.
                destStr[j] = '\0';

                if (numBytesPtr)
                {
                    *numBytesPtr = j;
                }

                return LE_OVERFLOW;
            }

            // We have enough room, insert the escaped character.
            charLength = EscapeCharacter(&srcStr[i], &destStr[j]);
            i += charLength;
            j += escapedCharLength;
        }
    }
}

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
    NonStringSamplePool = le_mem_CreatePool("Data Sample", sizeof(DataSample_t));

    SmallStringSamplePool = le_mem_CreatePool("Small String Sample",
                                              SMALL_STRING_SAMPLE_OBJECT_BYTES);

    HugeStringSamplePool = le_mem_CreatePool("Huge String Sample",
                                             HUGE_STRING_SAMPLE_OBJECT_BYTES);
}


//--------------------------------------------------------------------------------------------------
/**
 * @return True if a given string is shorter than SMALL_STRING_BYTES, including its null terminator.
 */
//--------------------------------------------------------------------------------------------------
static inline bool IsSmallString
(
    const char* string
)
//--------------------------------------------------------------------------------------------------
{
    return (strnlen(string, SMALL_STRING_BYTES) < SMALL_STRING_BYTES);
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
    DataSample_t* samplePtr = le_mem_ForceAlloc(pool);

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
    DataSample_t* samplePtr = CreateSample(NonStringSamplePool, timestamp);

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
    DataSample_t* samplePtr = CreateSample(NonStringSamplePool, timestamp);
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
    DataSample_t* samplePtr = CreateSample(NonStringSamplePool, timestamp);
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
    le_mem_PoolRef_t pool;
    size_t maxSize;
    if (IsSmallString(value))
    {
        pool = SmallStringSamplePool;
        maxSize = SMALL_STRING_BYTES;
    }
    else
    {
        pool = HugeStringSamplePool;
        maxSize = HUB_MAX_STRING_BYTES;
    }

    DataSample_t* samplePtr = CreateSample(pool, timestamp);

    if (LE_OK != le_utf8_Copy(samplePtr->value.string, value, maxSize, NULL))
    {
        LE_FATAL("String value longer than max permitted size of %d", maxSize);
    }

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
    return sampleRef->value.string;
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
    return sampleRef->value.string;
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
const le_result_t dataSample_ConvertToString
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
        return le_utf8_Copy(valueBuffPtr, sampleRef->value.string, valueBuffSize, NULL);
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
const le_result_t dataSample_ConvertToJson
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

            if (i >= valueBuffSize)
            {
                return LE_OVERFLOW;
            }
            return LE_OK;
        }

        case IO_DATA_TYPE_NUMERIC:

            if (valueBuffSize <= snprintf(valueBuffPtr,
                                          valueBuffSize,
                                          "%lf",
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
            result = dataSample_StringToJson(valueBuffPtr, sampleRef->value.string, valueBuffSize, &len);
            if ((result != LE_OK) || (len >= (valueBuffSize - 1)))  // need 1 more for the last '"'
            {
                return LE_OVERFLOW;
            }
            valueBuffPtr[len] = '"'; // replace null-terminator with '"'
            valueBuffPtr[len + 1] = '\0'; // null-terminate the string.
            return LE_OK;

        case IO_DATA_TYPE_JSON:

            // Already in JSON format, just copy it into the buffer.
            return le_utf8_Copy(valueBuffPtr, sampleRef->value.string, valueBuffSize, NULL);
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
    char resultBuff[IO_MAX_STRING_VALUE_LEN + 1];
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
    le_mem_PoolRef_t pool;

    if ((dataType == IO_DATA_TYPE_STRING) || (dataType == IO_DATA_TYPE_JSON))
    {
        if (IsSmallString(original->value.string))
        {
            pool = SmallStringSamplePool;
        }
        else
        {
            pool = HugeStringSamplePool;
        }
    }
    else
    {
        pool = NonStringSamplePool;
    }

    dataSample_Ref_t duplicate = le_mem_ForceAlloc(pool);

    memcpy(duplicate, original, le_mem_GetObjectSize(pool));

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
