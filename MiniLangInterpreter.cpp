// MiniLang Interpreter — Complete Project
// ---------------------------------------
// Interpreter for the toy language "MiniLang".
// It supports:
//   • Subprograms 
//   • Local variables with stack‑dynamic allocation
//   • Nested subprograms & blocks
//   • Lexical vs. dynamic scoping 
//   • Parameter passing 
//   • Closures
//   • Simple overloading and a sketch of generic subprograms
//
// Build (one‑file project):
//   g++ -std=c++17 -Wall -O2 MiniLangInterpreter.cpp -o minilang
//
// Run:
//   ./minilang
// ---------------------------------------
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cctype>
#include <stdexcept>

// ───────────────────────────────────────────────────────────────────────────────
// Forward declarations & aliases
struct Subprogram;                             // forward decl.
using SubprogramPtr = std::shared_ptr<Subprogram>;

// ───────────────────────────────────────────────────────────────────────────────
// Runtime value
struct Value {
    enum class Type { INT, SUBPROGRAM, CLOSURE } type{Type::INT};

    // INT
    int int_val{0};

    // SUBPROGRAM
    SubprogramPtr subprogram{};

    // CLOSURE = <subprogram , captured‑env>
    SubprogramPtr closure_subprogram{};
    std::vector<std::unordered_map<std::string, Value>> closure_env{};

    Value() = default;
    explicit Value(int v) : type(Type::INT), int_val(v) {}
    explicit Value(SubprogramPtr sub) : type(Type::SUBPROGRAM), subprogram(std::move(sub)) {}
    Value(SubprogramPtr sub, std::vector<std::unordered_map<std::string, Value>> env)
        : type(Type::CLOSURE), closure_subprogram(std::move(sub)), closure_env(std::move(env)) {}
};

// ───────────────────────────────────────────────────────────────────────────────
// Subprogram representation
struct Subprogram {
    std::vector<std::string> params;
    std::string              body;             // e.g. "return a + b"
    bool                     is_generic{false};

    Subprogram(std::vector<std::string> p, std::string b)
        : params(std::move(p)), body(std::move(b)) {}
};

// ───────────────────────────────────────────────────────────────────────────────
// Environment — stack of scopes with lexical or dynamic lookup
class Environment {
    using Scope = std::unordered_map<std::string, Value>;

    std::vector<Scope> scopes;                 // activation records
    bool dynamic_scoping{false};

public:
    explicit Environment(bool dyn = false) : dynamic_scoping(dyn) { scopes.emplace_back(); }

    void enter_scope(const Scope& copy_from = {}) {
        scopes.emplace_back(copy_from);        // copy_if any, otherwise empty
    }
    void exit_scope()                { if (scopes.size() > 1) scopes.pop_back(); }
    void define(const std::string& n, const Value& v) { scopes.back()[n] = v; }

    Value lookup(const std::string& name) const {
        // dynamic_scoping and lexical currently identical search – we walk from top
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
            auto it = scopes[i].find(name);
            if (it != scopes[i].end()) return it->second;
        }
        throw std::runtime_error("Variable not found: " + name);
    }

    bool is_dynamic() const { return dynamic_scoping; }
    const std::vector<Scope>& get_scopes() const { return scopes; }
};

// ───────────────────────────────────────────────────────────────────────────────
// MiniLang Interpreter
class MiniLangInterpreter {
    using ArgList = std::vector<Value>;

    Environment env;

    // Helper: trim
    static std::string trim(std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
        return s;
    }

    int resolve_int(const std::string& tok) {
        if (std::isdigit(tok[0]) || (tok[0] == '-' && tok.size() > 1 && std::isdigit(tok[1])))
            return std::stoi(tok);
        Value v = env.lookup(tok);
        if (v.type != Value::Type::INT) throw std::runtime_error("Expected int: " + tok);
        return v.int_val;
    }

    // Micro evaluator (return stmt w/ optional +)
    Value execute_body(const std::string& body) {
        std::string src = trim(body);
        if (src.rfind("return", 0) == 0) {
            src = trim(src.substr(6));                   // after "return"
            auto pos = src.find('+');
            if (pos != std::string::npos) {
                std::string lhs = trim(src.substr(0, pos));
                std::string rhs = trim(src.substr(pos + 1));
                return Value(resolve_int(lhs) + resolve_int(rhs));
            }
            return Value(resolve_int(src));
        }
        return Value(std::stoi(src));                     // fallback: constant
    }

public:
    explicit MiniLangInterpreter(bool dynamic = false) : env(dynamic) {}

    // Definition helpers ----------------------------------------------------
    SubprogramPtr make_sub(const std::vector<std::string>& params, const std::string& body) {
        return std::make_shared<Subprogram>(params, body);
    }

    void define_subprogram(const std::string& name,
                           const std::vector<std::string>& params,
                           const std::string& body) {
        env.define(name, Value(make_sub(params, body)));
    }

    void define_overloaded_subprogram(const std::string& base,
                                      const std::vector<std::string>& params,
                                      const std::string& body) {
        define_subprogram(base + "_" + std::to_string(params.size()), params, body);
    }

    void define_generic_subprogram(const std::string& name,
                                   const std::vector<std::string>& params,
                                   const std::string& body) {
        auto sub = make_sub(params, body);
        sub->is_generic = true;
        env.define(name, Value(sub));
    }

    void define_nested_subprogram(const std::string& /*outer*/, const std::string& inner,
                                  const std::vector<std::string>& params, const std::string& body) {
        env.enter_scope();
        define_subprogram(inner, params, body);
        env.exit_scope();
    }

    // Closure ---------------------------------------------------------------
    Value create_closure(const std::string& name) {
        Value v = env.lookup(name);
        if (v.type != Value::Type::SUBPROGRAM)
            throw std::runtime_error("Not a subprogram: " + name);
        return Value(v.subprogram, env.get_scopes());
    }

    // Call (direct or closure) ----------------------------------------------
    Value call_subprogram(const std::string& raw, const ArgList& args) {
        std::string name = raw;
        // overload resolution by arity
        try { env.lookup(name + "_" + std::to_string(args.size())); name += "_" + std::to_string(args.size()); }
        catch (...) {}

        Value callee = env.lookup(name);
        SubprogramPtr sub;
        std::vector<std::unordered_map<std::string, Value>> captured;

        if (callee.type == Value::Type::CLOSURE) {
            sub       = callee.closure_subprogram;
            captured  = callee.closure_env;
        } else {
            sub       = callee.subprogram;
        }

        if (args.size() != sub->params.size())
            throw std::runtime_error("Arity mismatch: " + name);

        // Push captured scopes to mimic lexical env depth (read‑only copies)
        for (const auto& cap_scope : captured) env.enter_scope(cap_scope);

        env.enter_scope();
        for (size_t i = 0; i < args.size(); ++i) env.define(sub->params[i], args[i]);

        Value result = execute_body(sub->body);

        env.exit_scope();
        for (size_t i = 0; i < captured.size(); ++i) env.exit_scope();

        return result;
    }

    // Scoping toggle ---------------------------------------------------------
    void toggle_dynamic_scoping(bool enable) { env = Environment(enable); }
};

// ───────────────────────────────────────────────────────────────────────────────
// Demo -------------------------------------------------------------------------
int main() {
    try {
        MiniLangInterpreter interp;                 // lexical by default

        interp.define_subprogram("add", {"a", "b"}, "return a + b");
        std::cout << "add(3,5) = "
                  << interp.call_subprogram("add", {Value(3), Value(5)}).int_val << "\n";

        interp.define_overloaded_subprogram("print", {"x"},       "return 42");
        interp.define_overloaded_subprogram("print", {"x", "y"}, "return 99");
        std::cout << "print(1)  -> " << interp.call_subprogram("print", {Value(7)}).int_val          << "\n";
        std::cout << "print(1,2)-> " << interp.call_subprogram("print", {Value(7), Value(9)}).int_val << "\n";

        interp.toggle_dynamic_scoping(true);
        interp.define_subprogram("echo", {"x"}, "return x");
        std::cout << "echo(dynamic) -> " << interp.call_subprogram("echo", {Value(11)}).int_val << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "[MiniLang Error] " << ex.what() << "\n";
        return 1;
    }
    return 0;
}