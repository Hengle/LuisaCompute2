//
// Created by Mike on 2021/11/8.
//

#pragma once

#include <ast/function.h>
#include <ast/statement.h>
#include <ast/expression.h>

namespace luisa {
class StringScratch;
}

namespace luisa::compute::cuda {

/**
 * @brief CUDA code generator
 * 
 */
class CUDACodegenAST final : private TypeVisitor, private ExprVisitor, private StmtVisitor {

private:
    StringScratch &_scratch;
    Function _function;
    luisa::vector<Function> _generated_functions;
    luisa::vector<uint64_t> _generated_constants;
    uint32_t _indent{0u};

private:
    const Type *_ray_type;
    const Type *_triangle_hit_type;

private:
    void visit(const Type *type) noexcept override;
    void visit(const UnaryExpr *expr) override;
    void visit(const BinaryExpr *expr) override;
    void visit(const MemberExpr *expr) override;
    void visit(const AccessExpr *expr) override;
    void visit(const LiteralExpr *expr) override;
    void visit(const RefExpr *expr) override;
    void visit(const CallExpr *expr) override;
    void visit(const CastExpr *expr) override;
    void visit(const BreakStmt *stmt) override;
    void visit(const ContinueStmt *stmt) override;
    void visit(const ReturnStmt *stmt) override;
    void visit(const ScopeStmt *stmt) override;
    void visit(const IfStmt *stmt) override;
    void visit(const LoopStmt *stmt) override;
    void visit(const ExprStmt *stmt) override;
    void visit(const SwitchStmt *stmt) override;
    void visit(const SwitchCaseStmt *stmt) override;
    void visit(const SwitchDefaultStmt *stmt) override;
    void visit(const AssignStmt *stmt) override;
    void visit(const ForStmt *stmt) override;
    void visit(const ConstantExpr *expr) override;
    void visit(const CommentStmt *stmt) override;
    void visit(const CpuCustomOpExpr *expr) override;
    void visit(const GpuCustomOpExpr *expr) override;

private:
    void _emit_type_decl() noexcept;
    void _emit_variable_decl(Variable v, bool force_const) noexcept;
    void _emit_type_name(const Type *type) noexcept;
    void _emit_function(Function f) noexcept;
    void _emit_variable_name(Variable v) noexcept;
    void _emit_indent() noexcept;
    void _emit_statements(luisa::span<const Statement *const> stmts) noexcept;
    void _emit_constant(Function::Constant c) noexcept;
    void _emit_variable_declarations(Function f) noexcept;

public:
    explicit CUDACodegenAST(StringScratch &scratch) noexcept;
    void emit(Function f);
};

}// namespace luisa::compute::cuda
