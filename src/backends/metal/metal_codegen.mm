//
// Created by Mike Smith on 2021/3/25.
//

#import <span>

#import <core/hash.h>
#import <ast/type_registry.h>
#import <ast/function_builder.h>
#import <ast/constant_data.h>
#import <backends/metal/metal_codegen.h>

namespace luisa::compute::metal {

void MetalCodegen::visit(const UnaryExpr *expr) {
    switch (expr->op()) {
        case UnaryOp::PLUS: _scratch << "+"; break;
        case UnaryOp::MINUS: _scratch << "-"; break;
        case UnaryOp::NOT: _scratch << "!"; break;
        case UnaryOp::BIT_NOT: _scratch << "~"; break;
    }
    expr->operand()->accept(*this);
}

void MetalCodegen::visit(const BinaryExpr *expr) {
    _scratch << "(";
    expr->lhs()->accept(*this);
    switch (expr->op()) {
        case BinaryOp::ADD: _scratch << " + "; break;
        case BinaryOp::SUB: _scratch << " - "; break;
        case BinaryOp::MUL: _scratch << " * "; break;
        case BinaryOp::DIV: _scratch << " / "; break;
        case BinaryOp::MOD: _scratch << " % "; break;
        case BinaryOp::BIT_AND: _scratch << " & "; break;
        case BinaryOp::BIT_OR: _scratch << " | "; break;
        case BinaryOp::BIT_XOR: _scratch << " ^ "; break;
        case BinaryOp::SHL: _scratch << " << "; break;
        case BinaryOp::SHR: _scratch << " >> "; break;
        case BinaryOp::AND: _scratch << " && "; break;
        case BinaryOp::OR: _scratch << " || "; break;
        case BinaryOp::LESS: _scratch << " < "; break;
        case BinaryOp::GREATER: _scratch << " > "; break;
        case BinaryOp::LESS_EQUAL: _scratch << " <= "; break;
        case BinaryOp::GREATER_EQUAL: _scratch << " >= "; break;
        case BinaryOp::EQUAL: _scratch << " == "; break;
        case BinaryOp::NOT_EQUAL: _scratch << " != "; break;
    }
    expr->rhs()->accept(*this);
    _scratch << ")";
}

void MetalCodegen::visit(const MemberExpr *expr) {
    expr->self()->accept(*this);
    if (expr->is_swizzle()) {
        static constexpr std::string_view xyzw[]{"x", "y", "z", "w"};
        _scratch << ".";
        for (auto i = 0u; i < expr->swizzle_size(); i++) {
            _scratch << xyzw[expr->swizzle_index(i)];
        }
    } else {
        _scratch << ".m" << expr->member_index();
    }
}

void MetalCodegen::visit(const AccessExpr *expr) {
    expr->range()->accept(*this);
    _scratch << "[";
    expr->index()->accept(*this);
    _scratch << "]";
}

namespace detail {

class LiteralPrinter {

private:
    Codegen::Scratch &_s;

public:
    explicit LiteralPrinter(Codegen::Scratch &s) noexcept : _s{s} {}
    void operator()(bool v) const noexcept { _s << v; }
    void operator()(float v) const noexcept {
        if (std::isnan(v)) [[unlikely]] { LUISA_ERROR_WITH_LOCATION("Encountered with NaN."); }
        if (std::isinf(v)) {
            _s << (v < 0.0f ? "(-INFINITY)" : "(+INFINITY)");
        } else {
            _s << v << "f";
        }
    }
    void operator()(int v) const noexcept { _s << v; }
    void operator()(uint v) const noexcept { _s << v << "u"; }

    template<typename T, size_t N>
    void operator()(Vector<T, N> v) const noexcept {
        auto t = Type::of<T>();
        _s << t->description() << N << "(";
        for (auto i = 0u; i < N; i++) {
            (*this)(v[i]);
            _s << ", ";
        }
        _s.pop_back();
        _s.pop_back();
        _s << ")";
    }

    void operator()(float2x2 m) const noexcept {
        _s << "float2x2(";
        for (auto col = 0u; col < 2u; col++) {
            for (auto row = 0u; row < 2u; row++) {
                (*this)(m[col][row]);
                _s << ", ";
            }
        }
        _s.pop_back();
        _s.pop_back();
        _s << ")";
    }

    void operator()(float3x3 m) const noexcept {
        _s << "float3x3(";
        for (auto col = 0u; col < 3u; col++) {
            for (auto row = 0u; row < 3u; row++) {
                (*this)(m[col][row]);
                _s << ", ";
            }
        }
        _s.pop_back();
        _s.pop_back();
        _s << ")";
    }

    void operator()(float4x4 m) const noexcept {
        _s << "float4x4(";
        for (auto col = 0u; col < 4u; col++) {
            for (auto row = 0u; row < 4u; row++) {
                (*this)(m[col][row]);
            }
        }
        _s << ")";
    }
};

}// namespace detail

void MetalCodegen::visit(const LiteralExpr *expr) {
    std::visit(detail::LiteralPrinter{_scratch}, expr->value());
}

void MetalCodegen::visit(const RefExpr *expr) {
    auto v = expr->variable();
    if (_function.tag() == Function::Tag::KERNEL
        && (v.tag() == Variable::Tag::UNIFORM
            || v.tag() == Variable::Tag::BUFFER
            || v.tag() == Variable::Tag::TEXTURE
            || v.tag() == Variable::Tag::TEXTURE_HEAP
            || v.tag() == Variable::Tag::DISPATCH_SIZE)) {
        _scratch << "arg.";
    }
    _emit_variable_name(expr->variable());
}

void MetalCodegen::visit(const CallExpr *expr) {
    auto is_atomic_op = false;
    switch (expr->op()) {
        case CallOp::CUSTOM: _scratch << "custom_" << hash_to_string(expr->custom().hash()); break;
        case CallOp::ALL: _scratch << "all"; break;
        case CallOp::ANY: _scratch << "any"; break;
        case CallOp::NONE: _scratch << "none"; break;
        case CallOp::SELECT: _scratch << "select"; break;
        case CallOp::CLAMP: _scratch << "clamp"; break;
        case CallOp::LERP: _scratch << "mix"; break;
        case CallOp::SATURATE: _scratch << "saturate"; break;
        case CallOp::SIGN: _scratch << "sign"; break;
        case CallOp::STEP: _scratch << "step"; break;
        case CallOp::SMOOTHSTEP: _scratch << "smoothstep"; break;
        case CallOp::ABS: _scratch << "abs"; break;
        case CallOp::MIN: _scratch << "min"; break;
        case CallOp::MAX: _scratch << "max"; break;
        case CallOp::CLZ: _scratch << "clz"; break;
        case CallOp::CTZ: _scratch << "ctz"; break;
        case CallOp::POPCOUNT: _scratch << "popcount"; break;
        case CallOp::REVERSE: _scratch << "reverse_bits"; break;
        case CallOp::ISINF: _scratch << "precise::isinf"; break;
        case CallOp::ISNAN: _scratch << "precise::isnan"; break;
        case CallOp::ACOS: _scratch << "acos"; break;
        case CallOp::ACOSH: _scratch << "acosh"; break;
        case CallOp::ASIN: _scratch << "asin"; break;
        case CallOp::ASINH: _scratch << "asinh"; break;
        case CallOp::ATAN: _scratch << "atan"; break;
        case CallOp::ATAN2: _scratch << "atan2"; break;
        case CallOp::ATANH: _scratch << "atanh"; break;
        case CallOp::COS: _scratch << "cos"; break;
        case CallOp::COSH: _scratch << "cosh"; break;
        case CallOp::SIN: _scratch << "sin"; break;
        case CallOp::SINH: _scratch << "sinh"; break;
        case CallOp::TAN: _scratch << "tan"; break;
        case CallOp::TANH: _scratch << "tanh"; break;
        case CallOp::EXP: _scratch << "exp"; break;
        case CallOp::EXP2: _scratch << "exp2"; break;
        case CallOp::EXP10: _scratch << "exp10"; break;
        case CallOp::LOG: _scratch << "log"; break;
        case CallOp::LOG2: _scratch << "log2"; break;
        case CallOp::LOG10: _scratch << "log10"; break;
        case CallOp::POW: _scratch << "pow"; break;
        case CallOp::SQRT: _scratch << "sqrt"; break;
        case CallOp::RSQRT: _scratch << "rsqrt"; break;
        case CallOp::CEIL: _scratch << "ceil"; break;
        case CallOp::FLOOR: _scratch << "floor"; break;
        case CallOp::FRACT: _scratch << "fract"; break;
        case CallOp::TRUNC: _scratch << "trunc"; break;
        case CallOp::ROUND: _scratch << "round"; break;
        case CallOp::MOD: _scratch << "glsl_mod"; break;
        case CallOp::FMOD: _scratch << "fmod"; break;
        case CallOp::DEGREES: _scratch << "degrees"; break;
        case CallOp::RADIANS: _scratch << "radians"; break;
        case CallOp::FMA: _scratch << "fma"; break;
        case CallOp::COPYSIGN: _scratch << "copysign"; break;
        case CallOp::CROSS: _scratch << "cross"; break;
        case CallOp::DOT: _scratch << "dot"; break;
        case CallOp::DISTANCE: _scratch << "distance"; break;
        case CallOp::DISTANCE_SQUARED: _scratch << "distance_squared"; break;
        case CallOp::LENGTH: _scratch << "length"; break;
        case CallOp::LENGTH_SQUARED: _scratch << "length_squared"; break;
        case CallOp::NORMALIZE: _scratch << "normalize"; break;
        case CallOp::FACEFORWARD: _scratch << "faceforward"; break;
        case CallOp::DETERMINANT: _scratch << "determinant"; break;
        case CallOp::TRANSPOSE: _scratch << "transpose"; break;
        case CallOp::INVERSE: _scratch << "inverse"; break;
        case CallOp::GROUP_MEMORY_BARRIER: _scratch << "group_memory_barrier"; break;
        case CallOp::DEVICE_MEMORY_BARRIER: _scratch << "device_memory_barrier"; break;
        case CallOp::ALL_MEMORY_BARRIER: _scratch << "all_memory_barrier"; break;
        case CallOp::ATOMIC_LOAD:
            _scratch << "atomic_load_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_STORE:
            _scratch << "atomic_store_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_EXCHANGE:
            _scratch << "atomic_exchange_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_COMPARE_EXCHANGE:
            _scratch << "atomic_compare_exchange";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_FETCH_ADD:
            _scratch << "atomic_fetch_add_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_FETCH_SUB:
            _scratch << "atomic_fetch_sub_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_FETCH_AND:
            _scratch << "atomic_fetch_and_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_FETCH_OR:
            _scratch << "atomic_fetch_or_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_FETCH_XOR:
            _scratch << "atomic_fetch_xor_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_FETCH_MIN:
            _scratch << "atomic_fetch_min_explicit";
            is_atomic_op = true;
            break;
        case CallOp::ATOMIC_FETCH_MAX:
            _scratch << "atomic_fetch_max_explicit";
            is_atomic_op = true;
            break;
        case CallOp::TEXTURE_READ: _scratch << "texture_read"; break;
        case CallOp::TEXTURE_WRITE: _scratch << "texture_write"; break;
        case CallOp::TEXTURE_SAMPLE: _scratch << "texture_sample"; break;
        case CallOp::TEXTURE_SAMPLE_LOD: _scratch << "texture_sample_lod"; break;
        case CallOp::TEXTURE_SAMPLE_GRAD: _scratch << "texture_sample_grad"; break;
        case CallOp::MAKE_BOOL2: _scratch << "bool2"; break;
        case CallOp::MAKE_BOOL3: _scratch << "bool3"; break;
        case CallOp::MAKE_BOOL4: _scratch << "bool4"; break;
        case CallOp::MAKE_INT2: _scratch << "int2"; break;
        case CallOp::MAKE_INT3: _scratch << "int3"; break;
        case CallOp::MAKE_INT4: _scratch << "int4"; break;
        case CallOp::MAKE_UINT2: _scratch << "uint2"; break;
        case CallOp::MAKE_UINT3: _scratch << "uint3"; break;
        case CallOp::MAKE_UINT4: _scratch << "uint4"; break;
        case CallOp::MAKE_FLOAT2: _scratch << "float2"; break;
        case CallOp::MAKE_FLOAT3: _scratch << "float3"; break;
        case CallOp::MAKE_FLOAT4: _scratch << "float4"; break;
        case CallOp::MAKE_FLOAT2X2: _scratch << "float2x2"; break;
        case CallOp::MAKE_FLOAT3X3: _scratch << "float3x3"; break;
        case CallOp::MAKE_FLOAT4X4: _scratch << "float4x4"; break;
        case CallOp::TRACE_CLOSEST: break;
        case CallOp::TRACE_ANY: break;
    }

    _scratch << "(";
    if (is_atomic_op) {
        _scratch << "as_atomic(";
        auto args = expr->arguments();
        args[0]->accept(*this);
        _scratch << "), ";
        for (auto i = 1u; i < args.size(); i++) {
            args[i]->accept(*this);
            _scratch << ", ";
        }
        _scratch << "memory_order_relaxed";
    } else if (!expr->arguments().empty()) {
        for (auto arg : expr->arguments()) {
            arg->accept(*this);
            _scratch << ", ";
        }
        _scratch.pop_back();
        _scratch.pop_back();
    }
    _scratch << ")";
}

void MetalCodegen::visit(const CastExpr *expr) {
    switch (expr->op()) {
        case CastOp::STATIC:
            _scratch << "static_cast<";
            _emit_type_name(expr->type());
            _scratch << ">(";
            break;
        case CastOp::BITWISE:
            _scratch << "as<";
            _emit_type_name(expr->type());
            _scratch << ">(";
            break;
    }
    expr->expression()->accept(*this);
    _scratch << ")";
}

void MetalCodegen::visit(const BreakStmt *) {
    _scratch << "break;";
}

void MetalCodegen::visit(const ContinueStmt *) {
    _scratch << "continue;";
}

void MetalCodegen::visit(const ReturnStmt *stmt) {
    _scratch << "return";
    if (auto expr = stmt->expression(); expr != nullptr) {
        _scratch << " ";
        expr->accept(*this);
    }
    _scratch << ";";
}

void MetalCodegen::visit(const ScopeStmt *stmt) {
    _scratch << "{";
    _emit_statements(stmt->statements());
    _scratch << "}";
}

void MetalCodegen::visit(const DeclareStmt *stmt) {
    auto v = stmt->variable();
    _scratch << "auto ";
    _emit_variable_name(v);
    _scratch << " = ";
    _emit_type_name(v.type());
    _scratch << (v.type()->is_structure() ? "{" : "(");
    if (!stmt->initializer().empty()) {
        for (auto init : stmt->initializer()) {
            init->accept(*this);
            _scratch << ", ";
        }
        _scratch.pop_back();
        _scratch.pop_back();
    }
    _scratch << (v.type()->is_structure() ? "};" : ");");
}

void MetalCodegen::visit(const IfStmt *stmt) {
    _scratch << "if (";
    stmt->condition()->accept(*this);
    _scratch << ") ";
    stmt->true_branch()->accept(*this);
    if (auto fb = stmt->false_branch(); fb != nullptr && !fb->statements().empty()) {
        _scratch << " else ";
        if (auto elif = dynamic_cast<const IfStmt *>(fb->statements().front());
            fb->statements().size() == 1u && elif != nullptr) {
            elif->accept(*this);
        } else {
            fb->accept(*this);
        }
    }
}

void MetalCodegen::visit(const WhileStmt *stmt) {
    _scratch << "while (";
    stmt->condition()->accept(*this);
    _scratch << ") ";
    stmt->body()->accept(*this);
}

void MetalCodegen::visit(const ExprStmt *stmt) {
    stmt->expression()->accept(*this);
    _scratch << ";";
}

void MetalCodegen::visit(const SwitchStmt *stmt) {
    _scratch << "switch (";
    stmt->expression()->accept(*this);
    _scratch << ") ";
    stmt->body()->accept(*this);
}

void MetalCodegen::visit(const SwitchCaseStmt *stmt) {
    _scratch << "case ";
    stmt->expression()->accept(*this);
    _scratch << ": ";
    stmt->body()->accept(*this);
}

void MetalCodegen::visit(const SwitchDefaultStmt *stmt) {
    _scratch << "default: ";
    stmt->body()->accept(*this);
}

void MetalCodegen::visit(const AssignStmt *stmt) {
    stmt->lhs()->accept(*this);
    switch (stmt->op()) {
        case AssignOp::ASSIGN: _scratch << " = "; break;
        case AssignOp::ADD_ASSIGN: _scratch << " += "; break;
        case AssignOp::SUB_ASSIGN: _scratch << " -= "; break;
        case AssignOp::MUL_ASSIGN: _scratch << " *= "; break;
        case AssignOp::DIV_ASSIGN: _scratch << " /= "; break;
        case AssignOp::MOD_ASSIGN: _scratch << " %= "; break;
        case AssignOp::BIT_AND_ASSIGN: _scratch << " &= "; break;
        case AssignOp::BIT_OR_ASSIGN: _scratch << " |= "; break;
        case AssignOp::BIT_XOR_ASSIGN: _scratch << " ^= "; break;
        case AssignOp::SHL_ASSIGN: _scratch << " <<= "; break;
        case AssignOp::SHR_ASSIGN: _scratch << " >>= "; break;
    }
    stmt->rhs()->accept(*this);
    _scratch << ";";
}

void MetalCodegen::emit(Function f) {
    _emit_preamble();
    _emit_type_decl();
    _emit_function(f);
}

void MetalCodegen::_emit_function(Function f) noexcept {

    if (std::find(_generated_functions.cbegin(), _generated_functions.cend(), f)
        != _generated_functions.cend()) { return; }

    _generated_functions.emplace_back(f);
    for (auto callable : f.custom_callables()) { _emit_function(callable); }

    _function = f;
    _indent = 0u;

    // constants
    if (!f.constants().empty()) {
        for (auto c : f.constants()) { _emit_constant(c); }
        _scratch << "\n";
    }

    if (f.tag() == Function::Tag::KERNEL) {

        // argument buffer
        _scratch << "struct Argument {";
        for (auto buffer : f.captured_buffers()) {
            _scratch << "\n  ";
            _emit_variable_decl(buffer.variable);
            _scratch << ";";
        }
        for (auto image : f.captured_textures()) {
            _scratch << "\n  ";
            _emit_variable_decl(image.variable);
            _scratch << ";";
        }
        for (auto arg : f.arguments()) {
            _scratch << "\n  ";
            _emit_variable_decl(arg);
            _scratch << ";";
        }
        _scratch << "\n  const uint3 ls;\n};\n\n";

        // function signature
        _scratch << "[[kernel]] // block_size = ("
                 << f.block_size().x << ", "
                 << f.block_size().y << ", "
                 << f.block_size().z << ")\n"
                 << "void kernel_" << hash_to_string(f.hash())
                 << "(\n    device const Argument &arg,";
        for (auto builtin : f.builtin_variables()) {
            if (builtin.tag() != Variable::Tag::DISPATCH_SIZE) {
                _scratch << "\n    ";
                _emit_variable_decl(builtin);
                _scratch << ",";
            }
        }
        _scratch.pop_back();
    } else if (f.tag() == Function::Tag::CALLABLE) {
        if (f.return_type() != nullptr) {
            _emit_type_name(f.return_type());
        } else {
            _scratch << "void";
        }
        _scratch << " custom_" << hash_to_string(f.hash()) << "(";
        for (auto buffer : f.captured_buffers()) {
            _scratch << "\n    ";
            _emit_variable_decl(buffer.variable);
            _scratch << ",";
        }
        for (auto tex : f.captured_textures()) {
            _scratch << "\n    ";
            _emit_variable_decl(tex.variable);
            _scratch << ",";
        }
        for (auto arg : f.arguments()) {
            _scratch << "\n    ";
            _emit_variable_decl(arg);
            _scratch << ",";
        }
        if (!f.arguments().empty()
            || !f.captured_textures().empty()
            || !f.captured_buffers().empty()) {
            _scratch.pop_back();
        }
    } else [[unlikely]] {
        LUISA_ERROR_WITH_LOCATION("Invalid function type.");
    }
    _scratch << ") {";
    if (!f.shared_variables().empty()) {
        _scratch << "\n";
        for (auto s : f.shared_variables()) {
            _scratch << "\n  ";
            _emit_variable_decl(s);
            _scratch << ";";
        }
        _scratch << "\n";
    }
    _emit_statements(f.body()->statements());
    _scratch << "}\n\n";
}

void MetalCodegen::_emit_variable_name(Variable v) noexcept {
    switch (v.tag()) {
        case Variable::Tag::LOCAL: _scratch << "v" << v.uid(); break;
        case Variable::Tag::SHARED: _scratch << "s" << v.uid(); break;
        case Variable::Tag::UNIFORM: _scratch << "u" << v.uid(); break;
        case Variable::Tag::BUFFER: _scratch << "b" << v.uid(); break;
        case Variable::Tag::TEXTURE: _scratch << "i" << v.uid(); break;
        case Variable::Tag::TEXTURE_HEAP: _scratch << "h" << v.uid(); break;
        case Variable::Tag::THREAD_ID: _scratch << "tid"; break;
        case Variable::Tag::BLOCK_ID: _scratch << "bid"; break;
        case Variable::Tag::DISPATCH_ID: _scratch << "did"; break;
        case Variable::Tag::DISPATCH_SIZE: _scratch << "ls"; break;
    }
}

void MetalCodegen::_emit_type_decl() noexcept {
    Type::traverse(*this);
}

void MetalCodegen::visit(const Type *type) noexcept {
    if (type->is_structure()) {
        _scratch << "struct alignas(" << type->alignment() << ") ";
        _emit_type_name(type);
        _scratch << " {\n";
        for (auto i = 0u; i < type->members().size(); i++) {
            _scratch << "  ";
            _emit_type_name(type->members()[i]);
            _scratch << " m" << i << ";\n";
        }
        _scratch << "};\n\n";
    }
}

void MetalCodegen::_emit_type_name(const Type *type) noexcept {

    switch (type->tag()) {
        case Type::Tag::BOOL: _scratch << "bool"; break;
        case Type::Tag::FLOAT: _scratch << "float"; break;
        case Type::Tag::INT: _scratch << "int"; break;
        case Type::Tag::UINT: _scratch << "uint"; break;
        case Type::Tag::VECTOR:
            _emit_type_name(type->element());
            _scratch << type->dimension();
            break;
        case Type::Tag::MATRIX:
            _scratch << "float"
                     << type->dimension()
                     << "x"
                     << type->dimension();
            break;
        case Type::Tag::ARRAY:
            _scratch << "array<";
            _emit_type_name(type->element());
            _scratch << ", ";
            _scratch << type->dimension() << ">";
            break;
        case Type::Tag::ATOMIC:
            _scratch << "atomic_";
            _emit_type_name(type->element());
            break;
        case Type::Tag::STRUCTURE:
            _scratch << "S" << hash_to_string(type->hash());
            break;
        default:
            break;
    }
}

void MetalCodegen::_emit_variable_decl(Variable v) noexcept {
    switch (v.tag()) {
        case Variable::Tag::BUFFER:
            _scratch << "device ";
            if (_function.variable_usage(v.uid()) == Usage::READ) {
                _scratch << "const ";
            }
            _emit_type_name(v.type()->element());
            _scratch << " *";
            _emit_variable_name(v);
            break;
        case Variable::Tag::TEXTURE:
            _scratch << "texture" << v.type()->dimension() << "d<";
            _emit_type_name(v.type()->element());
            if (auto usage = _function.variable_usage(v.uid());
                usage == Usage::READ_WRITE) {
                _scratch << ", access::read_write> ";
            } else if (usage == Usage::WRITE) {
                _scratch << ", access::write> ";
            } else if (usage == Usage::READ) {
                _scratch << ", access::read> ";
            }
            _emit_variable_name(v);
            break;
        case Variable::Tag::TEXTURE_HEAP:
            _scratch << "device const Texture *";
            _emit_variable_name(v);
            break;
        case Variable::Tag::UNIFORM:
            _scratch << "const ";
            _emit_type_name(v.type());
            _scratch << " ";
            _emit_variable_name(v);
            break;
        case Variable::Tag::THREAD_ID:
            _scratch << "const ";
            _emit_type_name(v.type());
            _scratch << " ";
            _emit_variable_name(v);
            _scratch << " [[thread_position_in_threadgroup]]";
            break;
        case Variable::Tag::BLOCK_ID:
            _scratch << "const ";
            _emit_type_name(v.type());
            _scratch << " ";
            _emit_variable_name(v);
            _scratch << " [[threadgroup_position_in_grid]]";
            break;
        case Variable::Tag::DISPATCH_ID:
            _scratch << "const ";
            _emit_type_name(v.type());
            _scratch << " ";
            _emit_variable_name(v);
            _scratch << " [[thread_position_in_grid]]";
            break;
        case Variable::Tag::DISPATCH_SIZE:
            _scratch << "const ";
            _emit_type_name(v.type());
            _scratch << " ";
            _emit_variable_name(v);
            break;
        case Variable::Tag::LOCAL:
            if (auto usage = _function.variable_usage(v.uid());
                usage == Usage::READ
                || usage == Usage::NONE) {
                _scratch << "const ";
            }
            _emit_type_name(v.type());
            _scratch << " ";
            _emit_variable_name(v);
            break;
        case Variable::Tag::SHARED:
            _scratch << "threadgroup ";
            _emit_type_name(v.type());
            _scratch << " ";
            _emit_variable_name(v);
            break;
    }
}

void MetalCodegen::_emit_indent() noexcept {
    for (auto i = 0u; i < _indent; i++) { _scratch << "  "; }
}

void MetalCodegen::_emit_statements(std::span<const Statement *const> stmts) noexcept {
    _indent++;
    for (auto s : stmts) {
        _scratch << "\n";
        _emit_indent();
        s->accept(*this);
    }
    _indent--;
    if (!stmts.empty()) {
        _scratch << "\n";
        _emit_indent();
    }
}

void MetalCodegen::_emit_constant(Function::ConstantBinding c) noexcept {

    if (std::find(_generated_constants.cbegin(),
                  _generated_constants.cend(), c.data.hash())
        != _generated_constants.cend()) { return; }
    _generated_constants.emplace_back(c.data.hash());

    _scratch << "constant ";
    _emit_type_name(c.type);
    _scratch << " c" << hash_to_string(c.data.hash()) << "{";
    auto count = c.type->dimension();
    static constexpr auto wrap = 16u;
    using namespace std::string_view_literals;
    std::visit(
        [count, this](auto ptr) {
            detail::LiteralPrinter print{_scratch};
            for (auto i = 0u; i < count; i++) {
                if (count > wrap && i % wrap == 0u) { _scratch << "\n    "; }
                print(ptr[i]);
                _scratch << ", ";
            }
        },
        c.data.view());
    if (count > 0u) {
        _scratch.pop_back();
        _scratch.pop_back();
    }
    _scratch << "};\n";
}

void MetalCodegen::visit(const ConstantExpr *expr) {
    _scratch << "c" << hash_to_string(expr->data().hash());
}

void MetalCodegen::visit(const ForStmt *stmt) {

    _scratch << "for (";

    if (auto init = stmt->initialization(); init != nullptr) {
        init->accept(*this);
    } else {
        _scratch << ";";
    }

    if (auto cond = stmt->condition(); cond != nullptr) {
        _scratch << " ";
        cond->accept(*this);
    }
    _scratch << ";";

    if (auto update = stmt->update(); update != nullptr) {
        _scratch << " ";
        update->accept(*this);
        if (_scratch.back() == ';') { _scratch.pop_back(); }
    }

    _scratch << ") ";
    stmt->body()->accept(*this);
}

void MetalCodegen::_emit_preamble() noexcept {

    _scratch << R"(#include <metal_stdlib>

using namespace metal;

template<typename T>
[[nodiscard]] auto none(T v) { return !any(v); }

template<typename T, access a>
[[nodiscard]] auto texture_read(texture2d<T, a> t, uint2 uv) {
  return t.read(uv);
}

template<typename T, access a>
[[nodiscard]] auto texture_read(texture3d<T, a> t, uint3 uvw) {
  return t.read(uvw);
}

template<typename T, access a, typename Value>
void texture_write(texture2d<T, a> t, uint2 uv, Value value) {
  t.write(value, uv);
}

template<typename T, access a, typename Value>
void texture_write(texture3d<T, a> t, uint3 uvw, Value value) {
  t.write(value, uvw);
}

template<typename T>
[[nodiscard]] auto radians(T v) { return v * (M_PI_F / 180.0f); }

template<typename T>
[[nodiscard]] auto degrees(T v) { return v * (180.0f * M_1_PI_F); }

[[nodiscard]] auto inverse(float2x2 m) {
  const auto one_over_determinant = 1.0f / (m[0][0] * m[1][1] - m[1][0] * m[0][1]);
  return float2x2(m[1][1] * one_over_determinant,
				- m[0][1] * one_over_determinant,
				- m[1][0] * one_over_determinant,
				+ m[0][0] * one_over_determinant);
}

[[nodiscard]] auto inverse(float3x3 m) {
  const auto one_over_determinant = 1.0f / (m[0].x * (m[1].y * m[2].z - m[2].y * m[1].z)
                                          - m[1].x * (m[0].y * m[2].z - m[2].y * m[0].z)
                                          + m[2].x * (m[0].y * m[1].z - m[1].y * m[0].z));
  return float3x3(
    (m[1].y * m[2].z - m[2].y * m[1].z) * one_over_determinant,
    (m[2].y * m[0].z - m[0].y * m[2].z) * one_over_determinant,
    (m[0].y * m[1].z - m[1].y * m[0].z) * one_over_determinant,
    (m[2].x * m[1].z - m[1].x * m[2].z) * one_over_determinant,
    (m[0].x * m[2].z - m[2].x * m[0].z) * one_over_determinant,
    (m[1].x * m[0].z - m[0].x * m[1].z) * one_over_determinant,
    (m[1].x * m[2].y - m[2].x * m[1].y) * one_over_determinant,
    (m[2].x * m[0].y - m[0].x * m[2].y) * one_over_determinant,
    (m[0].x * m[1].y - m[1].x * m[0].y) * one_over_determinant);
}

[[nodiscard]] auto inverse(float4x4 m) {
  const auto coef00 = m[2].z * m[3].w - m[3].z * m[2].w;
  const auto coef02 = m[1].z * m[3].w - m[3].z * m[1].w;
  const auto coef03 = m[1].z * m[2].w - m[2].z * m[1].w;
  const auto coef04 = m[2].y * m[3].w - m[3].y * m[2].w;
  const auto coef06 = m[1].y * m[3].w - m[3].y * m[1].w;
  const auto coef07 = m[1].y * m[2].w - m[2].y * m[1].w;
  const auto coef08 = m[2].y * m[3].z - m[3].y * m[2].z;
  const auto coef10 = m[1].y * m[3].z - m[3].y * m[1].z;
  const auto coef11 = m[1].y * m[2].z - m[2].y * m[1].z;
  const auto coef12 = m[2].x * m[3].w - m[3].x * m[2].w;
  const auto coef14 = m[1].x * m[3].w - m[3].x * m[1].w;
  const auto coef15 = m[1].x * m[2].w - m[2].x * m[1].w;
  const auto coef16 = m[2].x * m[3].z - m[3].x * m[2].z;
  const auto coef18 = m[1].x * m[3].z - m[3].x * m[1].z;
  const auto coef19 = m[1].x * m[2].z - m[2].x * m[1].z;
  const auto coef20 = m[2].x * m[3].y - m[3].x * m[2].y;
  const auto coef22 = m[1].x * m[3].y - m[3].x * m[1].y;
  const auto coef23 = m[1].x * m[2].y - m[2].x * m[1].y;
  const auto fac0 = float4{coef00, coef00, coef02, coef03};
  const auto fac1 = float4{coef04, coef04, coef06, coef07};
  const auto fac2 = float4{coef08, coef08, coef10, coef11};
  const auto fac3 = float4{coef12, coef12, coef14, coef15};
  const auto fac4 = float4{coef16, coef16, coef18, coef19};
  const auto fac5 = float4{coef20, coef20, coef22, coef23};
  const auto Vec0 = float4{m[1].x, m[0].x, m[0].x, m[0].x};
  const auto Vec1 = float4{m[1].y, m[0].y, m[0].y, m[0].y};
  const auto Vec2 = float4{m[1].z, m[0].z, m[0].z, m[0].z};
  const auto Vec3 = float4{m[1].w, m[0].w, m[0].w, m[0].w};
  const auto inv0 = Vec1 * fac0 - Vec2 * fac1 + Vec3 * fac2;
  const auto inv1 = Vec0 * fac0 - Vec2 * fac3 + Vec3 * fac4;
  const auto inv2 = Vec0 * fac1 - Vec1 * fac3 + Vec3 * fac5;
  const auto inv3 = Vec0 * fac2 - Vec1 * fac4 + Vec2 * fac5;
  constexpr auto sign_a = float4{+1.0f, -1.0f, +1.0f, -1.0f};
  constexpr auto sign_b = float4{-1.0f, +1.0f, -1.0f, +1.0f};
  const auto inv_0 = inv0 * sign_a;
  const auto inv_1 = inv1 * sign_b;
  const auto inv_2 = inv2 * sign_a;
  const auto inv_3 = inv3 * sign_b;
  const auto dot0 = m[0] * float4{inv_0.x, inv_1.x, inv_2.x, inv_3.x};
  const auto dot1 = dot0.x + dot0.y + dot0.z + dot0.w;
  const auto one_over_determinant = 1.0f / dot1;
  return float4x4(inv_0 * one_over_determinant,
                  inv_1 * one_over_determinant,
                  inv_2 * one_over_determinant,
                  inv_3 * one_over_determinant);
}

[[gnu::always_inline]] inline void group_memory_barrier() {
  threadgroup_barrier(mem_flags::mem_threadgroup);
}

[[gnu::always_inline]] inline void device_memory_barrier() {
  threadgroup_barrier(mem_flags::mem_device);
}

[[gnu::always_inline]] inline void all_memory_barrier() {
  group_memory_barrier();
  device_memory_barrier();
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(device int &a) {
  return reinterpret_cast<device atomic_int *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(device uint &a) {
  return reinterpret_cast<device atomic_uint *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(threadgroup int &a) {
  return reinterpret_cast<threadgroup atomic_int *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(threadgroup uint &a) {
  return reinterpret_cast<threadgroup atomic_uint *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(device const int &a) {
  return reinterpret_cast<device const atomic_int *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(device const uint &a) {
  return reinterpret_cast<device const atomic_uint *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(threadgroup const int &a) {
  return reinterpret_cast<threadgroup const atomic_int *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto as_atomic(threadgroup const uint &a) {
  return reinterpret_cast<threadgroup const atomic_uint *>(&a);
}

[[gnu::always_inline, nodiscard]] inline auto atomic_compare_exchange(device atomic_int *a, int cmp, int val, memory_order) {
  atomic_compare_exchange_weak_explicit(a, &cmp, val, memory_order_relaxed, memory_order_relaxed);
  return cmp;
}

[[gnu::always_inline, nodiscard]] inline auto atomic_compare_exchange(threadgroup atomic_int *a, int cmp, int val, memory_order) {
  atomic_compare_exchange_weak_explicit(a, &cmp, val, memory_order_relaxed, memory_order_relaxed);
  return cmp;
}

[[gnu::always_inline, nodiscard]] inline auto atomic_compare_exchange(device atomic_uint *a, uint cmp, uint val, memory_order) {
  atomic_compare_exchange_weak_explicit(a, &cmp, val, memory_order_relaxed, memory_order_relaxed);
  return cmp;
}

[[gnu::always_inline, nodiscard]] inline auto atomic_compare_exchange(threadgroup atomic_uint *a, uint cmp, uint val, memory_order) {
  atomic_compare_exchange_weak_explicit(a, &cmp, val, memory_order_relaxed, memory_order_relaxed);
  return cmp;
}

template<typename X, typename Y>
[[gnu::always_inline, nodiscard]] inline auto glsl_mod(X x, Y y) {
  return x - y * floor(x / y);
}

template<typename T>
[[gnu::always_inline, nodiscard]] inline auto select(T f, T t, bool b) {
  return b ? t : f;
}

struct Texture {
  metal::texture2d<float> handle2d;
  metal::texture3d<float> handle3d;
  metal::sampler sampler;
};

[[nodiscard]] auto texture_sample(device const Texture *heap, uint index, float2 uv) {
  device const auto &t = heap[index];
  return t.handle2d.sample(t.sampler, uv);
}

[[nodiscard]] auto texture_sample(device const Texture *heap, uint index, float3 uvw) {
  device const auto &t = heap[index];
  return t.handle3d.sample(t.sampler, uvw);
}

[[nodiscard]] auto texture_sample_lod(device const Texture *heap, uint index, float2 uv, float lod) {
  device const auto &t = heap[index];
  return t.handle2d.sample(t.sampler, uv, level(lod));
}

[[nodiscard]] auto texture_sample_lod(device const Texture *heap, uint index, float3 uvw, float lod) {
  device const auto &t = heap[index];
  return t.handle3d.sample(t.sampler, uvw, level(lod));
}

[[nodiscard]] auto texture_sample_grad(device const Texture *heap, uint index, float2 uv, float2 dpdx, float2 dpdy) {
  device const auto &t = heap[index];
  return t.handle2d.sample(t.sampler, uv, gradient2d(dpdx, dpdy));
}

[[nodiscard]] auto texture_sample_grad(device const Texture *heap, uint index, float3 uvw, float3 dpdx, float3 dpdy) {
  device const auto &t = heap[index];
  return t.handle3d.sample(t.sampler, uvw, gradient3d(dpdx, dpdy));
}

)";
}

}
