/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <comphelper/string.hxx>
#include <sal/log.hxx>
#include "createparser.hxx"
#include "utils.hxx"
#include <com/sun/star/sdbc/DataType.hpp>

using namespace ::comphelper;
using namespace css::sdbc;

namespace
{
//Find ascii escaped unicode
sal_Int32 lcl_IndexOfUnicode(const OString& rSource, const sal_Int32 nFrom = 0)
{
    const OString sHexDigits = "0123456789abcdefABCDEF";
    sal_Int32 nIndex = rSource.indexOf("\\u", nFrom);
    if (nIndex == -1)
    {
        return -1;
    }
    bool bIsUnicode = true;
    for (short nDist = 2; nDist <= 5; ++nDist)
    {
        if (sHexDigits.indexOf(rSource[nIndex + nDist]) == -1)
        {
            bIsUnicode = false;
        }
    }
    return bIsUnicode ? nIndex : -1;
}

//Convert ascii escaped unicode to utf-8
OUString lcl_ConvertToUTF8(const OString& original)
{
    OString sResult = original;
    sal_Int32 nIndex = lcl_IndexOfUnicode(sResult);
    while (nIndex != -1 && nIndex < original.getLength())
    {
        const OString sHex = original.copy(nIndex + 2, 4);
        const sal_Unicode cDec = static_cast<sal_Unicode>(strtol(sHex.getStr(), nullptr, 16));
        const OString sNewChar = OString(&cDec, 1, RTL_TEXTENCODING_UTF8);
        sResult = sResult.replaceAll("\\u" + sHex, sNewChar);
        nIndex = lcl_IndexOfUnicode(original, nIndex + 1);
    }
    return OStringToOUString(sResult, RTL_TEXTENCODING_UTF8);
}

/// Returns substring of sSql from the first occurrence of '(' until the
/// last occurrence of ')' (excluding the parenthesis)
OUString lcl_getColumnPart(const OUString& sSql)
{
    sal_Int32 nBeginIndex = sSql.indexOf("(") + 1;
    if (nBeginIndex < 0)
    {
        SAL_WARN("dbaccess", "No column definitions found");
        return OUString();
    }
    sal_Int32 nCount = sSql.lastIndexOf(")") - nBeginIndex;
    return sSql.copy(nBeginIndex, nCount);
}

/// Constructs a vector of strings that represents the definitions of each
/// column or constraint.
///
/// @param sColumnPart part of the create statement inside the parenthesis
/// containing the column definitions
std::vector<OUString> lcl_splitColumnPart(const OUString& sColumnPart)
{
    std::vector<OUString> sParts = string::split(sColumnPart, sal_Unicode(u','));
    std::vector<OUString> sReturn;

    OUStringBuffer current;
    for (auto const& part : sParts)
    {
        current.append(part);
        if (current.lastIndexOf("(") > current.lastIndexOf(")"))
            current.append(","); // it was false split
        else
            sReturn.push_back(current.makeStringAndClear());
    }
    return sReturn;
}

sal_Int32 lcl_getAutoIncrementDefault(const OUString& sColumnDef)
{
    // TODO what if there are more spaces?
    if (sColumnDef.indexOf("GENERATED BY DEFAULT AS IDENTITY") > 0)
    {
        // TODO parse starting sequence stated by "START WITH"
        return 0;
    }
    return -1;
}

OUString lcl_getDefaultValue(const OUString& sColumnDef)
{
    constexpr char DEFAULT_KW[] = "DEFAULT";
    auto nDefPos = sColumnDef.indexOf(DEFAULT_KW);
    if (nDefPos > 0 && lcl_getAutoIncrementDefault(sColumnDef) < 0)
    {
        const OUString& fromDefault = sColumnDef.copy(nDefPos + sizeof(DEFAULT_KW)).trim();

        // next word is the value
        auto nNextSpace = fromDefault.indexOf(" ");
        return nNextSpace > 0 ? fromDefault.copy(0, fromDefault.indexOf(" ")) : fromDefault;
    }
    return OUString{};
}

bool lcl_isNullable(const OUString& sColumnDef) { return sColumnDef.indexOf("NOT NULL") < 0; }

bool lcl_isPrimaryKey(const OUString& sColumnDef) { return sColumnDef.indexOf("PRIMARY KEY") >= 0; }

sal_Int32 lcl_getDataTypeFromHsql(const OUString& sTypeName)
{
    if (sTypeName == "CHAR")
        return DataType::CHAR;
    else if (sTypeName == "VARCHAR" || sTypeName == "VARCHAR_IGNORECASE")
        return DataType::VARCHAR;
    else if (sTypeName == "TINYINT")
        return DataType::TINYINT;
    else if (sTypeName == "SMALLINT")
        return DataType::SMALLINT;
    else if (sTypeName == "INTEGER")
        return DataType::INTEGER;
    else if (sTypeName == "BIGINT")
        return DataType::BIGINT;
    else if (sTypeName == "NUMERIC")
        return DataType::NUMERIC;
    else if (sTypeName == "DECIMAL")
        return DataType::DECIMAL;
    else if (sTypeName == "BOOLEAN")
        return DataType::BOOLEAN;
    else if (sTypeName == "LONGVARCHAR")
        return DataType::LONGVARCHAR;
    else if (sTypeName == "LONGVARBINARY")
        return DataType::LONGVARBINARY;
    else if (sTypeName == "CLOB")
        return DataType::CLOB;
    else if (sTypeName == "BLOB")
        return DataType::BLOB;
    else if (sTypeName == "BINARY")
        return DataType::BINARY;
    else if (sTypeName == "VARBINARY")
        return DataType::VARBINARY;
    else if (sTypeName == "DATE")
        return DataType::DATE;
    else if (sTypeName == "TIME")
        return DataType::TIME;
    else if (sTypeName == "TIMESTAMP")
        return DataType::TIMESTAMP;
    else if (sTypeName == "DOUBLE")
        return DataType::DOUBLE;
    else if (sTypeName == "REAL")
        return DataType::REAL;
    else if (sTypeName == "FLOAT")
        return DataType::FLOAT;

    assert(false);
    return -1;
}

void lcl_addDefaultParameters(std::vector<sal_Int32>& aParams, sal_Int32 eType)
{
    if (eType == DataType::CHAR || eType == DataType::BINARY || eType == DataType::VARBINARY
        || eType == DataType::VARCHAR)
        aParams.push_back(8000); // from SQL standard
}

struct ColumnTypeParts
{
    OUString typeName;
    std::vector<sal_Int32> params;
};

/**
 * Separates full type descriptions (e.g. NUMERIC(5,4)) to type name (NUMERIC) and
 * parameters (5,4)
 */
ColumnTypeParts lcl_getColumnTypeParts(const OUString& sFullTypeName)
{
    ColumnTypeParts parts;
    auto nParenPos = sFullTypeName.indexOf("(");
    if (nParenPos > 0)
    {
        parts.typeName = sFullTypeName.copy(0, nParenPos).trim();
        OUString sParamStr
            = sFullTypeName.copy(nParenPos + 1, sFullTypeName.indexOf(")") - nParenPos - 1);
        auto sParams = string::split(sParamStr, sal_Unicode(u','));
        for (auto& sParam : sParams)
        {
            parts.params.push_back(sParam.toInt32());
        }
    }
    else
    {
        parts.typeName = sFullTypeName.trim();
        lcl_addDefaultParameters(parts.params, lcl_getDataTypeFromHsql(parts.typeName));
    }
    return parts;
}

} // unnamed namespace

namespace dbahsql
{
CreateStmtParser::CreateStmtParser() {}

void CreateStmtParser::parsePrimaryKeys(const OUString& sPrimaryPart)
{
    sal_Int32 nParenPos = sPrimaryPart.indexOf("(");
    if (nParenPos > 0)
    {
        OUString sParamStr
            = sPrimaryPart.copy(nParenPos + 1, sPrimaryPart.lastIndexOf(")") - nParenPos - 1);
        auto sParams = string::split(sParamStr, sal_Unicode(u','));
        for (auto& sParam : sParams)
        {
            m_PrimaryKeys.push_back(sParam);
        }
    }
}

void CreateStmtParser::parseColumnPart(const OUString& sColumnPart)
{
    auto sColumns = lcl_splitColumnPart(sColumnPart);
    for (OUString& sColumn : sColumns)
    {
        if (sColumn.startsWithIgnoreAsciiCase("PRIMARY KEY"))
        {
            parsePrimaryKeys(sColumn);
            continue;
        }

        if (sColumn.startsWithIgnoreAsciiCase("CONSTRAINT"))
        {
            m_aForeignParts.push_back(sColumn);
            continue;
        }

        bool bIsQuoteUsedForColumnName(sColumn[0] == '\"');

        // find next quote after the initial quote
        // or next space if quote isn't used as delimiter
        auto nEndColumnName
            = bIsQuoteUsedForColumnName ? sColumn.indexOf("\"", 1) + 1 : sColumn.indexOf(" ");
        OUString rColumnName = sColumn.copy(0, nEndColumnName);

        const OUString& sFromTypeName = sColumn.copy(nEndColumnName).trim();

        // Now let's manage the column type
        // search next space to get the whole type name
        // eg: INTEGER, VARCHAR(10), DECIMAL(6,3)
        auto nNextSpace = sFromTypeName.indexOf(" ");
        OUString sFullTypeName;
        if (nNextSpace > 0)
            sFullTypeName = sFromTypeName.copy(0, nNextSpace);
        // perhaps column type corresponds to the last info here
        else
            sFullTypeName = sFromTypeName;

        ColumnTypeParts typeParts = lcl_getColumnTypeParts(sFullTypeName);

        bool bCaseInsensitive = typeParts.typeName.indexOf("IGNORECASE") >= 0;
        rColumnName = lcl_ConvertToUTF8(OUStringToOString(rColumnName, RTL_TEXTENCODING_UTF8));
        bool isPrimaryKey = lcl_isPrimaryKey(sColumn);

        if (isPrimaryKey)
            m_PrimaryKeys.push_back(rColumnName);

        const OUString sColumnWithoutName = sColumn.copy(sColumn.indexOf(typeParts.typeName));

        ColumnDefinition aColDef(rColumnName, lcl_getDataTypeFromHsql(typeParts.typeName),
                                 typeParts.params, isPrimaryKey,
                                 lcl_getAutoIncrementDefault(sColumnWithoutName),
                                 lcl_isNullable(sColumnWithoutName), bCaseInsensitive,
                                 lcl_getDefaultValue(sColumnWithoutName));

        m_aColumns.push_back(aColDef);
    }
}

void CreateStmtParser::parse(const OUString& sSql)
{
    // TODO Foreign keys
    if (!sSql.startsWith("CREATE"))
    {
        SAL_WARN("dbaccess", "Not a create statement");
        return;
    }

    m_sTableName = utils::getTableNameFromStmt(sSql);
    OUString sColumnPart = lcl_getColumnPart(sSql);
    parseColumnPart(sColumnPart);
}

} // namespace dbahsql

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
