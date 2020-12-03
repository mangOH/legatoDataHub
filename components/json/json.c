//--------------------------------------------------------------------------------------------------
/**
 * @file json.c
 *
 * Implementation of JSON parsing functionality.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "json.h"


//--------------------------------------------------------------------------------------------------
/**
 * Skip over whitespace, if any.
 *
 * @return Pointer to the first non-whitespace character in the string, or NULL iff valPtr is NULL.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipWhitespace
(
    const char* valPtr  ///< JSON string, or NULL.
)
//--------------------------------------------------------------------------------------------------
{
    if (valPtr != NULL)
    {
        while (isspace(*valPtr))
        {
            valPtr++;
        }
    }

    return valPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip over a string.
 *
 * @return Pointer to the first character after the string, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipString
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '"')
    {
        return NULL;
    }

    valPtr++;

    while (*valPtr != '"')
    {
        if (*valPtr == '\0')
        {
            return NULL;
        }

        // Skip an escaped character (making sure not to skip past the null terminator).
        // Note that JSON only allows a few things to be escaped, but we are being tolerant
        // of invalid escape sequences here in the interests of runtime performance.
        if (valPtr[0] == '\\')
        {
            if (valPtr[1] == '\0')
            {
                return NULL;
            }
            valPtr += 2;
        }
        else
        {
            valPtr += 1;
        }
    }

    return valPtr + 1;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip a literal value, such as (e.g., true, false, or null).
 *
 * @return Pointer to the first character after the value, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipLiteral
(
    const char* valPtr,
    const char* textToSkip,
    size_t lenToSkip
)
//--------------------------------------------------------------------------------------------------
{
    if (strncmp(valPtr, textToSkip, lenToSkip) == 0)
    {
        valPtr += lenToSkip;

        // The literal must be followed by space, a comma, a closing bracket or end of the string.
        if (isspace(*valPtr))
        {
            return valPtr;
        }
        else if (   (*valPtr == ',')
                 || (*valPtr == ']')
                 || (*valPtr == '}')
                 || (*valPtr == '\0')  )
        {
            return valPtr;
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Parse a number value from a JSON string.
 *
 * @return LE_OK if successful.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ParseNumber
(
    double* numberPtr,
    const char* string,
    const char* legalEndChars ///< String containing characters allowed to appear after the number.
                              ///< '\0' is always a legal end character.
)
//--------------------------------------------------------------------------------------------------
{
    errno = 0;
    char* endPtr;
    double number = strtod(string, &endPtr);
    if ((endPtr != string) && (errno == 0))
    {
        // Check that one of the permitted end characters is next.
        for (;;)
        {
            if (*legalEndChars == *endPtr)
            {
                *numberPtr = number;
                return LE_OK;
            }

            if (*legalEndChars == '\0')
            {
                break;
            }

            legalEndChars++;
        }
    }

    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip a number.
 *
 * @return Pointer to the first character after the number, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipNumber
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    char* endPtr = NULL;

    (void)strtod(valPtr, &endPtr);

    if (endPtr != valPtr)
    {
        // The number must be followed by space, a comma, a closing bracket or end of the string.
        if (   isspace(*endPtr)
            || (*endPtr == ',')
            || (*endPtr == ']')
            || (*endPtr == '}')
            || (*endPtr == '\0')  )
        {
            return endPtr;
        }
    }

    return NULL;
}


static const char* SkipObject(const char* valPtr);
static const char* SkipArray(const char* valPtr);


//--------------------------------------------------------------------------------------------------
/**
 * Skip any kind of JSON value.
 *
 * @return Pointer to the first character after the value, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipValue
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    switch (*valPtr)
    {
        case '{':

            return SkipObject(valPtr);

        case '[':

            return SkipArray(valPtr);

        case '"':

            return SkipString(valPtr);

        case 't':

            return SkipLiteral(valPtr, "true", 4);

        case 'f':

            return SkipLiteral(valPtr, "false", 5);

        case 'n':

            return SkipLiteral(valPtr, "null", 4);

        default:

            return SkipNumber(valPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip an object member.
 *
 * @return Pointer to the first character after the member, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipMember
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    valPtr = SkipString(valPtr);

    if (valPtr != NULL)
    {
        valPtr = SkipWhitespace(valPtr);

        if (*valPtr != ':')
        {
            return NULL;
        }

        valPtr = SkipValue(SkipWhitespace(valPtr + 1));
    }

    return valPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip an object.
 *
 * @return Pointer to the first character after the object, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipObject
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '{')
    {
        return NULL;
    }

    valPtr = SkipWhitespace(valPtr + 1);

    if (*valPtr == '}')
    {
        return valPtr + 1;
    }

    while (*valPtr != '\0')
    {
        valPtr = SkipWhitespace(SkipMember(valPtr));

        if (valPtr == NULL)
        {
            return NULL;
        }

        if (*valPtr == '}')
        {
            return valPtr + 1;
        }

        if (*valPtr == ',')
        {
            valPtr = SkipWhitespace(valPtr + 1);
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip an array.
 *
 * @return Pointer to the first non-whitespace character after the array, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipArray
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '[')
    {
        return NULL;
    }

    valPtr = SkipWhitespace(valPtr + 1);

    if (*valPtr == ']')
    {
        return valPtr + 1;
    }

    while (*valPtr != '\0')
    {
        valPtr = SkipWhitespace(SkipValue(valPtr));

        if (valPtr == NULL)
        {
            return NULL;
        }

        if (*valPtr == ']')
        {
            return valPtr + 1;
        }

        if (*valPtr == ',')
        {
            valPtr = SkipWhitespace(valPtr + 1);
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Find the array element at a given index in the JSON array.
 *
 * @return
 *  - LE_OK if successful,
 *  - LE_FORMAT_ERROR if the original JSON input string is malformed,
 *  - LE_NOT_FOUND if the thing specified in the extraction spec is not found in the JSON input.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GoToElement
(
    const char* valPtr,
    long int index,
    const char** resultPtrPtr ///< [OUT] Ptr to where to put ptr to start of element if LE_OK returned.
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '[')
    {
        return LE_FORMAT_ERROR;
    }

    valPtr++;   // Skip '['
    valPtr = SkipWhitespace(valPtr);

    // Until we find the ith entry, skip values and the commas after them.
    for (size_t i = 0; i != index; i++)
    {
        if (*valPtr == ']')
        {
            // The element doesn't exist in the array.
            return LE_NOT_FOUND;
        }

        valPtr = SkipWhitespace(SkipValue(valPtr));

        if ((valPtr == NULL) || (*valPtr != ','))
        {
            return LE_FORMAT_ERROR;
        }

        valPtr++;   // Skip ','
        valPtr = SkipWhitespace(valPtr);
    }

    *resultPtrPtr = valPtr;

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Find the object member with a given name in the JSON object.
 *
 * @return
 *  - LE_OK if successful,
 *  - LE_FORMAT_ERROR if the original JSON input string is malformed,
 *  - LE_NOT_FOUND if the thing specified in the extraction spec is not found in the JSON input.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GoToMember
(
    const char* valPtr,
    const char* memberName,
    const char** resultPtrPtr ///< [OUT] Ptr to where to put ptr to start of member if LE_OK rtrned.
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '{')
    {
        return LE_FORMAT_ERROR;
    }

    size_t nameLen = strlen(memberName);

    valPtr++;   // Skip '{'
    valPtr = SkipWhitespace(valPtr);

    // If we find something other than a '"' (or '}') beginning a member name,
    // then the JSON is malformed.
    while (*valPtr == '"')
    {
        // If the member name matches,
        if ((strncmp(valPtr + 1, memberName, nameLen) == 0) && (valPtr[nameLen + 1] == '"'))
        {
            valPtr = SkipWhitespace(valPtr + nameLen + 2);
            if (*valPtr != ':')
            {
                LE_ERROR("Missing colon after JSON object member name '%s'.", memberName);
                return LE_FORMAT_ERROR;
            }

            *resultPtrPtr = SkipWhitespace(valPtr + 1);
            return LE_OK;
        }

        // The member name doesn't match, so skip over this member.
        valPtr = SkipWhitespace(SkipMember(valPtr));

        // Since we haven't found the member we are looking for yet, we hope to find a comma next,
        // meaning there will be more members to follow.
        if ((valPtr == NULL) || (*valPtr != ','))
        {
            return LE_FORMAT_ERROR;
        }

        valPtr++;   // Skip ','
        valPtr = SkipWhitespace(valPtr);
    }

    if (*valPtr == '}')
    {
        return LE_NOT_FOUND;
    }

    return LE_FORMAT_ERROR;
}


//--------------------------------------------------------------------------------------------------
/**
 * Extract a member name from an extraction specifier string.
 *
 * @return Pointer to the location in the extraction specifier string immediately following the
 *         extracted member name, or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
static const char* GetMemberName
(
    const char* specPtr,    ///< The extraction specifier.
    char* buffPtr,  ///< Ptr to buffer where the name will be copied.
    size_t buffSize ///< Size of buffer pointed to by buffPtr.
)
//--------------------------------------------------------------------------------------------------
{
    size_t i = 0;

    while ((i < buffSize) && isalnum(*specPtr))
    {
        *buffPtr = *specPtr;

        buffPtr++;
        specPtr++;
        i++;
    }

    if (i >= buffSize)
    {
        return NULL;   // Overflow.
    }

    *buffPtr = '\0';

    return specPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Find an object member or array element in a JSON data value, based on a given
 * extraction specifier.
 *
 * The extraction specifiers look like "x" or "x.y" or "[3]" or "x[3].y", etc.
 *
 * @return
 *  - LE_OK if successful,
 *  - LE_FORMAT_ERROR if the original JSON input string is malformed,
 *  - LE_BAD_PARAMETER if the extraction spec is invalid,
 *  - LE_NOT_FOUND if the thing specified in the extraction spec is not found in the JSON input.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t Find
(
    const char* original,       ///< [IN] Original JSON string to extract from.
    const char* extractionSpec, ///< [IN] Extraction specification.
    const char** resultPtrPtr   ///< [OUT] Ptr to where the ptr to the value should go if LE_OK.
)
//--------------------------------------------------------------------------------------------------
{
    const char* specPtr = extractionSpec;
    const char* valPtr = original;

    while (valPtr && (*valPtr != '\0'))
    {
        switch (*specPtr)
        {
            case '\0':

                *resultPtrPtr = valPtr;
                return LE_OK;

            case '[':
            {
                char* endPtr;
                long int index = strtoul(specPtr + 1, &endPtr, 10);
                if ((index < 0) || (endPtr == (specPtr + 1)) || (*endPtr != ']'))
                {
                    goto badSpec;
                }
                specPtr = endPtr + 1;

                le_result_t r = GoToElement(valPtr, index, &valPtr);
                if (r != LE_OK)
                {
                    return r;
                }

                break;
            }

            case '.':

                specPtr++;

                // *** FALL THROUGH ***

            default:
            {
                if (!isalpha(*specPtr))
                {
                    goto badSpec;
                }

                char memberName[128];   // Arbitrary number bigger than any member name should be.
                specPtr = GetMemberName(specPtr, memberName, sizeof(memberName));
                if (specPtr == NULL)
                {
                    goto badSpec;
                }

                le_result_t r = GoToMember(valPtr, memberName, &valPtr);
                if (r != LE_OK)
                {
                    return r;
                }

                break;
            }
        }
    }

    LE_DEBUG("'%s' not found in JSON value '%s'.", extractionSpec, original);
    return LE_NOT_FOUND;

badSpec:

    LE_ERROR("Invalid JSON extraction spec '%s'.", extractionSpec);
    return LE_BAD_PARAMETER;
}


//--------------------------------------------------------------------------------------------------
/**
 * Extract an object member or array element from a JSON data value, based on a given
 * extraction specifier.
 *
 * The extraction specifiers look like "x" or "x.y" or "[3]" or "x[3].y", etc.
 *
 * @return
 *  - LE_OK if successful
 *  - LE_FORMAT_ERROR if there's something wrong with the input JSON string.
 *  - LE_BAD_PARAMETER if there's something wrong with the extraction specification.
 *  - LE_NOT_FOUND if the thing we are trying to extract doesn't exist in the JSON input.
 *  - LE_OVERFLOW if the provided result buffer isn't big enough.
 */
//--------------------------------------------------------------------------------------------------
le_result_t json_Extract
(
    char* resultBuffPtr,    ///< [OUT] Ptr to where to put the extracted JSON.
    size_t resultBuffSize,  ///< [IN] Size of the result buffer, in bytes, including space for null.
    const char* jsonValue,   ///< [IN] Original JSON string to extract from.
    const char* extractionSpec, ///< [IN] the extraction specification.
    json_DataType_t* dataTypePtr  ///< [OUT] Ptr to where to put the data type of extracted JSON
)
//--------------------------------------------------------------------------------------------------
{
    const char* valPtr;

    le_result_t result = Find(jsonValue, extractionSpec, &valPtr);

    if (result != LE_OK)
    {
        return result;
    }

    const char* endPtr = NULL;

    switch (*valPtr)
    {
        case '{':

            endPtr = SkipObject(valPtr);
            if (endPtr != NULL)
            {
                size_t objSize = (endPtr - valPtr);
                if (objSize >= resultBuffSize)
                {
                    return LE_OVERFLOW;
                }
                strncpy(resultBuffPtr, valPtr, objSize);
                resultBuffPtr[objSize] = '\0';
                if (dataTypePtr)
                {
                    *dataTypePtr = JSON_TYPE_OBJECT;
                }
                return LE_OK;
            }
            break;

        case '[':

            endPtr = SkipArray(valPtr);
            if (endPtr != NULL)
            {
                size_t objSize = (endPtr - valPtr);
                if (objSize >= resultBuffSize)
                {
                    return LE_OVERFLOW;
                }
                strncpy(resultBuffPtr, valPtr, objSize);
                resultBuffPtr[objSize] = '\0';
                if (dataTypePtr)
                {
                    *dataTypePtr = JSON_TYPE_ARRAY;
                }
                return LE_OK;
            }
            break;

        case '"':

            endPtr = SkipString(valPtr);
            if (endPtr != NULL)
            {
                // Move inside the quotes.
                valPtr++;
                endPtr--;

                size_t stringLen = (endPtr - valPtr);
                if (stringLen >= resultBuffSize)
                {
                    return LE_OVERFLOW;
                }
                strncpy(resultBuffPtr, valPtr, stringLen);
                resultBuffPtr[stringLen] = '\0';
                if (dataTypePtr)
                {
                    *dataTypePtr = JSON_TYPE_STRING;
                }
                return LE_OK;
            }
            break;

        case 't':

            if (strncmp(valPtr, "true", 4) == 0)
            {
                *dataTypePtr = JSON_TYPE_BOOLEAN;
                return le_utf8_Copy(resultBuffPtr, "true", resultBuffSize, NULL);
            }
            break;

        case 'f':

            if (strncmp(valPtr, "false", 5) == 0)
            {
                *dataTypePtr = JSON_TYPE_BOOLEAN;
                return le_utf8_Copy(resultBuffPtr, "false", resultBuffSize, NULL);
            }
            break;

        case 'n':

            if (strncmp(valPtr, "null", 4) == 0)
            {
                *dataTypePtr = JSON_TYPE_NULL;
                return le_utf8_Copy(resultBuffPtr, "null", resultBuffSize, NULL);
            }
            break;

        default:
        {
            const char* endPtr = SkipNumber(valPtr);
            if (endPtr != NULL)
            {
                size_t objSize = (endPtr - valPtr);
                if (objSize >= resultBuffSize)
                {
                    return LE_OVERFLOW;
                }
                strncpy(resultBuffPtr, valPtr, objSize);
                resultBuffPtr[objSize] = '\0';
                if (dataTypePtr)
                {
                    *dataTypePtr = JSON_TYPE_NUMBER;
                }
                return LE_OK;
            }
            break;
        }
    }

    LE_ERROR("Invalid content in JSON string '%s' beginning at byte %zu.",
             jsonValue,
             valPtr - jsonValue);

    return LE_FORMAT_ERROR;
}


//--------------------------------------------------------------------------------------------------
/**
 * Convert a JSON value into a Boolean value.
 *
 * @return The Boolean value.
 */
//--------------------------------------------------------------------------------------------------
bool json_ConvertToBoolean
(
    const char* jsonValue
)
//--------------------------------------------------------------------------------------------------
{
    // If the JSON is a Boolean value, use that.
    if (strcmp(jsonValue, "true") == 0)
    {
        return true;
    }
    if (strcmp(jsonValue, "false") == 0)
    {
        return false;
    }

    // See if it's a number, in which case we interpret zero as false and non-zero as true.
    double number;
    if (ParseNumber(&number, jsonValue, "") == LE_OK)
    {
        return ((number != 0) && (!isnan(number)));
    }

    // If not a Boolean or a number, use empty string as false and non-empty
    // string as true.
    return (jsonValue[0] != '\0');
}


//--------------------------------------------------------------------------------------------------
/**
 * Convert a JSON value into a numeric value.
 *
 * @return The numeric value.
 */
//--------------------------------------------------------------------------------------------------
double json_ConvertToNumber
(
    const char* jsonValue
)
//--------------------------------------------------------------------------------------------------
{
    // If the JSON is a Boolean value, use true = 1, false = 0.
    if (strcmp(jsonValue, "true") == 0)
    {
        return 1;
    }
    if (strcmp(jsonValue, "false") == 0)
    {
        return 0;
    }

    // See if it's a number.
    double number;
    if (ParseNumber(&number, jsonValue, "") == LE_OK)
    {
        return number;
    }

    // If not a Boolean or a number, return NaN.
    return NAN;
}


//--------------------------------------------------------------------------------------------------
/**
 * Validate a JSON string.
 *
 * @return true if the string is valid JSON.  false if not.
 */
//--------------------------------------------------------------------------------------------------
bool json_IsValid
(
    const char* jsonValue
)
//--------------------------------------------------------------------------------------------------
{
    const char* endPtr = SkipWhitespace(SkipValue(SkipWhitespace(jsonValue)));

    if ((endPtr == NULL) || (*endPtr != '\0'))
    {
        return false;
    }

    return true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string containing the name of a given data type.
 *
 * @return Pointer to the null-terminated string.
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED const char* json_GetDataTypeName
(
    json_DataType_t dataType
)
//--------------------------------------------------------------------------------------------------
{
    switch (dataType)
    {
        case JSON_TYPE_NULL:    return "null";
        case JSON_TYPE_BOOLEAN: return "Boolean";
        case JSON_TYPE_NUMBER:  return "number";
        case JSON_TYPE_STRING:  return "string";
        case JSON_TYPE_OBJECT:  return "object";
        case JSON_TYPE_ARRAY:   return "array";
    }

    LE_CRIT("Invalid data type code '%d'.", dataType);

    return "unknown";
}


COMPONENT_INIT
{
}

