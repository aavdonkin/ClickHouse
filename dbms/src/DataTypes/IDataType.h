#pragma once

#include <memory>

#include <Core/Field.h>


namespace DB
{

class ReadBuffer;
class WriteBuffer;

class IDataType;
struct FormatSettingsJSON;

class IColumn;
using ColumnPtr = std::shared_ptr<IColumn>;

using DataTypePtr = std::shared_ptr<IDataType>;
using DataTypes = std::vector<DataTypePtr>;


/** Properties of data type.
  * Contains methods for serialization/deserialization.
  * Implementations of this interface represent a data type (example: UInt8)
  *  or parapetric family of data types (example: Array(...)).
  */
class IDataType
{
public:
    /// Compile time flag. If false, then if C++ types are the same, then SQL types are also the same.
    /// Example: DataTypeString is not parametric: thus all instances of DataTypeString are the same SQL type.
    /// Example: DataTypeFixedString is parametric: different instances of DataTypeFixedString may be different SQL types.
    /// Place it in descendants:
    /// static constexpr bool is_parametric = false;

    /// Name of data type (examples: UInt64, Array(String)).
    virtual String getName() const { return getFamilyName(); };

    /// Name of data type family (example: FixedString, Array).
    virtual const char * getFamilyName() const = 0;

    /// Is this type nullable?
    virtual bool isNullable() const { return false; }

    /// Is this type can represent only NULL value? (It also implies isNullable)
    virtual bool isNull() const { return false; }

    /// Is this type numeric? Date and DateTime types are considered as such.
    virtual bool isNumeric() const { return false; }

    /// Is this type numeric and not nullable?
    virtual bool isNumericNotNullable() const { return isNumeric(); }

    /// If this type is numeric, are all the arithmetic operations and type casting
    /// relevant for it? True for numbers. False for Date and DateTime types.
    virtual bool behavesAsNumber() const { return false; }

    /// If this data type cannot appear in table declaration - only for intermediate values of calculations.
    virtual bool notForTables() const { return false; }

    /// If this data type cannot be wrapped in Nullable data type.
    virtual bool canBeInsideNullable() const { return true; }

    virtual DataTypePtr clone() const = 0;

    /** Binary serialization for range of values in column - for writing to disk/network, etc.
      *
      * Some data types are represented in multiple streams while being serialized.
      * Example:
      * - Arrays are represented as stream of all elements and stream of array sizes.
      * - Nullable types are represented as stream of values (with unspecified values in place of NULLs) and stream of NULL flags.
      *
      * Different streams are identified by "path".
      * If the data type require single stream (it's true for most of data types), the stream will have empty path.
      * Otherwise, the path can have components like "array elements", "array sizes", etc.
      *
      * For multidimensional arrays, path can have arbiraty length.
      * As an example, for 2-dimensional arrays of numbers we have at least three streams:
      * - array sizes;                      (sizes of top level arrays)
      * - array elements / array sizes;     (sizes of second level (nested) arrays)
      * - array elements / array elements;  (the most deep elements, placed contiguously)
      *
      * Descendants must override either serializeBinaryBulk, deserializeBinaryBulk methods (for simple cases with single stream)
      *  or serializeBinaryBulkWithMultipleStreams, deserializeBinaryBulkWithMultipleStreams, enumerateStreams methods (for cases with multiple streams).
      *
      * Default implementations of ...WithMultipleStreams methods will call serializeBinaryBulk, deserializeBinaryBulk for single stream.
      */

    struct Substream
    {
        enum Type
        {
            ArrayElements,
            ArraySizes,

            NullableElements,
            NullMap,

            TupleElement,
        };
        Type type;

        /// Index of tuple element, starting at 1.
        size_t tuple_element = 0;

        Substream(Type type) : type(type) {}
    };

    using SubstreamPath = std::vector<Substream>;

    using StreamCallback = std::function<void(const SubstreamPath &)>;
    virtual void enumerateStreams(StreamCallback callback, SubstreamPath path) const
    {
        callback(path);
    }

    using OutputStreamGetter = std::function<WriteBuffer*(const SubstreamPath &)>;
    using InputStreamGetter = std::function<ReadBuffer*(const SubstreamPath &)>;

    /** 'offset' and 'limit' are used to specify range.
      * limit = 0 - means no limit.
      * offset must be not greater than size of column.
      * offset + limit could be greater than size of column
      *  - in that case, column is serialized till the end.
      */
    virtual void serializeBinaryBulkWithMultipleStreams(
        const IColumn & column,
        OutputStreamGetter getter,
        size_t offset,
        size_t limit,
        bool /*position_independent_encoding*/,
        SubstreamPath path) const
    {
        if (WriteBuffer * stream = getter(path))
            serializeBinaryBulk(column, *stream, offset, limit);
    }

    /** Read no more than limit values and append them into column.
      * avg_value_size_hint - if not zero, may be used to avoid reallocations while reading column of String type.
      */
    virtual void deserializeBinaryBulkWithMultipleStreams(
        IColumn & column,
        InputStreamGetter getter,
        size_t limit,
        double avg_value_size_hint,
        bool /*position_independent_encoding*/,
        SubstreamPath path) const
    {
        if (ReadBuffer * stream = getter(path))
            deserializeBinaryBulk(column, *stream, limit, avg_value_size_hint);
    }

    /** Override these methods for data types that require just single stream (most of data types).
      */
    virtual void serializeBinaryBulk(const IColumn & column, WriteBuffer & ostr, size_t offset, size_t limit) const;
    virtual void deserializeBinaryBulk(IColumn & column, ReadBuffer & istr, size_t limit, double avg_value_size_hint) const;

    /** Serialization/deserialization of individual values.
      *
      * These are helper methods for implementation of various formats to input/output for user (like CSV, JSON, etc.).
      * There is no one-to-one correspondence between formats and these methods.
      * For example, TabSeparated and Pretty formats could use same helper method serializeTextEscaped.
      *
      * For complex data types (like arrays) binary serde for individual values may differ from bulk serde.
      * For example, if you serialize single array, it will be represented as its size and elements in single contiguous stream,
      *  but if you bulk serialize column with arrays, then sizes and elements will be written to separate streams.
      */

    /// There is two variants for binary serde. First variant work with Field.
    virtual void serializeBinary(const Field & field, WriteBuffer & ostr) const = 0;
    virtual void deserializeBinary(Field & field, ReadBuffer & istr) const = 0;

    /// Other variants takes a column, to avoid creating temporary Field object.
    /// Column must be non-constant.

    /// Serialize one value of a column at specified row number.
    virtual void serializeBinary(const IColumn & column, size_t row_num, WriteBuffer & ostr) const = 0;
    /// Deserialize one value and insert into a column.
    /// If method will throw an exception, then column will be in same state as before call to method.
    virtual void deserializeBinary(IColumn & column, ReadBuffer & istr) const = 0;

    /** Text serialization with escaping but without quoting.
      */
    virtual void serializeTextEscaped(const IColumn & column, size_t row_num, WriteBuffer & ostr) const = 0;

    virtual void deserializeTextEscaped(IColumn & column, ReadBuffer & istr) const = 0;

    /** Text serialization as a literal that may be inserted into a query.
      */
    virtual void serializeTextQuoted(const IColumn & column, size_t row_num, WriteBuffer & ostr) const = 0;

    virtual void deserializeTextQuoted(IColumn & column, ReadBuffer & istr) const = 0;

    /** Text serialization for the CSV format.
      */
    virtual void serializeTextCSV(const IColumn & column, size_t row_num, WriteBuffer & ostr) const = 0;

    /** delimiter - the delimiter we expect when reading a string value that is not double-quoted
      * (the delimiter is not consumed).
      */
    virtual void deserializeTextCSV(IColumn & column, ReadBuffer & istr, const char delimiter) const = 0;

    /** Text serialization for displaying on a terminal or saving into a text file, and the like.
      * Without escaping or quoting.
      */
    virtual void serializeText(const IColumn & column, size_t row_num, WriteBuffer & ostr) const = 0;

    /** Text serialization intended for using in JSON format.
      * force_quoting_64bit_integers parameter forces to brace UInt64 and Int64 types into quotes.
      */
    virtual void serializeTextJSON(const IColumn & column, size_t row_num, WriteBuffer & ostr, const FormatSettingsJSON & settings) const = 0;
    virtual void deserializeTextJSON(IColumn & column, ReadBuffer & istr) const = 0;

    /** Text serialization for putting into the XML format.
      */
    virtual void serializeTextXML(const IColumn & column, size_t row_num, WriteBuffer & ostr) const
    {
        serializeText(column, row_num, ostr);
    }

    /** Create empty (non-constant) column for corresponding type.
      */
    virtual ColumnPtr createColumn() const = 0;

    /** Create constant column for corresponding type, with specified size and value.
      */
    virtual ColumnPtr createConstColumn(size_t size, const Field & field) const;

    /** Get default value of data type.
      * It is the "default" default, regardless the fact that a table could contain different user-specified default.
      */
    virtual Field getDefault() const = 0;

    /** Directly insert default value into a column. Default implementation use method IColumn::insertDefault.
      * This should be overriden if data type default value differs from column default value (example: Enum data types).
      */
    virtual void insertDefaultInto(IColumn & column) const;

    /// For fixed-size types, return size of value in bytes. For other data types, return some approximate size just for estimation.
    virtual size_t getSizeOfField() const
    {
        throw Exception("getSizeOfField() method is not implemented for data type " + getName(), ErrorCodes::NOT_IMPLEMENTED);
    }

    /// Checks that two instances belong to the same type
    inline bool equals(const IDataType & rhs) const
    {
        return getName() == rhs.getName();
    }

    virtual ~IDataType() {}

    /// Updates avg_value_size_hint for newly read column. Uses to optimize deserialization. Zero expected for first column.
    static void updateAvgValueSizeHint(const IColumn & column, double & avg_value_size_hint);

    static String getFileNameForStream(const String & column_name, const SubstreamPath & path);
};


}

