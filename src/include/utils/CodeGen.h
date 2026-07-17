#pragma once

#include "../token/Ast.h"
#include "Utils.h"
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

class CodeGen {
  std::stringstream code;
  std::map<std::string, std::string> function_signatures;
  int indent_level = 0;
  int temp_var_counter = 0;
  void write_indent();
  std::string get_temp_var();
  std::string generate_expr(const std::shared_ptr<AstNode> &expr);
  std::string get_c_type(const std::string &pgt_type);
  void generate_function(const std::shared_ptr<FunctionDef> &func);
  void generate_statement(const std::shared_ptr<AstNode> &stmt);
  void generate_print(const std::shared_ptr<PrintStmt> &print);
  void generate_input(const std::shared_ptr<InputStmt> &input);
  void generate_if(const std::shared_ptr<IfStmt> &if_stmt);
  void generate_while(const std::shared_ptr<WhileStmt> &while_stmt);
  void generate_file_op(const std::shared_ptr<FileOp> &file_op);
  void generate_net_op(const std::shared_ptr<NetOp> &net_op);
  void generate_var_decl(const std::shared_ptr<VarDecl> &decl);
  void generate_call(const std::shared_ptr<CallStmt> &call);
  void generate_return(const std::shared_ptr<ReturnStmt> &ret);

public:
  std::string generate(const std::vector<std::shared_ptr<AstNode>> &program);
  void save_to_file(const std::string &filename);
};