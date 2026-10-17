#pragma once

#include "ResourceGuard.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace EnsemblePlugin {

class ScriptValue;
struct AstNode;
class Environment;

using ScriptList = std::vector<ScriptValue>;
using ScriptDict = std::map<std::string, ScriptValue>;

using NativeFuncSignature = std::function<ScriptValue(
    const std::vector<ScriptValue> &args,
    ResourceGuard &guard,
    int line,
    int col)>;

struct ScriptFunction {
    std::string name;
    std::vector<std::string> parameters;
    std::shared_ptr<AstNode> body;
    std::shared_ptr<Environment> closure;
    std::vector<std::string> decorators;
};

/**
 * @brief Sandboxed value representation for the Python-like DSL.
 * Supports None, Bool, Number (double), String, List, Dict, NativeFunction, and UserFunction.
 * Completely bounds-checked and tracked by ResourceGuard.
 */
class ScriptValue {
public:
    enum class Type {
        None,
        Bool,
        Number,
        String,
        List,
        Dict,
        NativeFunc,
        UserFunc
    };

    ScriptValue(); // None
    explicit ScriptValue(bool val);
    explicit ScriptValue(double val);
    explicit ScriptValue(int val);
    explicit ScriptValue(int64_t val);
    explicit ScriptValue(const char *str);
    explicit ScriptValue(std::string str);
    explicit ScriptValue(std::shared_ptr<ScriptList> list);
    explicit ScriptValue(std::shared_ptr<ScriptDict> dict);
    explicit ScriptValue(NativeFuncSignature nativeFunc, std::string funcName = "<native>");
    explicit ScriptValue(ScriptFunction userFunc);

    static ScriptValue makeList(ResourceGuard &guard, int line = -1, int col = -1);
    static ScriptValue makeDict(ResourceGuard &guard, int line = -1, int col = -1);

    Type type() const noexcept;
    std::string typeName() const;

    bool isNone() const noexcept;
    bool isBool() const noexcept;
    bool isNumber() const noexcept;
    bool isString() const noexcept;
    bool isList() const noexcept;
    bool isDict() const noexcept;
    bool isCallable() const noexcept;

    bool toBool() const;
    double toNumber(int line = -1, int col = -1) const;
    int64_t toInteger(int line = -1, int col = -1) const;
    std::string toString() const;

    size_t estimateBytes() const noexcept;

    // Direct accessors (with bounds/type verification)
    bool asBool() const;
    double asNumber() const;
    const std::string &asString() const;
    std::shared_ptr<ScriptList> asList() const;
    std::shared_ptr<ScriptDict> asDict() const;
    const ScriptFunction &asUserFunc() const;
    const NativeFuncSignature &asNativeFunc() const;

    // Subscript access: val[index] or val[key]
    ScriptValue getItem(const ScriptValue &indexOrKey, ResourceGuard &guard, int line = -1, int col = -1) const;
    void setItem(const ScriptValue &indexOrKey, const ScriptValue &value, ResourceGuard &guard, int line = -1, int col = -1);
    bool delItem(const ScriptValue &indexOrKey, ResourceGuard &guard, int line = -1, int col = -1);

    // Member access: val.identifier
    ScriptValue getMember(const std::string &memberName, ResourceGuard &guard, int line = -1, int col = -1) const;
    void setMember(const std::string &memberName, const ScriptValue &value, ResourceGuard &guard, int line = -1, int col = -1);

    // Call method or built-in member: val.method(args)
    ScriptValue callMember(const std::string &methodName, const std::vector<ScriptValue> &args, ResourceGuard &guard, int line = -1, int col = -1) const;

    // Binary arithmetic & logic
    ScriptValue add(const ScriptValue &other, ResourceGuard &guard, int line = -1, int col = -1) const;
    ScriptValue subtract(const ScriptValue &other, ResourceGuard &guard, int line = -1, int col = -1) const;
    ScriptValue multiply(const ScriptValue &other, ResourceGuard &guard, int line = -1, int col = -1) const;
    ScriptValue divide(const ScriptValue &other, ResourceGuard &guard, int line = -1, int col = -1) const;
    ScriptValue modulo(const ScriptValue &other, ResourceGuard &guard, int line = -1, int col = -1) const;
    ScriptValue unaryMinus(ResourceGuard &guard, int line = -1, int col = -1) const;

    // Comparisons
    bool equals(const ScriptValue &other) const;
    bool lessThan(const ScriptValue &other, int line = -1, int col = -1) const;
    bool contains(const ScriptValue &item, ResourceGuard &guard, int line = -1, int col = -1) const;

private:
    struct NativeFuncWrapper {
        NativeFuncSignature func;
        std::string name;
    };

    using ValueVariant = std::variant<
        std::monostate,
        bool,
        double,
        std::string,
        std::shared_ptr<ScriptList>,
        std::shared_ptr<ScriptDict>,
        NativeFuncWrapper,
        ScriptFunction
    >;

    ValueVariant m_data;
};

} // namespace EnsemblePlugin
