// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <optional>

namespace GDriveJson
{

class Parser;

enum class Type
{
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class Value
{
public:
    Value();
    Value(bool b);
    Value(int64_t i);
    Value(int i);
    Value(double d);
    Value(const char* s);
    Value(std::string s);

    Type GetType() const { return m_type; }
    bool IsNull() const { return m_type == Type::Null; }
    bool IsBool() const { return m_type == Type::Boolean; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsArray() const { return m_type == Type::Array; }
    bool IsObject() const { return m_type == Type::Object; }

    bool AsBool(bool def = false) const;
    int64_t AsInt64(int64_t def = 0) const;
    int AsInt(int def = 0) const { return (int)AsInt64(def); }
    double AsDouble(double def = 0.0) const;
    const std::string& AsString(const std::string& def = "") const;

    // Array operations
    size_t Size() const;
    const Value& operator[](size_t index) const;
    void PushBack(const Value& val);

    // Object operations
    bool Has(const std::string& key) const;
    const Value& operator[](const std::string& key) const;
    const Value& Get(const std::string& key) const;
    void Set(const std::string& key, const Value& val);
    const std::map<std::string, Value>& GetObjectMembers() const;
    const std::vector<Value>& GetArrayElements() const;

    // Helpers
    std::string GetString(const std::string& key, const std::string& def = "") const;
    int64_t GetInt64(const std::string& key, int64_t def = 0) const;
    int GetInt(const std::string& key, int def = 0) const;
    bool GetBool(const std::string& key, bool def = false) const;
    double GetDouble(const std::string& key, double def = 0.0) const;
    const Value& GetObject(const std::string& key) const;
    const Value& GetArray(const std::string& key) const;

    std::string Serialize(bool pretty = false) const;

    static Value Parse(const std::string& json, std::string* errorOut = nullptr);
    static Value NullValue;

private:
    friend class Parser;

    Type m_type;
    bool m_boolVal;
    int64_t m_intVal;
    double m_doubleVal;
    bool m_isDouble;
    std::string m_stringVal;
    std::vector<Value> m_arrayVal;
    std::map<std::string, Value> m_objectVal;

    void SerializeInternal(std::string& out, int indent, bool pretty) const;
};

} // namespace GDriveJson
