// SPDX-FileCopyrightText: 2026 Open Salamander Authors & Red Salamander Authors
// SPDX-FileContributor: Ported to Open Salamander framework by fila73
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "gdrive_json.h"

namespace GDriveJson
{

Value Value::NullValue;

Value::Value()
    : m_type(Type::Null), m_boolVal(false), m_intVal(0), m_doubleVal(0.0), m_isDouble(false)
{
}

Value::Value(bool b)
    : m_type(Type::Boolean), m_boolVal(b), m_intVal(0), m_doubleVal(0.0), m_isDouble(false)
{
}

Value::Value(int64_t i)
    : m_type(Type::Number), m_boolVal(false), m_intVal(i), m_doubleVal((double)i), m_isDouble(false)
{
}

Value::Value(int i)
    : m_type(Type::Number), m_boolVal(false), m_intVal(i), m_doubleVal((double)i), m_isDouble(false)
{
}

Value::Value(double d)
    : m_type(Type::Number), m_boolVal(false), m_intVal((int64_t)d), m_doubleVal(d), m_isDouble(true)
{
}

Value::Value(const char* s)
    : m_type(s ? Type::String : Type::Null), m_boolVal(false), m_intVal(0), m_doubleVal(0.0), m_isDouble(false), m_stringVal(s ? s : "")
{
}

Value::Value(std::string s)
    : m_type(Type::String), m_boolVal(false), m_intVal(0), m_doubleVal(0.0), m_isDouble(false), m_stringVal(std::move(s))
{
}

bool Value::AsBool(bool def) const
{
    if (m_type == Type::Boolean) return m_boolVal;
    if (m_type == Type::Number) return m_intVal != 0;
    if (m_type == Type::String) return m_stringVal == "true" || m_stringVal == "1";
    return def;
}

int64_t Value::AsInt64(int64_t def) const
{
    if (m_type == Type::Number) return m_intVal;
    if (m_type == Type::Boolean) return m_boolVal ? 1 : 0;
    if (m_type == Type::String && !m_stringVal.empty())
    {
        try { return std::stoll(m_stringVal); } catch (...) {}
    }
    return def;
}

double Value::AsDouble(double def) const
{
    if (m_type == Type::Number) return m_doubleVal;
    if (m_type == Type::String && !m_stringVal.empty())
    {
        try { return std::stod(m_stringVal); } catch (...) {}
    }
    return def;
}

const std::string& Value::AsString(const std::string& def) const
{
    if (m_type == Type::String) return m_stringVal;
    return def;
}

size_t Value::Size() const
{
    if (m_type == Type::Array) return m_arrayVal.size();
    if (m_type == Type::Object) return m_objectVal.size();
    return 0;
}

const Value& Value::operator[](size_t index) const
{
    if (m_type == Type::Array && index < m_arrayVal.size())
        return m_arrayVal[index];
    return NullValue;
}

void Value::PushBack(const Value& val)
{
    if (m_type != Type::Array)
    {
        m_type = Type::Array;
        m_arrayVal.clear();
    }
    m_arrayVal.push_back(val);
}

bool Value::Has(const std::string& key) const
{
    if (m_type != Type::Object) return false;
    return m_objectVal.find(key) != m_objectVal.end();
}

const Value& Value::operator[](const std::string& key) const
{
    return Get(key);
}

const Value& Value::Get(const std::string& key) const
{
    if (m_type == Type::Object)
    {
        auto it = m_objectVal.find(key);
        if (it != m_objectVal.end()) return it->second;
    }
    return NullValue;
}

void Value::Set(const std::string& key, const Value& val)
{
    if (m_type != Type::Object)
    {
        m_type = Type::Object;
        m_objectVal.clear();
    }
    m_objectVal[key] = val;
}

const std::map<std::string, Value>& Value::GetObjectMembers() const
{
    return m_objectVal;
}

const std::vector<Value>& Value::GetArrayElements() const
{
    return m_arrayVal;
}

std::string Value::GetString(const std::string& key, const std::string& def) const
{
    const Value& v = Get(key);
    return v.IsString() ? v.AsString() : def;
}

int64_t Value::GetInt64(const std::string& key, int64_t def) const
{
    const Value& v = Get(key);
    return (v.IsNumber() || v.IsString()) ? v.AsInt64(def) : def;
}

int Value::GetInt(const std::string& key, int def) const
{
    return (int)GetInt64(key, def);
}

bool Value::GetBool(const std::string& key, bool def) const
{
    const Value& v = Get(key);
    return v.IsBool() ? v.AsBool() : def;
}

double Value::GetDouble(const std::string& key, double def) const
{
    const Value& v = Get(key);
    return v.IsNumber() ? v.AsDouble() : def;
}

const Value& Value::GetObject(const std::string& key) const
{
    const Value& v = Get(key);
    return v.IsObject() ? v : NullValue;
}

const Value& Value::GetArray(const std::string& key) const
{
    const Value& v = Get(key);
    return v.IsArray() ? v : NullValue;
}

static void EscapeString(const std::string& in, std::string& out)
{
    out.push_back('"');
    for (char c : in)
    {
        switch (c)
        {
        case '"': out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default:
            if ((unsigned char)c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                out.append(buf);
            }
            else
            {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
}

void Value::SerializeInternal(std::string& out, int indent, bool pretty) const
{
    std::string indentStr = pretty ? std::string(indent * 2, ' ') : "";
    std::string childIndentStr = pretty ? std::string((indent + 1) * 2, ' ') : "";
    std::string nl = pretty ? "\n" : "";
    std::string sp = pretty ? " " : "";

    switch (m_type)
    {
    case Type::Null:
        out.append("null");
        break;
    case Type::Boolean:
        out.append(m_boolVal ? "true" : "false");
        break;
    case Type::Number:
        if (m_isDouble)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.6g", m_doubleVal);
            out.append(buf);
        }
        else
        {
            out.append(std::to_string(m_intVal));
        }
        break;
    case Type::String:
        EscapeString(m_stringVal, out);
        break;
    case Type::Array:
        if (m_arrayVal.empty())
        {
            out.append("[]");
        }
        else
        {
            out.push_back('[');
            out.append(nl);
            for (size_t i = 0; i < m_arrayVal.size(); ++i)
            {
                out.append(childIndentStr);
                m_arrayVal[i].SerializeInternal(out, indent + 1, pretty);
                if (i + 1 < m_arrayVal.size()) out.push_back(',');
                out.append(nl);
            }
            out.append(indentStr);
            out.push_back(']');
        }
        break;
    case Type::Object:
        if (m_objectVal.empty())
        {
            out.append("{}");
        }
        else
        {
            out.push_back('{');
            out.append(nl);
            size_t count = 0;
            for (const auto& [k, v] : m_objectVal)
            {
                out.append(childIndentStr);
                EscapeString(k, out);
                out.push_back(':');
                out.append(sp);
                v.SerializeInternal(out, indent + 1, pretty);
                if (++count < m_objectVal.size()) out.push_back(',');
                out.append(nl);
            }
            out.append(indentStr);
            out.push_back('}');
        }
        break;
    }
}

std::string Value::Serialize(bool pretty) const
{
    std::string out;
    SerializeInternal(out, 0, pretty);
    return out;
}

// Simple fast recursive descent JSON parser
class Parser
{
public:
    Parser(const std::string& src) : m_src(src), m_pos(0), m_len(src.size()) {}

    Value Parse(std::string* errorOut)
    {
        SkipWhitespace();
        if (m_pos >= m_len)
        {
            if (errorOut) *errorOut = "Empty JSON input";
            return Value::NullValue;
        }

        Value v = ParseValue();
        SkipWhitespace();
        if (m_pos < m_len && !m_error)
        {
            m_error = true;
            m_errorMsg = "Unexpected characters after JSON root at position " + std::to_string(m_pos);
        }

        if (m_error)
        {
            if (errorOut) *errorOut = m_errorMsg;
            return Value::NullValue;
        }
        return v;
    }

private:
    const std::string& m_src;
    size_t m_pos;
    size_t m_len;
    bool m_error = false;
    std::string m_errorMsg;

    void SkipWhitespace()
    {
        while (m_pos < m_len)
        {
            char c = m_src[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                m_pos++;
            }
            else
            {
                break;
            }
        }
    }

    Value ParseValue()
    {
        SkipWhitespace();
        if (m_pos >= m_len)
        {
            m_error = true;
            m_errorMsg = "Unexpected end of input";
            return Value::NullValue;
        }

        char c = m_src[m_pos];
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == 't' || c == 'f') return ParseBool();
        if (c == 'n') return ParseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();

        m_error = true;
        m_errorMsg = std::string("Unexpected character '") + c + "' at position " + std::to_string(m_pos);
        return Value::NullValue;
    }

    Value ParseObject()
    {
        m_pos++; // skip '{'
        Value obj;
        obj.m_type = Type::Object;

        while (true)
        {
            SkipWhitespace();
            if (m_pos >= m_len)
            {
                m_error = true;
                m_errorMsg = "Unterminated object";
                return Value::NullValue;
            }

            if (m_src[m_pos] == '}')
            {
                m_pos++;
                return obj;
            }

            if (m_src[m_pos] != '"')
            {
                m_error = true;
                m_errorMsg = "Expected string key in object";
                return Value::NullValue;
            }

            Value keyVal = ParseString();
            if (m_error) return Value::NullValue;
            std::string key = keyVal.AsString();

            SkipWhitespace();
            if (m_pos >= m_len || m_src[m_pos] != ':')
            {
                m_error = true;
                m_errorMsg = "Expected ':' after key in object";
                return Value::NullValue;
            }
            m_pos++; // skip ':'

            Value val = ParseValue();
            if (m_error) return Value::NullValue;
            obj.Set(key, val);

            SkipWhitespace();
            if (m_pos >= m_len)
            {
                m_error = true;
                m_errorMsg = "Unterminated object";
                return Value::NullValue;
            }

            if (m_src[m_pos] == ',')
            {
                m_pos++;
            }
            else if (m_src[m_pos] == '}')
            {
                m_pos++;
                return obj;
            }
            else
            {
                m_error = true;
                m_errorMsg = "Expected ',' or '}' in object";
                return Value::NullValue;
            }
        }
    }

    Value ParseArray()
    {
        m_pos++; // skip '['
        Value arr;
        arr.m_type = Type::Array;

        while (true)
        {
            SkipWhitespace();
            if (m_pos >= m_len)
            {
                m_error = true;
                m_errorMsg = "Unterminated array";
                return Value::NullValue;
            }

            if (m_src[m_pos] == ']')
            {
                m_pos++;
                return arr;
            }

            Value item = ParseValue();
            if (m_error) return Value::NullValue;
            arr.PushBack(item);

            SkipWhitespace();
            if (m_pos >= m_len)
            {
                m_error = true;
                m_errorMsg = "Unterminated array";
                return Value::NullValue;
            }

            if (m_src[m_pos] == ',')
            {
                m_pos++;
            }
            else if (m_src[m_pos] == ']')
            {
                m_pos++;
                return arr;
            }
            else
            {
                m_error = true;
                m_errorMsg = "Expected ',' or ']' in array";
                return Value::NullValue;
            }
        }
    }

    Value ParseString()
    {
        m_pos++; // skip opening '"'
        std::string s;
        while (m_pos < m_len)
        {
            char c = m_src[m_pos++];
            if (c == '"')
            {
                return Value(s);
            }
            else if (c == '\\')
            {
                if (m_pos >= m_len) break;
                char esc = m_src[m_pos++];
                switch (esc)
                {
                case '"': s.push_back('"'); break;
                case '\\': s.push_back('\\'); break;
                case '/': s.push_back('/'); break;
                case 'b': s.push_back('\b'); break;
                case 'f': s.push_back('\f'); break;
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                case 'u':
                {
                    if (m_pos + 4 <= m_len)
                    {
                        std::string hexStr = m_src.substr(m_pos, 4);
                        m_pos += 4;
                        unsigned int codepoint = 0;
                        try { codepoint = std::stoul(hexStr, nullptr, 16); } catch (...) {}
                        if (codepoint <= 0x7F)
                        {
                            s.push_back((char)codepoint);
                        }
                        else if (codepoint <= 0x7FF)
                        {
                            s.push_back((char)(0xC0 | (codepoint >> 6)));
                            s.push_back((char)(0x80 | (codepoint & 0x3F)));
                        }
                        else
                        {
                            s.push_back((char)(0xE0 | (codepoint >> 12)));
                            s.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
                            s.push_back((char)(0x80 | (codepoint & 0x3F)));
                        }
                    }
                    break;
                }
                default:
                    s.push_back(esc);
                    break;
                }
            }
            else
            {
                s.push_back(c);
            }
        }

        m_error = true;
        m_errorMsg = "Unterminated string";
        return Value::NullValue;
    }

    Value ParseBool()
    {
        if (m_src.compare(m_pos, 4, "true") == 0)
        {
            m_pos += 4;
            return Value(true);
        }
        if (m_src.compare(m_pos, 5, "false") == 0)
        {
            m_pos += 5;
            return Value(false);
        }
        m_error = true;
        m_errorMsg = "Expected boolean";
        return Value::NullValue;
    }

    Value ParseNull()
    {
        if (m_src.compare(m_pos, 4, "null") == 0)
        {
            m_pos += 4;
            return Value::NullValue;
        }
        m_error = true;
        m_errorMsg = "Expected null";
        return Value::NullValue;
    }

    Value ParseNumber()
    {
        size_t start = m_pos;
        if (m_src[m_pos] == '-') m_pos++;

        while (m_pos < m_len && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
            m_pos++;

        bool isDouble = false;
        if (m_pos < m_len && m_src[m_pos] == '.')
        {
            isDouble = true;
            m_pos++;
            while (m_pos < m_len && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
                m_pos++;
        }

        if (m_pos < m_len && (m_src[m_pos] == 'e' || m_src[m_pos] == 'E'))
        {
            isDouble = true;
            m_pos++;
            if (m_pos < m_len && (m_src[m_pos] == '+' || m_src[m_pos] == '-'))
                m_pos++;
            while (m_pos < m_len && m_src[m_pos] >= '0' && m_src[m_pos] <= '9')
                m_pos++;
        }

        std::string numStr = m_src.substr(start, m_pos - start);
        if (isDouble)
        {
            try { return Value(std::stod(numStr)); } catch (...) {}
        }
        else
        {
            try { return Value((int64_t)std::stoll(numStr)); } catch (...) {}
        }

        m_error = true;
        m_errorMsg = "Invalid number format: " + numStr;
        return Value::NullValue;
    }
};

Value Value::Parse(const std::string& json, std::string* errorOut)
{
    Parser p(json);
    return p.Parse(errorOut);
}

} // namespace GDriveJson
