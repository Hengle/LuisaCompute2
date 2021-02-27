//
// Created by Mike Smith on 2020/12/2.
//

#pragma once

#include <ast/function_builder.h>

namespace luisa::compute::dsl {

template<typename T>
class Var;

}

namespace luisa::compute::dsl::detail {

template<typename T>
class Expr;

template<typename T>
class ExprBase {

private:
    const Expression *_expression;

public:
    explicit constexpr ExprBase(const Expression *expr) noexcept : _expression{expr} {}
    ExprBase(const Var<T> &variable) noexcept : ExprBase{FunctionBuilder::current()->ref(variable.variable())} {}
    ExprBase(T literal) noexcept : ExprBase{FunctionBuilder::current()->literal(literal)} {}
    constexpr ExprBase(ExprBase &&) noexcept = default;
    constexpr ExprBase(const ExprBase &) noexcept = default;
    [[nodiscard]] constexpr auto expression() const noexcept { return _expression; }

#define LUISA_MAKE_EXPR_BINARY_OP(op, op_concept_name, op_tag_name)                                       \
    template<typename U>                                                                                  \
    requires concepts::op_concept_name<T, U> [[nodiscard]] auto operator op(Expr<U> rhs) const noexcept { \
        using R = std::remove_cvref_t<decltype(std::declval<T>() op std::declval<U>())>;                  \
        return Expr<R>{FunctionBuilder::current()->binary(                                                \
            Type::of<R>(),                                                                                \
            BinaryOp::op_tag_name, this->expression(), rhs.expression())};                                \
    }                                                                                                     \
    template<typename U>                                                                                  \
    [[nodiscard]] auto operator op(U &&rhs) const noexcept {                                              \
        return this->operator op(Expr{std::forward<U>(rhs)});                                             \
    }
#define LUISA_MAKE_EXPR_BINARY_OP_FROM_TRIPLET(op) LUISA_MAKE_EXPR_BINARY_OP op
    LUISA_MAP(LUISA_MAKE_EXPR_BINARY_OP_FROM_TRIPLET,
              (+, Add, ADD),
              (-, Sub, SUB),
              (*, Mul, MUL),
              (/, Div, DIV),
              (%, Mod, MOD),
              (&, BitAnd, BIT_AND),
              (|, BitOr, BIT_OR),
              (^, BitXor, BIT_XOR),
              (<<, ShiftLeft, SHL),
              (>>, ShiftRight, SHR),
              (&&, And, AND),
              (||, Or, OR),
              (==, Equal, EQUAL),
              (!=, NotEqual, NOT_EQUAL),
              (<, Less, LESS),
              (<=, LessEqual, LESS_EQUAL),
              (>, Greater, GREATER),
              (>=, GreaterEqual, GREATER_EQUAL))
#undef LUISA_MAKE_EXPR_BINARY_OP
#undef LUISA_MAKE_EXPR_BINARY_OP_FROM_TRIPLET

    template<typename U>
    requires concepts::Access<T, U> [[nodiscard]] auto operator[](Expr<U> index) const noexcept {
        using R = std::remove_cvref_t<decltype(std::declval<T>()[std::declval<U>()])>;
        return Expr<R>{FunctionBuilder::current()->access(
            Type::of<R>(),
            this->expression(), index.expression())};
    }

    template<typename U>
    [[nodiscard]] auto operator[](U &&index) const noexcept { return this->operator[](Expr{std::forward<U>(index)}); }

    void operator=(const ExprBase &rhs) const noexcept {
        FunctionBuilder::current()->assign(AssignOp::ASSIGN, this->expression(), rhs.expression());
    }

    void operator=(ExprBase &&rhs) const noexcept {
        FunctionBuilder::current()->assign(AssignOp::ASSIGN, this->expression(), rhs.expression());
    }

#define LUISA_MAKE_EXPR_ASSIGN_OP(op, op_concept_name, op_tag_name)                                      \
    template<typename U>                                                                                 \
    requires concepts::op_concept_name<T, U> void operator op(Expr<U> rhs) const noexcept {              \
        FunctionBuilder::current()->assign(AssignOp::op_tag_name, this->expression(), rhs.expression()); \
    }                                                                                                    \
    template<typename U>                                                                                 \
    void operator op(U &&rhs) const noexcept {                                                           \
        return this->operator op(Expr{std::forward<U>(rhs)});                                            \
    }
#define LUISA_MAKE_EXPR_ASSIGN_OP_FROM_TRIPLET(op) LUISA_MAKE_EXPR_ASSIGN_OP op
    LUISA_MAP(LUISA_MAKE_EXPR_ASSIGN_OP_FROM_TRIPLET,
              (=, Assign, ASSIGN),
              (+=, AddAssign, ADD_ASSIGN),
              (-=, SubAssign, SUB_ASSIGN),
              (*=, MulAssign, MUL_ASSIGN),
              (/=, DivAssign, DIV_ASSIGN),
              (%=, ModAssign, MOD_ASSIGN),
              (&=, BitAndAssign, BIT_AND_ASSIGN),
              (|=, BitOrAssign, BIT_OR_ASSIGN),
              (^=, BitXorAssign, BIT_XOR_ASSIGN),
              (<<=, ShiftLeftAssign, SHL_ASSIGN),
              (>>=, ShiftRightAssign, SHR_ASSIGN))
#undef LUISA_MAKE_EXPR_ASSIGN_OP
#undef LUISA_MAKE_EXPR_ASSIGN_OP_FROM_TRIPLET
};

template<typename T>
class Expr : public ExprBase<T> {
public:
    using Type = T;
    using detail::ExprBase<T>::ExprBase;
};

// deduction guides
template<typename T>
Expr(Expr<T>) -> Expr<T>;

template<typename T>
Expr(const Var<T> &) -> Expr<T>;

template<typename T>
Expr(Var<T> &&) -> Expr<T>;

template<concepts::Native T>
Expr(T) -> Expr<std::remove_cvref_t<T>>;

}// namespace luisa::compute::dsl::detail

#define LUISA_MAKE_GLOBAL_EXPR_UNARY_OP(op, op_concept, op_tag)                                 \
    template<luisa::concepts::op_concept T>                                                     \
    [[nodiscard]] inline auto operator op(luisa::compute::dsl::detail::Expr<T> expr) noexcept { \
        using R = std::remove_cvref_t<decltype(op std::declval<T>())>;                          \
        return luisa::compute::dsl::detail::Expr<R>{                                            \
            luisa::compute::FunctionBuilder::current()->unary(                                  \
                luisa::compute::Type::of<R>(),                                                  \
                luisa::compute::UnaryOp::op_tag,                                                \
                expr.expression())};                                                            \
    }
LUISA_MAKE_GLOBAL_EXPR_UNARY_OP(+, Plus, PLUS)
LUISA_MAKE_GLOBAL_EXPR_UNARY_OP(-, Minus, MINUS)
LUISA_MAKE_GLOBAL_EXPR_UNARY_OP(!, Not, NOT)
LUISA_MAKE_GLOBAL_EXPR_UNARY_OP(~, BitNot, BIT_NOT)
#undef LUISA_MAKE_GLOBAL_EXPR_UNARY_OP

#define LUISA_MAKE_GLOBAL_EXPR_BINARY_OP(op, op_concept)                        \
    template<luisa::concepts::Native Lhs, typename Rhs>                         \
    requires luisa::concepts::op_concept<Lhs, Rhs> [[nodiscard]] inline auto    \
    operator op(Lhs lhs, luisa::compute::dsl::detail::Expr<Rhs> rhs) noexcept { \
        return luisa::compute::dsl::detail::Expr{lhs} op rhs;                   \
    }
#define LUISA_MAKE_GLOBAL_EXPR_BINARY_OP_FROM_PAIR(op) LUISA_MAKE_GLOBAL_EXPR_BINARY_OP op
LUISA_MAP(LUISA_MAKE_GLOBAL_EXPR_BINARY_OP_FROM_PAIR,
          (+, Add),
          (-, Sub),
          (*, Mul),
          (/, Div),
          (%, Mod),
          (&, BitAnd),
          (|, BitOr),
          (^, BitXor),
          (<<, ShiftLeft),
          (>>, ShiftRight),
          (&&, And),
          (||, Or),
          (==, Equal),
          (!=, NotEqual),
          (<, Less),
          (<=, LessEqual),
          (>, Greater),
          (>=, GreaterEqual))
#undef LUISA_MAKE_GLOBAL_EXPR_BINARY_OP
#undef LUISA_MAKE_GLOBAL_EXPR_BINARY_OP_FROM_PAIR
