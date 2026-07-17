#pragma once

#include "Error.h"
#include "Token.h"
#include "Utils.h"
#include <memory>
#include <string>
#include <vector>

struct AstNode {
  SourceLocation location;
  virtual ~AstNode() = default;
};

struct RouteDef {
  std::string method;
  std::string path;
  SourceLocation location;
};

struct FunctionDef : AstNode {
  std::string name;
  std::vector<std::string> param_names;
  std::vector<std::string> param_types;
  std::vector<std::shared_ptr<AstNode>> body;
  std::vector<RouteDef> routes;
  bool has_return_one = false;
};

struct OrmField {
  std::string name;
  std::string db_type;
  int size = 0;
  bool primary_key = false;
  SourceLocation location;
};

struct ClassDef : AstNode {
  std::string name;
  std::string base;
  std::vector<OrmField> fields;
};

struct VarDecl : AstNode {
  std::string name;
  std::string type_name;
  std::shared_ptr<AstNode> expr;
};

struct BinaryOp : AstNode {
  TokenType op;
  std::shared_ptr<AstNode> left, right;
};

struct Literal : AstNode {
  Value value;
};
struct Identifier : AstNode {
  std::string name;
};

struct BuiltinCallExpr : AstNode {
  std::string name;
  std::vector<std::shared_ptr<AstNode>> args;
};

struct PrintStmt : AstNode {
  std::vector<std::shared_ptr<AstNode>> args;
  std::vector<std::string> formats;
  bool is_printg = false;
};

struct InputStmt : AstNode {
  std::string format;
  std::string prompt;
  std::string var_name;
};

struct CallStmt : AstNode {
  std::string func_name;
  std::vector<std::shared_ptr<AstNode>> args;
};

struct ReturnStmt : AstNode {
  std::shared_ptr<AstNode> expr;
};

struct ImportStmt : AstNode {
  std::string file_path;
  std::vector<std::string> import_names;
};

struct IfStmt : AstNode {
  std::shared_ptr<AstNode> condition;
  std::vector<std::shared_ptr<AstNode>> then_body;
  std::vector<std::shared_ptr<AstNode>> else_body;
};

struct WhileStmt : AstNode {
  std::shared_ptr<AstNode> condition;
  std::vector<std::shared_ptr<AstNode>> body;
};

struct NetOp : AstNode {
  std::string transport;
  std::string method;
  std::shared_ptr<AstNode> url;
  std::shared_ptr<AstNode> path;
  std::shared_ptr<AstNode> port;
  std::shared_ptr<AstNode> data;
};

struct FileOp : AstNode {
  TokenType operation;
  std::shared_ptr<AstNode> file_path;
  std::string mode;
  std::shared_ptr<AstNode> data;
};
