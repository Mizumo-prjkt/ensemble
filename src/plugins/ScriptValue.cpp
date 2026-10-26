#include "ScriptValue.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace EnsemblePlugin {

ScriptValue::ScriptValue()
    : m_data(std::monostate{})
{
}

ScriptValue::ScriptValue(bool val)
    : m_data(val)
{
}

ScriptValue::ScriptValue(double val)
    : m_data(val)
{
}

ScriptValue::ScriptValue(int val)
    : m_data(static_cast<double>(val))
{
}

ScriptValue::ScriptValue(int64_t val)
    : m_data(static_cast<double>(val))
{
}

ScriptValue::ScriptValue(const char *str)
    : m_data(str ? std::string(str) : std::string{})
{
}

ScriptValue::ScriptValue(std::string str)
    : m_data(std::move(str))
{
}

ScriptValue::ScriptValue(std::shared_ptr<ScriptList> list)
    : m_data(std::move(list))
{
}

ScriptValue::ScriptValue(std::shared_ptr<ScriptDict> dict)
    : m_data(std::move(dict))
{
}

ScriptValue::ScriptValue(NativeFuncSignature nativeFunc, std::string funcName)
    : m_data(NativeFuncWrapper{std::move(nativeFunc), std::move(funcName)})
{
}

ScriptValue::ScriptValue(ScriptFunction userFunc)
    : m_data(std::move(userFunc))
{
}

ScriptValue ScriptValue::makeList(ResourceGuard &guard, int line, int col) {
    guard.alloc(sizeof(ScriptList), line, col);
    return ScriptValue(std::make_shared<ScriptList>());
}

ScriptValue ScriptValue::makeDict(ResourceGuard &guard, int line, int col) {
    guard.alloc(sizeof(ScriptDict), line, col);
    return ScriptValue(std::make_shared<ScriptDict>());
}

ScriptValue::Type ScriptValue::type() const noexcept {
    switch (m_data.index()) {
    case 0: return Type::None;
    case 1: return Type::Bool;
    case 2: return Type::Number;
    case 3: return Type::String;
    case 4: return Type::List;
    case 5: return Type::Dict;
    case 6: return Type::NativeFunc;
    case 7: return Type::UserFunc;
    default: return Type::None;
    }
}

std::string ScriptValue::typeName() const {
    switch (type()) {
    case Type::None: return "NoneType";
    case Type::Bool: return "bool";
    case Type::Number: return "number";
    case Type::String: return "str";
    case Type::List: return "list";
    case Type::Dict: return "dict";
    case Type::NativeFunc:
    case Type::UserFunc: return "function";
    }
    return "unknown";
}

bool ScriptValue::isNone() const noexcept { return std::holds_alternative<std::monostate>(m_data); }
bool ScriptValue::isBool() const noexcept { return std::holds_alternative<bool>(m_data); }
bool ScriptValue::isNumber() const noexcept { return std::holds_alternative<double>(m_data); }
bool ScriptValue::isString() const noexcept { return std::holds_alternative<std::string>(m_data); }
bool ScriptValue::isList() const noexcept { return std::holds_alternative<std::shared_ptr<ScriptList>>(m_data); }
bool ScriptValue::isDict() const noexcept { return std::holds_alternative<std::shared_ptr<ScriptDict>>(m_data); }
bool ScriptValue::isCallable() const noexcept {
    return std::holds_alternative<NativeFuncWrapper>(m_data) || std::holds_alternative<ScriptFunction>(m_data);
}

bool ScriptValue::toBool() const {
    if (isNone()) return false;
    if (isBool()) return std::get<bool>(m_data);
    if (isNumber()) return std::get<double>(m_data) != 0.0;
    if (isString()) return !std::get<std::string>(m_data).empty();
    if (isList()) {
        auto l = std::get<std::shared_ptr<ScriptList>>(m_data);
        return l && !l->empty();
    }
    if (isDict()) {
        auto d = std::get<std::shared_ptr<ScriptDict>>(m_data);
        return d && !d->empty();
    }
    if (isCallable()) return true;
    return false;
}

double ScriptValue::toNumber(int line, int col) const {
    if (isNumber()) return std::get<double>(m_data);
    if (isBool()) return std::get<bool>(m_data) ? 1.0 : 0.0;
    if (isString()) {
        const std::string &s = std::get<std::string>(m_data);
        try {
            size_t idx = 0;
            double v = std::stod(s, &idx);
            if (idx == s.size()) return v;
        } catch (...) {
        }
        throw ScriptTrap("ValueError: could not convert string to number: '" + s + "'", line, col);
    }
    throw ScriptTrap("TypeError: expected number or boolean, got " + typeName(), line, col);
}

int64_t ScriptValue::toInteger(int line, int col) const {
    double d = toNumber(line, col);
    if (std::isnan(d) || std::isinf(d)) {
        throw ScriptTrap("ValueError: cannot convert NaN or Inf to integer", line, col);
    }
    return static_cast<int64_t>(d);
}

std::string ScriptValue::toString() const {
    if (isNone()) return "None";
    if (isBool()) return std::get<bool>(m_data) ? "True" : "False";
    if (isNumber()) {
        double d = std::get<double>(m_data);
        if (d == std::floor(d) && !std::isinf(d) && std::abs(d) < 1e15) {
            return std::to_string(static_cast<int64_t>(d));
        }
        std::ostringstream ss;
        ss << d;
        return ss.str();
    }
    if (isString()) return std::get<std::string>(m_data);
    if (isList()) {
        auto list = std::get<std::shared_ptr<ScriptList>>(m_data);
        if (!list) return "[]";
        std::string s = "[";
        for (size_t i = 0; i < list->size(); ++i) {
            if (i > 0) s += ", ";
            const auto &item = list->at(i);
            if (item.isString()) {
                s += "'" + item.toString() + "'";
            } else {
                s += item.toString();
            }
        }
        s += "]";
        return s;
    }
    if (isDict()) {
        auto dict = std::get<std::shared_ptr<ScriptDict>>(m_data);
        if (!dict) return "{}";
        std::string s = "{";
        size_t idx = 0;
        for (const auto &pair : *dict) {
            if (idx++ > 0) s += ", ";
            s += "'" + pair.first + "': ";
            if (pair.second.isString()) {
                s += "'" + pair.second.toString() + "'";
            } else {
                s += pair.second.toString();
            }
        }
        s += "}";
        return s;
    }
    if (std::holds_alternative<NativeFuncWrapper>(m_data)) {
        return "<native_function " + std::get<NativeFuncWrapper>(m_data).name + ">";
    }
    if (std::holds_alternative<ScriptFunction>(m_data)) {
        return "<function " + std::get<ScriptFunction>(m_data).name + ">";
    }
    return "None";
}

size_t ScriptValue::estimateBytes() const noexcept {
    size_t base = sizeof(ScriptValue);
    if (isString()) {
        base += std::get<std::string>(m_data).capacity();
    } else if (isList()) {
        auto l = std::get<std::shared_ptr<ScriptList>>(m_data);
        if (l) {
            base += sizeof(ScriptList) + (l->capacity() * sizeof(ScriptValue));
            for (const auto &item : *l) {
                base += item.estimateBytes();
            }
        }
    } else if (isDict()) {
        auto d = std::get<std::shared_ptr<ScriptDict>>(m_data);
        if (d) {
            base += sizeof(ScriptDict);
            for (const auto &pair : *d) {
                base += pair.first.capacity() + pair.second.estimateBytes() + 32;
            }
        }
    }
    return base;
}

bool ScriptValue::asBool() const {
    if (!isBool()) throw ScriptTrap("Expected bool");
    return std::get<bool>(m_data);
}

double ScriptValue::asNumber() const {
    if (!isNumber()) throw ScriptTrap("Expected number");
    return std::get<double>(m_data);
}

const std::string &ScriptValue::asString() const {
    if (!isString()) throw ScriptTrap("Expected string");
    return std::get<std::string>(m_data);
}

std::shared_ptr<ScriptList> ScriptValue::asList() const {
    if (!isList()) throw ScriptTrap("Expected list");
    return std::get<std::shared_ptr<ScriptList>>(m_data);
}

std::shared_ptr<ScriptDict> ScriptValue::asDict() const {
    if (!isDict()) throw ScriptTrap("Expected dict");
    return std::get<std::shared_ptr<ScriptDict>>(m_data);
}

const ScriptFunction &ScriptValue::asUserFunc() const {
    if (!std::holds_alternative<ScriptFunction>(m_data)) throw ScriptTrap("Expected user function");
    return std::get<ScriptFunction>(m_data);
}

const NativeFuncSignature &ScriptValue::asNativeFunc() const {
    if (!std::holds_alternative<NativeFuncWrapper>(m_data)) throw ScriptTrap("Expected native function");
    return std::get<NativeFuncWrapper>(m_data).func;
}

ScriptValue ScriptValue::getItem(const ScriptValue &indexOrKey, ResourceGuard &guard, int line, int col) const {
    if (isList()) {
        auto list = asList();
        if (!list) throw ScriptTrap("ReferenceError: list is null", line, col);
        int64_t idx = indexOrKey.toInteger(line, col);
        if (idx < 0) idx += static_cast<int64_t>(list->size());
        if (idx < 0 || idx >= static_cast<int64_t>(list->size())) {
            throw ScriptTrap("IndexError: list index out of range (" + std::to_string(idx) + ")", line, col);
        }
        return list->at(static_cast<size_t>(idx));
    }
    if (isString()) {
        const std::string &str = asString();
        int64_t idx = indexOrKey.toInteger(line, col);
        if (idx < 0) idx += static_cast<int64_t>(str.size());
        if (idx < 0 || idx >= static_cast<int64_t>(str.size())) {
            throw ScriptTrap("IndexError: string index out of range (" + std::to_string(idx) + ")", line, col);
        }
        guard.alloc(sizeof(std::string) + 2, line, col);
        return ScriptValue(std::string(1, str.at(static_cast<size_t>(idx))));
    }
    if (isDict()) {
        auto dict = asDict();
        if (!dict) throw ScriptTrap("ReferenceError: dict is null", line, col);
        std::string key = indexOrKey.toString();
        auto it = dict->find(key);
        if (it == dict->end()) {
            throw ScriptTrap("KeyError: '" + key + "'", line, col);
        }
        return it->second;
    }
    throw ScriptTrap("TypeError: '" + typeName() + "' object is not subscriptable", line, col);
}

void ScriptValue::setItem(const ScriptValue &indexOrKey, const ScriptValue &value, ResourceGuard &guard, int line, int col) {
    if (isList()) {
        auto list = asList();
        if (!list) throw ScriptTrap("ReferenceError: list is null", line, col);
        int64_t idx = indexOrKey.toInteger(line, col);
        if (idx < 0) idx += static_cast<int64_t>(list->size());
        if (idx < 0 || idx >= static_cast<int64_t>(list->size())) {
            throw ScriptTrap("IndexError: list assignment index out of range", line, col);
        }
        guard.alloc(value.estimateBytes(), line, col);
        list->at(static_cast<size_t>(idx)) = value;
        return;
    }
    if (isDict()) {
        auto dict = asDict();
        if (!dict) throw ScriptTrap("ReferenceError: dict is null", line, col);
        std::string key = indexOrKey.toString();
        if (key.find("__") != std::string::npos) {
            throw ScriptTrap("SecurityError: double-underscore keys are forbidden", line, col);
        }
        size_t memDelta = key.capacity() + value.estimateBytes() + 32;
        guard.alloc(memDelta, line, col);
        (*dict)[key] = value;
        return;
    }
    throw ScriptTrap("TypeError: '" + typeName() + "' object does not support item assignment", line, col);
}

bool ScriptValue::delItem(const ScriptValue &indexOrKey, ResourceGuard &guard, int line, int col) {
    if (isList()) {
        auto list = asList();
        if (!list) return false;
        int64_t idx = indexOrKey.toInteger(line, col);
        if (idx < 0) idx += static_cast<int64_t>(list->size());
        if (idx < 0 || idx >= static_cast<int64_t>(list->size())) {
            throw ScriptTrap("IndexError: list index out of range", line, col);
        }
        list->erase(list->begin() + idx);
        return true;
    }
    if (isDict()) {
        auto dict = asDict();
        if (!dict) return false;
        std::string key = indexOrKey.toString();
        auto it = dict->find(key);
        if (it != dict->end()) {
            dict->erase(it);
            return true;
        }
        return false;
    }
    throw ScriptTrap("TypeError: '" + typeName() + "' object does not support item deletion", line, col);
}

ScriptValue ScriptValue::getMember(const std::string &memberName, ResourceGuard &guard, int line, int col) const {
    if (memberName.find("__") != std::string::npos) {
        throw ScriptTrap("SecurityError: access to double-underscore ('" + memberName + "') is forbidden", line, col);
    }
    if (isDict()) {
        auto dict = asDict();
        if (dict) {
            auto it = dict->find(memberName);
            if (it != dict->end()) {
                return it->second;
            }
        }
        if (memberName == "get" || memberName == "keys" || memberName == "values" ||
            memberName == "clear" || memberName == "len") {
            ScriptValue self = *this;
            return ScriptValue([self, memberName](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
                return self.callMember(memberName, args, g, l, c);
            }, memberName);
        }
    }
    if (isString() || isList()) {
        ScriptValue self = *this;
        return ScriptValue([self, memberName](const std::vector<ScriptValue> &args, ResourceGuard &g, int l, int c) -> ScriptValue {
            return self.callMember(memberName, args, g, l, c);
        }, memberName);
    }
    // Return bound method or attribute error handled in callMember
    throw ScriptTrap("AttributeError: '" + typeName() + "' object has no attribute '" + memberName + "'", line, col);
}

void ScriptValue::setMember(const std::string &memberName, const ScriptValue &value, ResourceGuard &guard, int line, int col) {
    if (memberName.find("__") != std::string::npos) {
        throw ScriptTrap("SecurityError: double-underscore attributes are forbidden", line, col);
    }
    if (isDict()) {
        setItem(ScriptValue(memberName), value, guard, line, col);
        return;
    }
    throw ScriptTrap("TypeError: cannot assign attribute to '" + typeName() + "'", line, col);
}

ScriptValue ScriptValue::callMember(const std::string &methodName, const std::vector<ScriptValue> &args, ResourceGuard &guard, int line, int col) const {
    if (methodName.find("__") != std::string::npos) {
        throw ScriptTrap("SecurityError: access to double-underscore ('" + methodName + "') is forbidden", line, col);
    }

    // Built-in String methods
    if (isString()) {
        const std::string &s = asString();
        if (methodName == "lower") {
            std::string res = s;
            std::transform(res.begin(), res.end(), res.begin(), ::tolower);
            guard.alloc(res.capacity(), line, col);
            return ScriptValue(std::move(res));
        }
        if (methodName == "upper") {
            std::string res = s;
            std::transform(res.begin(), res.end(), res.begin(), ::toupper);
            guard.alloc(res.capacity(), line, col);
            return ScriptValue(std::move(res));
        }
        if (methodName == "strip") {
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return ScriptValue("");
            size_t end = s.find_last_not_of(" \t\r\n");
            std::string res = s.substr(start, end - start + 1);
            guard.alloc(res.capacity(), line, col);
            return ScriptValue(std::move(res));
        }
        if (methodName == "split") {
            std::string delim = " ";
            if (!args.empty() && args.at(0).isString()) {
                delim = args.at(0).asString();
            }
            auto list = std::make_shared<ScriptList>();
            if (delim.empty()) {
                guard.alloc(sizeof(ScriptList), line, col);
                return ScriptValue(list);
            }
            size_t start = 0;
            size_t pos = 0;
            while ((pos = s.find(delim, start)) != std::string::npos) {
                std::string token = s.substr(start, pos - start);
                guard.alloc(token.capacity() + sizeof(ScriptValue), line, col);
                list->push_back(ScriptValue(std::move(token)));
                start = pos + delim.length();
            }
            std::string token = s.substr(start);
            guard.alloc(token.capacity() + sizeof(ScriptValue) + sizeof(ScriptList), line, col);
            list->push_back(ScriptValue(std::move(token)));
            return ScriptValue(list);
        }
        if (methodName == "replace") {
            if (args.size() < 2) throw ScriptTrap("replace() takes at least 2 arguments", line, col);
            std::string oldSub = args.at(0).toString();
            std::string newSub = args.at(1).toString();
            if (oldSub.empty()) return *this;
            std::string res = s;
            size_t pos = 0;
            while ((pos = res.find(oldSub, pos)) != std::string::npos) {
                guard.alloc(newSub.size(), line, col);
                res.replace(pos, oldSub.length(), newSub);
                pos += newSub.length();
            }
            return ScriptValue(std::move(res));
        }
        if (methodName == "startswith") {
            if (args.empty()) throw ScriptTrap("startswith() requires 1 argument", line, col);
            std::string prefix = args.at(0).toString();
            return ScriptValue(s.rfind(prefix, 0) == 0);
        }
        if (methodName == "endswith") {
            if (args.empty()) throw ScriptTrap("endswith() requires 1 argument", line, col);
            std::string suffix = args.at(0).toString();
            if (s.size() < suffix.size()) return ScriptValue(false);
            return ScriptValue(s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
        }
        if (methodName == "find") {
            if (args.empty()) throw ScriptTrap("find() requires 1 argument", line, col);
            std::string sub = args.at(0).toString();
            size_t p = s.find(sub);
            return ScriptValue(p == std::string::npos ? -1 : static_cast<int>(p));
        }
        if (methodName == "count") {
            if (args.empty()) throw ScriptTrap("count() requires 1 argument", line, col);
            std::string sub = args.at(0).toString();
            if (sub.empty()) return ScriptValue(0);
            int64_t occurrences = 0;
            size_t pos = 0;
            while ((pos = s.find(sub, pos)) != std::string::npos) {
                occurrences++;
                pos += sub.length();
            }
            return ScriptValue(occurrences);
        }
        if (methodName == "lower") {
            std::string res = s;
            for (char &c : res) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            guard.alloc(res.capacity(), line, col);
            return ScriptValue(std::move(res));
        }
        if (methodName == "upper") {
            std::string res = s;
            for (char &c : res) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            guard.alloc(res.capacity(), line, col);
            return ScriptValue(std::move(res));
        }
    }

    // Built-in List methods
    if (isList()) {
        auto list = asList();
        if (methodName == "append") {
            if (args.empty()) throw ScriptTrap("append() requires 1 argument", line, col);
            const auto &item = args.at(0);
            guard.alloc(item.estimateBytes() + sizeof(ScriptValue), line, col);
            list->push_back(item);
            return ScriptValue();
        }
        if (methodName == "pop") {
            if (list->empty()) throw ScriptTrap("IndexError: pop from empty list", line, col);
            int64_t idx = static_cast<int64_t>(list->size()) - 1;
            if (!args.empty()) {
                idx = args.at(0).toInteger(line, col);
                if (idx < 0) idx += static_cast<int64_t>(list->size());
            }
            if (idx < 0 || idx >= static_cast<int64_t>(list->size())) {
                throw ScriptTrap("IndexError: pop index out of range", line, col);
            }
            ScriptValue val = list->at(static_cast<size_t>(idx));
            list->erase(list->begin() + idx);
            return val;
        }
        if (methodName == "clear") {
            list->clear();
            return ScriptValue();
        }
        if (methodName == "len") {
            return ScriptValue(static_cast<int64_t>(list->size()));
        }
    }

    // Built-in Dict methods
    if (isDict()) {
        auto dict = asDict();

        // If the dictionary contains a callable member, invoke it first
        auto itCallable = dict->find(methodName);
        if (itCallable != dict->end() && itCallable->second.isCallable()) {
            if (itCallable->second.type() == Type::NativeFunc) {
                return itCallable->second.asNativeFunc()(args, guard, line, col);
            }
        }

        if (methodName == "get") {
            if (args.empty()) throw ScriptTrap("get() requires at least 1 argument", line, col);
            std::string key = args.at(0).toString();
            auto it = dict->find(key);
            if (it != dict->end()) return it->second;
            return (args.size() > 1) ? args.at(1) : ScriptValue();
        }
        if (methodName == "keys") {
            auto keysList = std::make_shared<ScriptList>();
            guard.alloc(sizeof(ScriptList), line, col);
            for (const auto &pair : *dict) {
                guard.alloc(pair.first.capacity() + sizeof(ScriptValue), line, col);
                keysList->push_back(ScriptValue(pair.first));
            }
            return ScriptValue(keysList);
        }
        if (methodName == "values") {
            auto valList = std::make_shared<ScriptList>();
            guard.alloc(sizeof(ScriptList), line, col);
            for (const auto &pair : *dict) {
                guard.alloc(valList->capacity() + sizeof(ScriptValue), line, col);
                valList->push_back(pair.second);
            }
            return ScriptValue(valList);
        }
        if (methodName == "clear") {
            dict->clear();
            return ScriptValue();
        }
        if (methodName == "len") {
            return ScriptValue(static_cast<int64_t>(dict->size()));
        }

        // If the dictionary contains a callable member
        auto it = dict->find(methodName);
        if (it != dict->end() && it->second.isCallable()) {
            if (it->second.type() == Type::NativeFunc) {
                return it->second.asNativeFunc()(args, guard, line, col);
            }
        }
    }

    throw ScriptTrap("AttributeError: '" + typeName() + "' object has no method '" + methodName + "'", line, col);
}

ScriptValue ScriptValue::add(const ScriptValue &other, ResourceGuard &guard, int line, int col) const {
    if (isNumber() && other.isNumber()) {
        return ScriptValue(asNumber() + other.asNumber());
    }
    if (isString() && other.isString()) {
        const auto &s1 = asString();
        const auto &s2 = other.asString();
        size_t totalBytes = s1.size() + s2.size();
        guard.alloc(totalBytes + sizeof(std::string), line, col);
        return ScriptValue(s1 + s2);
    }
    if (isList() && other.isList()) {
        auto l1 = asList();
        auto l2 = other.asList();
        size_t totalCount = l1->size() + l2->size();
        guard.alloc(totalCount * sizeof(ScriptValue) + sizeof(ScriptList), line, col);
        auto res = std::make_shared<ScriptList>();
        res->reserve(totalCount);
        res->insert(res->end(), l1->begin(), l1->end());
        res->insert(res->end(), l2->begin(), l2->end());
        return ScriptValue(res);
    }
    throw ScriptTrap("TypeError: unsupported operand type(s) for +: '" + typeName() + "' and '" + other.typeName() + "'", line, col);
}

ScriptValue ScriptValue::subtract(const ScriptValue &other, ResourceGuard &guard, int line, int col) const {
    if (isNumber() && other.isNumber()) {
        return ScriptValue(asNumber() - other.asNumber());
    }
    throw ScriptTrap("TypeError: unsupported operand type(s) for -: '" + typeName() + "' and '" + other.typeName() + "'", line, col);
}

ScriptValue ScriptValue::multiply(const ScriptValue &other, ResourceGuard &guard, int line, int col) const {
    if (isNumber() && other.isNumber()) {
        return ScriptValue(asNumber() * other.asNumber());
    }
    if (isString() && other.isNumber()) {
        const std::string &s = asString();
        int64_t count = other.toInteger(line, col);
        if (count <= 0) return ScriptValue("");
        uint64_t totalBytes = static_cast<uint64_t>(s.size()) * static_cast<uint64_t>(count);
        guard.alloc(static_cast<size_t>(totalBytes) + sizeof(std::string), line, col);
        std::string res;
        res.reserve(static_cast<size_t>(totalBytes));
        for (int64_t i = 0; i < count; ++i) {
            res += s;
        }
        return ScriptValue(std::move(res));
    }
    if (isList() && other.isNumber()) {
        auto l = asList();
        int64_t count = other.toInteger(line, col);
        if (count <= 0) return ScriptValue::makeList(guard, line, col);
        uint64_t totalCount = static_cast<uint64_t>(l->size()) * static_cast<uint64_t>(count);
        guard.alloc(static_cast<size_t>(totalCount * sizeof(ScriptValue)) + sizeof(ScriptList), line, col);
        auto res = std::make_shared<ScriptList>();
        res->reserve(static_cast<size_t>(totalCount));
        for (int64_t i = 0; i < count; ++i) {
            res->insert(res->end(), l->begin(), l->end());
        }
        return ScriptValue(res);
    }
    throw ScriptTrap("TypeError: unsupported operand type(s) for *: '" + typeName() + "' and '" + other.typeName() + "'", line, col);
}

ScriptValue ScriptValue::divide(const ScriptValue &other, ResourceGuard &guard, int line, int col) const {
    if (isNumber() && other.isNumber()) {
        double d = other.asNumber();
        if (d == 0.0) {
            throw ScriptTrap("ZeroDivisionError: division by zero", line, col);
        }
        return ScriptValue(asNumber() / d);
    }
    throw ScriptTrap("TypeError: unsupported operand type(s) for /: '" + typeName() + "' and '" + other.typeName() + "'", line, col);
}

ScriptValue ScriptValue::modulo(const ScriptValue &other, ResourceGuard &guard, int line, int col) const {
    if (isNumber() && other.isNumber()) {
        double d = other.asNumber();
        if (d == 0.0) {
            throw ScriptTrap("ZeroDivisionError: modulo by zero", line, col);
        }
        return ScriptValue(std::fmod(asNumber(), d));
    }
    throw ScriptTrap("TypeError: unsupported operand type(s) for %: '" + typeName() + "' and '" + other.typeName() + "'", line, col);
}

ScriptValue ScriptValue::unaryMinus(ResourceGuard &guard, int line, int col) const {
    if (isNumber()) {
        return ScriptValue(-asNumber());
    }
    throw ScriptTrap("TypeError: bad operand type for unary -: '" + typeName() + "'", line, col);
}

bool ScriptValue::equals(const ScriptValue &other) const {
    if (type() != other.type()) return false;
    switch (type()) {
    case Type::None: return true;
    case Type::Bool: return std::get<bool>(m_data) == std::get<bool>(other.m_data);
    case Type::Number: return std::get<double>(m_data) == std::get<double>(other.m_data);
    case Type::String: return std::get<std::string>(m_data) == std::get<std::string>(other.m_data);
    case Type::List: {
        auto l1 = std::get<std::shared_ptr<ScriptList>>(m_data);
        auto l2 = std::get<std::shared_ptr<ScriptList>>(other.m_data);
        if (l1 == l2) return true;
        if (!l1 || !l2 || l1->size() != l2->size()) return false;
        for (size_t i = 0; i < l1->size(); ++i) {
            if (!l1->at(i).equals(l2->at(i))) return false;
        }
        return true;
    }
    case Type::Dict: {
        auto d1 = std::get<std::shared_ptr<ScriptDict>>(m_data);
        auto d2 = std::get<std::shared_ptr<ScriptDict>>(other.m_data);
        if (d1 == d2) return true;
        if (!d1 || !d2 || d1->size() != d2->size()) return false;
        for (const auto &pair : *d1) {
            auto it = d2->find(pair.first);
            if (it == d2->end() || !pair.second.equals(it->second)) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

bool ScriptValue::lessThan(const ScriptValue &other, int line, int col) const {
    if (isNumber() && other.isNumber()) {
        return asNumber() < other.asNumber();
    }
    if (isString() && other.isString()) {
        return asString() < other.asString();
    }
    throw ScriptTrap("TypeError: '<' not supported between instances of '" + typeName() + "' and '" + other.typeName() + "'", line, col);
}

bool ScriptValue::contains(const ScriptValue &item, ResourceGuard &guard, int line, int col) const {
    if (isString()) {
        std::string sub = item.toString();
        return asString().find(sub) != std::string::npos;
    }
    if (isList()) {
        auto list = asList();
        if (!list) return false;
        for (const auto &elem : *list) {
            if (elem.equals(item)) return true;
        }
        return false;
    }
    if (isDict()) {
        auto dict = asDict();
        if (!dict) return false;
        std::string key = item.toString();
        return dict->find(key) != dict->end();
    }
    throw ScriptTrap("TypeError: argument of type '" + typeName() + "' is not iterable", line, col);
}

} // namespace EnsemblePlugin
