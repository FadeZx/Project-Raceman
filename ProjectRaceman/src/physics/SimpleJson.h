#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace raceman::physics::json
{

class ParseError : public std::runtime_error
{
public:
    explicit ParseError(const std::string &message) : std::runtime_error(message) {}
};

struct Value;

using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value
{
    using Variant = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;
    Variant data{nullptr};

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_number() const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_object() const { return std::holds_alternative<Object>(data); }
    bool is_array() const { return std::holds_alternative<Array>(data); }

    const Object &as_object() const
    {
        if (!is_object())
        {
            throw ParseError("Value is not an object");
        }
        return std::get<Object>(data);
    }

    const Array &as_array() const
    {
        if (!is_array())
        {
            throw ParseError("Value is not an array");
        }
        return std::get<Array>(data);
    }

    double as_number() const
    {
        if (!is_number())
        {
            throw ParseError("Value is not a number");
        }
        return std::get<double>(data);
    }

    bool as_bool() const
    {
        if (!is_bool())
        {
            throw ParseError("Value is not a boolean");
        }
        return std::get<bool>(data);
    }

    const std::string &as_string() const
    {
        if (!is_string())
        {
            throw ParseError("Value is not a string");
        }
        return std::get<std::string>(data);
    }

    const Value &at(const std::string &key) const
    {
        const auto &obj = as_object();
        auto it = obj.find(key);
        if (it == obj.end())
        {
            throw ParseError("Missing key: " + key);
        }
        return it->second;
    }
};

class Parser
{
public:
    explicit Parser(const std::string &src) : m_src(src) {}

    Value parse()
    {
        skip_whitespace();
        Value value = parse_value();
        skip_whitespace();
        if (!eof())
        {
            throw ParseError("Unexpected trailing characters");
        }
        return value;
    }

private:
    Value parse_value()
    {
        skip_whitespace();
        if (eof())
        {
            throw ParseError("Unexpected end of input");
        }

        char c = peek();
        if (c == '{')
        {
            return parse_object();
        }
        if (c == '[')
        {
            return parse_array();
        }
        if (c == '"')
        {
            return Value{parse_string()};
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
        {
            return Value{parse_number()};
        }
        if (match("true"))
        {
            return Value{true};
        }
        if (match("false"))
        {
            return Value{false};
        }
        if (match("null"))
        {
            return Value{nullptr};
        }

        throw ParseError("Unexpected character: " + std::string(1, c));
    }

    Object parse_object()
    {
        expect('{');
        Object object;
        skip_whitespace();
        if (peek() == '}')
        {
            advance();
            return object;
        }

        while (true)
        {
            skip_whitespace();
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            skip_whitespace();
            object.emplace(std::move(key), parse_value());
            skip_whitespace();
            if (peek() == '}')
            {
                advance();
                break;
            }
            expect(',');
        }
        return object;
    }

    Array parse_array()
    {
        expect('[');
        Array array;
        skip_whitespace();
        if (peek() == ']')
        {
            advance();
            return array;
        }

        while (true)
        {
            skip_whitespace();
            array.emplace_back(parse_value());
            skip_whitespace();
            if (peek() == ']')
            {
                advance();
                break;
            }
            expect(',');
        }
        return array;
    }

    std::string parse_string()
    {
        expect('"');
        std::string result;
        while (!eof())
        {
            char c = advance();
            if (c == '"')
            {
                return result;
            }
            if (c == '\\')
            {
                if (eof())
                {
                    throw ParseError("Unterminated escape sequence");
                }
                char esc = advance();
                switch (esc)
                {
                case '"':
                case '\\':
                case '/':
                    result.push_back(esc);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u':
                    result.push_back(parse_unicode_escape());
                    break;
                default:
                    throw ParseError("Invalid escape sequence");
                }
            }
            else
            {
                result.push_back(c);
            }
        }
        throw ParseError("Unterminated string literal");
    }

    char parse_unicode_escape()
    {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (eof())
            {
                throw ParseError("Incomplete unicode escape sequence");
            }
            char c = advance();
            value <<= 4;
            if (c >= '0' && c <= '9')
            {
                value |= (c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                value |= (c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                value |= (c - 'A' + 10);
            }
            else
            {
                throw ParseError("Invalid unicode escape character");
            }
        }
        return static_cast<char>(value & 0xFFu);
    }

    double parse_number()
    {
        std::size_t start = m_index;
        if (peek() == '-')
        {
            advance();
        }
        if (peek() == '0')
        {
            advance();
        }
        else
        {
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
            {
                advance();
            }
        }
        if (!eof() && peek() == '.')
        {
            advance();
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
            {
                advance();
            }
        }
        if (!eof() && (peek() == 'e' || peek() == 'E'))
        {
            advance();
            if (peek() == '+' || peek() == '-')
            {
                advance();
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
            {
                advance();
            }
        }
        double value = std::stod(m_src.substr(start, m_index - start));
        return value;
    }

    void skip_whitespace()
    {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek())))
        {
            advance();
        }
    }

    void expect(char expected)
    {
        if (eof() || peek() != expected)
        {
            throw ParseError(std::string("Expected '") + expected + "'");
        }
        advance();
    }

    bool match(const char *token)
    {
        std::size_t len = std::strlen(token);
        if (m_src.compare(m_index, len, token) == 0)
        {
            m_index += len;
            return true;
        }
        return false;
    }

    char advance()
    {
        return m_src[m_index++];
    }

    char peek() const
    {
        return m_src[m_index];
    }

    bool eof() const
    {
        return m_index >= m_src.size();
    }

    const std::string &m_src;
    std::size_t m_index{0};
};

inline Value parse(const std::string &src)
{
    Parser parser(src);
    return parser.parse();
}

} // namespace raceman::physics::json

