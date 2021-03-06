#include <TableFunctions/ITableFunction.h>
#include <TableFunctions/TableFunctionMySQL.h>
#include <TableFunctions/TableFunctionFactory.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTLiteral.h>
#include <Common/Exception.h>
#include <Common/typeid_cast.h>
#include <Interpreters/evaluateConstantExpression.h>
#include <Storages/StorageMySQL.h>
#include <Dictionaries/MySQLBlockInputStream.h>
#include <DataTypes/DataTypeFactory.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeFixedString.h>

#include <mysqlxx/Pool.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
}

void insertColumn(Block & sample_block, const char * name)
{
    ColumnWithTypeAndName col;
    col.name = name;
    col.type = std::make_shared<DataTypeString>();
    col.column = col.type->createColumn();
    sample_block.insert(std::move(col));
}

DataTypePtr getDataType(const char * mysql_type)
{
    int len = strlen(mysql_type);
    bool un_signed = (len >= 8) && (memcmp(mysql_type + len - 8, "unsigned", 8) == 0);
    if (strncmp(mysql_type, "tinyint", 7) == 0)
        return DataTypeFactory::instance().get(un_signed ? "UInt8" : "Int8");
    if (strncmp(mysql_type, "smallint", 8) == 0)
        return DataTypeFactory::instance().get(un_signed ? "UInt16" : "Int16");
    if ((strncmp(mysql_type, "mediumint", 9) == 0) || (strncmp(mysql_type, "int", 3) == 0))
        return DataTypeFactory::instance().get(un_signed ? "UInt32" : "Int32");
    if (strncmp(mysql_type, "bigint", 6) == 0)
        return DataTypeFactory::instance().get(un_signed ? "UInt64" : "Int64");
    if (strncmp(mysql_type, "float", 5) == 0)
        return DataTypeFactory::instance().get("Float32");
    if (strncmp(mysql_type, "double", 6) == 0)
        return DataTypeFactory::instance().get("Float64");
    if (strcmp(mysql_type, "date") == 0)
        return DataTypeFactory::instance().get("Date");
    if (strcmp(mysql_type, "datetime") == 0)
        return DataTypeFactory::instance().get("DateTime");
    if (strncmp(mysql_type, "binary(", 7) == 0)
    {
        size_t size = 1;
        sscanf(mysql_type + 7, "%li", &size);
        return std::shared_ptr<IDataType>(new DataTypeFixedString(size));
    }
    return DataTypeFactory::instance().get("String");
}

StoragePtr TableFunctionMySQL::execute(const ASTPtr & ast_function, const Context & context) const
{
    ASTs & args_func = typeid_cast<ASTFunction &>(*ast_function).children;

    if (args_func.size() != 1)
        throw Exception("Table function 'mysql' requires exactly one argument: amount of numbers.",
            ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

    ASTs & args = typeid_cast<ASTExpressionList &>(*args_func.at(0)).children;

    if (args.size() != 5)
        throw Exception("Table function 'mysql' requires exactly 5 arguments: host:port, database name, table name, user name and password",
            ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

    for (int i = 0; i < 5; ++i)
        args[i] = evaluateConstantExpressionOrIdentifierAsLiteral(args[i], context);

    int port;
    std::string host_port = static_cast<const ASTLiteral &>(*args[0]).value.safeGet<String>();
    std::string server = splitHostPort(host_port.c_str(), port);
    std::string database_name = static_cast<const ASTLiteral &>(*args[1]).value.safeGet<String>();
    std::string user_name = static_cast<const ASTLiteral &>(*args[3]).value.safeGet<String>();
    std::string password = static_cast<const ASTLiteral &>(*args[4]).value.safeGet<String>();
    mysqlxx::Pool pool(database_name, server, user_name, password, port);
    Block sample_block;
    insertColumn(sample_block, "Field");
    insertColumn(sample_block, "Type");
    insertColumn(sample_block, "Null");
    insertColumn(sample_block, "Key");
    insertColumn(sample_block, "Default");
    insertColumn(sample_block, "Extra");
    std::string table_name = static_cast<const ASTLiteral &>(*args[2]).value.safeGet<String>();
    MySQLBlockInputStream result(pool.Get(), std::string("DESCRIBE ") + table_name, sample_block, 1 << 16);
    Block resultBlock = result.read();
    const IColumn & names = *resultBlock.getByPosition(0).column.get();
    const IColumn & types = *resultBlock.getByPosition(1).column.get();
    size_t field_count = names.size();
    NamesAndTypesListPtr columns = std::make_shared<NamesAndTypesList>();
    NamesAndTypesList materialized_columns;
    NamesAndTypesList alias_columns;
    ColumnDefaults column_defaults;
    for (size_t i = 0; i < field_count; ++i)
    {
        columns->push_back(NameAndTypePair(names.getDataAt(i).data, getDataType(types.getDataAt(i).data)));
    }
    auto res = StorageMySQL::create(table_name, host_port, database_name, table_name, user_name, password, columns, materialized_columns, alias_columns, column_defaults, context);
    res->startup();
    return res;
}


void registerTableFunctionMySQL(TableFunctionFactory & factory)
{
    TableFunctionFactory::instance().registerFunction<TableFunctionMySQL>();
}

}
