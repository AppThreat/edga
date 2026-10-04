// The back end: walks the IL of the translation unit the front end built and writes it as JSON.
//
// The document has flat tables for what is shared (files, macros and their invocations, types,
// variables at file and namespace scope) and nests each function's body as a tree of statements
// and expressions. Every kind is written by name: the enumerators' values depend on the front
// end's configuration. A node the front end made implicitly (a conversion, a `!= 0` test, an
// implicit `this`) is kept and marked "implicit".

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "edga_options.h"
#include "edga_version.h"
#include "fe_common.h"
#include "il_walk.h"
#include "json_writer.h"
#include "sarif_reader.h"

USING_NAMESPACE_EDG

namespace edga {

#include "il_names.inc"

namespace {

std::string realPath(const char* path) {
  if (path == nullptr || *path == '\0') return "";
  char* resolved = ::realpath(path, nullptr);
  if (resolved == nullptr) return path;
  std::string out(resolved);
  std::free(resolved);
  return out;
}

bool underDirectory(const std::string& path, const std::string& dir) {
  if (dir.empty()) return true;
  if (path.size() < dir.size() || path.compare(0, dir.size(), dir) != 0) return false;
  return path.size() == dir.size() || path[dir.size()] == '/' || dir.back() == '/';
}

class Exporter {
 public:
  explicit Exporter(JsonWriter& json) : json_(json) {}

  void run() {
    root_ = realPath(options().root.c_str());
    setUpTypeSpelling();
    json_.key("tu");
    writeUnit();
    json_.key("files");
    writeFiles();
    json_.key("macros");
    writeMacros();
    json_.key("macroInvocations");
    writeMacroInvocations();
    collect(il_header.primary_scope);
    // names are written as the front end qualifies them, inline namespaces included
    // (`std::__1::vector`); a program names their members without them
    json_.key("inlineNamespaces");
    json_.beginArray();
    for (const std::string& name : inlineNamespaces_) json_.string(name);
    json_.endArray();
    json_.key("globals");
    json_.beginArray();
    for (a_variable_ptr v : globals_) writeVariable(v, /*withInitializer=*/true);
    json_.endArray();
    json_.key("routines");
    json_.beginArray();
    // writing a routine can add the routines it refers to: the list grows while it is written
    for (size_t i = 0; i < routines_.size(); ++i) writeRoutine(routines_[i]);
    json_.endArray();
    // last: every type the document refers to, and the types they refer to
    json_.key("types");
    writeTypes();
  }

 private:
  // ---- files ------------------------------------------------------------------------------------

  void writeUnit() {
    json_.beginObject();
    json_.field("path", primary_source_file_name);
    json_.field("language", C_mode() ? "c" : "c++");
    json_.field("status", is_at_least_one_error() ? "errors" : "ok");
    json_.key("mode");
    json_.beginObject();
    json_.fieldBool("gcc", il_header.gcc_mode);
    json_.fieldBool("gpp", il_header.gpp_mode);
    json_.fieldBool("clang", il_header.clang_mode);
    json_.fieldBool("microsoft", il_header.microsoft_mode);
    json_.endObject();
    json_.endObject();
  }

  void numberFiles(a_source_file_ptr f) {
    for (; f != nullptr; f = f->next) {
      if (fileIds_.find(f) == fileIds_.end()) {
        fileIds_[f] = static_cast<long>(files_.size());
        files_.push_back(f);
      }
      numberFiles(f->first_child_file);
    }
  }

  void writeFiles() {
    numberFiles(il_header.primary_source_file);
    std::unordered_map<a_source_file_ptr, a_source_file_ptr> parentOf;
    for (a_source_file_ptr f : files_)
      for (a_source_file_ptr c = f->first_child_file; c != nullptr; c = c->next) parentOf[c] = f;
    json_.beginArray();
    for (a_source_file_ptr f : files_) {
      json_.beginObject();
      json_.field("id", fileIds_[f]);
      const char* name = f->full_name != nullptr ? f->full_name : f->file_name;
      std::string path = realPath(name);
      json_.field("path", path);
      if (f->name_as_written != nullptr) json_.field("written", f->name_as_written);
      json_.fieldBool("system", f->from_system_include_dir);
      json_.fieldBool("inRoot", !f->from_system_include_dir && underDirectory(path, root_));
      if (f->full_name == nullptr) json_.fieldBool("lineDirective", true);
      auto parent = parentOf.find(f);
      if (parent != parentOf.end() && f->first_seq_number > 0) {
        // the line of the parent the include is on: the parent's line just before the child
        a_line_number line = 0;
        a_boolean atEnd = FALSE;
        a_source_file_ptr at =
            source_file_for_seq(f->first_seq_number - 1, &line, &atEnd, /*physical_line=*/TRUE);
        json_.key("includedFrom");
        json_.beginArray();
        json_.integer(fileIds_[parent->second]);
        json_.integer(at == parent->second ? static_cast<long long>(line) : 0);
        json_.endArray();
        json_.fieldBool("systemInclude", f->included_by_system_include);
      }
      json_.endObject();
    }
    json_.endArray();
  }

  long fileId(a_source_file_ptr f) {
    auto it = fileIds_.find(f);
    if (it != fileIds_.end()) return it->second;
    // a file outside the include tree (none is expected): numbered, though not in the table
    long id = static_cast<long>(files_.size());
    fileIds_[f] = id;
    files_.push_back(f);
    return id;
  }

  bool inRoot(a_source_file_ptr f) {
    if (f == nullptr) return false;
    if (f->from_system_include_dir) return false;
    auto cached = inRootCache_.find(f);
    if (cached != inRootCache_.end()) return cached->second;
    const char* name = f->full_name != nullptr ? f->full_name : f->file_name;
    bool result = underDirectory(realPath(name), root_);
    inRootCache_[f] = result;
    return result;
  }

  a_source_file_ptr fileOfSeq(a_seq_number seq, a_line_number* line) {
    a_boolean atEnd = FALSE;
    return source_file_for_seq(seq, line, &atEnd, /*physical_line=*/TRUE);
  }

  // `[file, line, column]`
  bool writePosition(a_seq_number seq, unsigned column) {
    if (seq == 0) {
      json_.null();
      return false;
    }
    a_line_number line = 0;
    a_source_file_ptr f = fileOfSeq(seq, &line);
    if (f == nullptr) {
      json_.null();
      return false;
    }
    json_.beginArray();
    json_.integer(fileId(f));
    json_.integer(static_cast<long long>(line));
    json_.integer(column);
    json_.endArray();
    return true;
  }

  // "p": where it is; inside a macro expansion also "mi" (the innermost invocation) and "po"
  // (where its text was written: the macro's definition or an argument)
  void position(const a_source_position& p, const char* key = "p") {
    if (p.seq == 0) return;
    json_.key(key);
    writePosition(p.seq, p.column);
    if (p.macro_context != NO_PARENT_MACRO_INVOCATION) {
      json_.field(std::strcmp(key, "p") == 0 ? "mi" : "emi",
                  static_cast<long long>(p.macro_context));
      if (p.orig_seq != 0 && (p.orig_seq != p.seq || p.orig_column != p.column)) {
        json_.key(std::strcmp(key, "p") == 0 ? "po" : "eo");
        writePosition(p.orig_seq, p.orig_column);
      }
    }
  }

  // `key`: [start, end] of a range both ends of which the front end recorded
  void range(const a_source_range& r, const char* key) {
    if (r.start.seq == 0 || r.end.seq == 0) return;
    json_.key(key);
    json_.beginArray();
    writePosition(r.start.seq, r.start.column);
    writePosition(r.end.seq, r.end.column);
    json_.endArray();
  }

  // ---- macros -----------------------------------------------------------------------------------

  void writeMacros() {
    json_.beginArray();
    long id = 1;
    for (a_macro_ptr m = il_header.macros; m != nullptr; m = m->next, ++id) {
      macroIds_[m] = id;
      if (m->is_predefined || m->is_command_line_definition) continue;
      json_.beginObject();
      json_.field("id", id);
      json_.field("name", m->source_corresp.name);
      json_.fieldBool("objectLike", m->object_like);
      if (m->is_undef) json_.fieldBool("undef", true);
      if (m->text != nullptr) json_.field("text", m->text);
      position(m->source_corresp.decl_position);
      json_.endObject();
    }
    json_.endArray();
  }

  void writeMacroInvocations() {
    json_.beginArray();
    if (il_header.root_macro_invocation_record_block != nullptr)
      scanInvocations(il_header.root_macro_invocation_record_block,
                      il_header.num_macro_invocation_records);
    json_.endArray();
  }

  void scanInvocations(a_macro_invocation_record_block_ptr b, a_macro_invocation_record_index n) {
    if (b->left_subtree != nullptr) scanInvocations(b->left_subtree, n);
    long k = static_cast<long>(n) - static_cast<long>(b->first_record_in_block);
    if (k > MACRO_INVOCATION_RECORDS_PER_BLOCK) k = MACRO_INVOCATION_RECORDS_PER_BLOCK;
    for (long i = 0; i < k; ++i) {
      a_macro_invocation_record_ptr r = b->records + i;
      long index = static_cast<long>(b->first_record_in_block) + i;
      // index 0 is unused; a negative parent marks a placeholder, not an invocation
      if (index == 0 || r->parent_macro_index < 0 || r->assoc_macro == nullptr) continue;
      json_.beginObject();
      json_.field("id", index);
      json_.field("parent", static_cast<long long>(r->parent_macro_index));
      auto macro = macroIds_.find(r->assoc_macro);
      if (macro != macroIds_.end()) json_.field("macro", macro->second);
      json_.field("name", r->assoc_macro->source_corresp.name);
      json_.key("start");
      writePosition(r->start.seq, r->start.column);
      json_.key("end");
      writePosition(r->end.seq, r->end.column);
      json_.endObject();
    }
    if (b->right_subtree != nullptr) scanInvocations(b->right_subtree, n);
  }

  // ---- declarations ---------------------------------------------------------------------------

  // Every routine and file- or namespace-scope variable, including template instances and
  // compiler-generated members, which source sequence lists leave out.
  void collect(a_scope_ptr s) {
    if (s == nullptr) return;
    for (a_routine_ptr r = s->routines; r != nullptr; r = r->next) {
      if (r->is_prototype_instantiation) continue;
      if (special_kind_is(r, sfk_deduction_guide)) continue;
      // the front end's own declarations (`operator new`, builtins) are written when referred to
      if (r->source_corresp.decl_position.seq == 0 && r->function_def_number == 0) continue;
      if (seenRoutines_.insert(r).second) routines_.push_back(r);
    }
    if (s->kind == sck_file || s->kind == sck_namespace) {
      for (a_variable_ptr v = s->variables; v != nullptr; v = v->next) {
        if (v->is_prototype_instantiation || v->is_nonreal || v->init_kind == initk_binding)
          continue;
        globals_.push_back(v);
      }
    }
    for (a_type_ptr t = s->types; t != nullptr; t = t->next) {
      if (is_immediate_class_type(t) && !type_is_nonreal(t) && class_type_supp(t) != nullptr) {
        a_scope_ptr cs = class_type_supp(t)->assoc_scope;
        if (cs != nullptr) {
          collect(cs);
          // static data members
          for (a_variable_ptr v = cs->variables; v != nullptr; v = v->next)
            if (!v->is_prototype_instantiation && !v->is_nonreal) globals_.push_back(v);
        }
      }
    }
    for (a_namespace_ptr ns = s->namespaces; ns != nullptr; ns = ns->next) {
      if (ns->is_namespace_alias) continue;
      if (ns->is_inline) inlineNamespaces_.insert(qualifiedName(&ns->source_corresp, iek_namespace));
      collect(ns->variant.assoc_scope);
    }
  }

  // ---- routines -------------------------------------------------------------------------------

  void writeRoutine(a_routine_ptr r) {
    json_.beginObject();
    json_.field("id", routineId(r));
    const char* name = r->source_corresp.name;
    json_.field("name", name != nullptr ? name : "");
    json_.field("qualifiedName", qualifiedName(&r->source_corresp, iek_routine));
    json_.field("type", typeId(r->type));
    a_type_ptr routineType = skip_typerefs(r->type);
    if (routineType != nullptr && routineType->kind == tk_routine) {
      json_.field("returnType", typeId(routineType->variant.routine.return_type));
      a_routine_type_supplement_ptr extra = routineType->variant.routine.extra_info;
      if (extra != nullptr) {
        if (extra->has_ellipsis) json_.fieldBool("variadic", true);
        if (extra->prototyped) json_.fieldBool("prototyped", true);
        if (extra->this_class != nullptr) json_.field("thisClass", typeId(extra->this_class));
        if (extra->does_not_return) json_.fieldBool("noReturn", true);
      }
    }
    json_.field("linkage",
                linkageName(static_cast<a_name_linkage_kind>(r->source_corresp.name_linkage)));
    json_.field("storage", storageClassName(r->storage_class));
    if (r->special_kind != sfk_none) {
      json_.field("special", specialFunctionName(r->special_kind));
      if (r->special_kind == sfk_operator)
        json_.field("operator", opnameName(r->variant.opname_kind));
    }
    if (r->source_corresp.is_class_member) {
      a_type_ptr owner = parent_class_of(r);
      if (owner != nullptr) json_.field("class", typeId(owner));
    }
    if (r->is_virtual) json_.fieldBool("virtual", true);
    if (r->pure_virtual) json_.fieldBool("pureVirtual", true);
    if (r->is_inline) json_.fieldBool("inline", true);
    if (r->compiler_generated) json_.fieldBool("implicit", true);
    if (r->is_template_function) json_.fieldBool("templateInstance", true);
    if (r->is_lambda_body) json_.fieldBool("lambda", true);
    position(r->source_corresp.decl_position);
    // where the declaration starts: its specifiers (`int`, `static`), else its declarator (a
    // constructor's name)
    if (const a_decl_position_supplement* info = r->source_corresp.decl_pos_info) {
      const a_source_position& start = info->specifiers_range.start.seq != 0
                                           ? info->specifiers_range.start
                                           : info->variant.declarator_range.start;
      if (start.seq != 0) position(start, "start");
    }
    writeAttributes(r->source_corresp.attributes);

    a_scope_ptr body = nullptr;
    if (r->function_def_number != NULL_function_def_number) body = scope_for_routine_or_null(r);
    a_line_number line = 0;
    a_seq_number seq = r->source_corresp.decl_position.seq != 0
                           ? r->source_corresp.decl_position.seq
                           : r->source_corresp.decl_position.orig_seq;
    a_source_file_ptr definedIn = seq != 0 ? fileOfSeq(seq, &line) : nullptr;
    bool exportBody = body != nullptr && body->assoc_block != nullptr &&
                      (inRoot(definedIn) || (options().headers && definedIn != nullptr));
    json_.fieldBool("defined", body != nullptr);
    if (exportBody) {
      a_scope_ptr saved = innermost_function_scope;
      innermost_function_scope = body;
      json_.key("params");
      json_.beginArray();
      if (a_variable_ptr self = body->variant.routine.this_param_variable) {
        writeVariable(self, false, /*parameter=*/true);
      }
      for (a_variable_ptr p = body->variant.routine.parameters; p != nullptr; p = p->next)
        writeVariable(p, false, /*parameter=*/true);
      json_.endArray();
      writeConstructorInits(body);
      json_.key("body");
      writeStatement(body->assoc_block);
      innermost_function_scope = saved;
    } else {
      writeDeclaredParameters(routineType);
    }
    json_.endObject();
  }

  void writeDeclaredParameters(a_type_ptr routineType) {
    json_.key("params");
    json_.beginArray();
    if (routineType != nullptr && routineType->kind == tk_routine &&
        routineType->variant.routine.extra_info != nullptr) {
      for (a_param_type_ptr p = routineType->variant.routine.extra_info->param_type_list;
           p != nullptr; p = p->next) {
        json_.beginObject();
        json_.field("name", p->name != nullptr ? p->name : "");
        json_.field("type", typeId(p->type));
        json_.endObject();
      }
    }
    json_.endArray();
  }

  void writeConstructorInits(a_scope_ptr body) {
    a_constructor_init_ptr ci = body->variant.routine.constructor_inits;
    if (ci == nullptr) return;
    json_.key("ctorInits");
    json_.beginArray();
    for (; ci != nullptr; ci = ci->next) {
      json_.beginObject();
      if (ci->kind == cik_field && ci->variant.field != nullptr) {
        json_.field("field", ci->variant.field->source_corresp.name);
      } else if ((ci->kind == cik_direct_base_class || ci->kind == cik_virtual_base_class) &&
                 ci->variant.base_class != nullptr) {
        json_.field("base", typeId(ci->variant.base_class->type));
      }
      if (ci->compiler_generated) json_.fieldBool("implicit", true);
      json_.key("init");
      writeDynamicInit(ci->initializer);
      json_.endObject();
    }
    json_.endArray();
  }

  void writeAttributes(an_attribute_ptr a) {
    if (a == nullptr) return;
    json_.key("attributes");
    json_.beginArray();
    for (; a != nullptr; a = a->next) {
      json_.beginObject();
      json_.field("kind", attributeKindName(a->kind));
      if (a->name != nullptr) json_.field("name", a->name);
      json_.key("args");
      json_.beginArray();
      // an attribute the front end does not know keeps its argument tokens: one argument, as
      // written (`write_only,1,2`)
      std::string raw;
      for (an_attribute_arg_ptr arg = a->arguments; arg != nullptr; arg = arg->next) {
        if (arg->kind == aak_constant && arg->variant.constant != nullptr) {
          writeConstantValue(arg->variant.constant);
        } else if (arg->kind == aak_token && arg->variant.token != nullptr) {
          json_.string(arg->variant.token);
        } else if (arg->kind == aak_raw_token && arg->variant.token != nullptr) {
          raw += arg->variant.token;
        } else if (arg->kind == aak_expression) {
          // a name the attribute refers to (`cleanup(unlock)`), or a constant expression
          writeExpr(expr_node_from_attribute_arg(arg));
        }
      }
      if (!raw.empty()) json_.string(raw.substr(raw.front() == '(' ? 1 : 0,
                                              raw.size() - (raw.front() == '(' ? 1 : 0) -
                                                  (raw.back() == ')' ? 1 : 0)));
      json_.endArray();
      json_.endObject();
    }
    json_.endArray();
  }

  // ---- variables ------------------------------------------------------------------------------

  void writeVariable(a_variable_ptr v, bool withInitializer, bool parameter = false) {
    json_.beginObject();
    json_.field("id", variableId(v));
    const char* name = v->source_corresp.name;
    json_.field("name", name != nullptr ? name : "");
    if (!parameter && v->source_corresp.is_class_member)
      json_.field("qualifiedName", qualifiedName(&v->source_corresp, iek_variable));
    json_.field("type", typeId(v->type));
    json_.field("storage", storageClassName(v->storage_class));
    if (!parameter)
      json_.field("linkage",
                  linkageName(static_cast<a_name_linkage_kind>(v->source_corresp.name_linkage)));
    if (v->is_this_parameter) json_.fieldBool("this", true);
    if (v->compiler_generated) json_.fieldBool("implicit", true);
    if (v->address_taken) json_.fieldBool("addressTaken", true);
    if (parameter && v->param_value_has_been_changed) json_.fieldBool("modified", true);
    position(v->source_corresp.decl_position);
    writeAttributes(v->source_corresp.attributes);
    // where the declaration starts: its specifiers (`const std::vector<int> &v` at `const`)
    if (const a_decl_position_supplement* info = v->source_corresp.decl_pos_info) {
      if (info->specifiers_range.start.seq != 0) position(info->specifiers_range.start, "start");
    }
    if (withInitializer) writeVariableInitializer(v);
    json_.endObject();
  }

  void writeVariableInitializer(a_variable_ptr v) {
    switch (v->init_kind) {
      case initk_static:
        if (v->initializer.constant != nullptr) {
          json_.key("init");
          writeConstantNode(v->initializer.constant, v->type);
        }
        break;
      case initk_dynamic:
        if (v->initializer.dynamic != nullptr) {
          json_.key("init");
          writeDynamicInit(v->initializer.dynamic);
        }
        break;
      case initk_zero:
        json_.fieldBool("zeroInit", true);
        break;
      default:
        break;
    }
  }

  // ---- statements -----------------------------------------------------------------------------

  void writeStatement(a_statement_ptr s) {
    if (s == nullptr) {
      json_.null();
      return;
    }
    json_.beginObject();
    json_.field("k", statementKindName(s->kind));
    position(s->position);
    if (s->end_position.seq != 0) position(s->end_position, "end");
    if (s->compiler_generated) json_.fieldBool("implicit", true);
    switch (s->kind) {
      case stmk_expr:
        writeExprField("expr", s->expr);
        break;
      case stmk_if:
        writeExprField("cond", s->expr);
        json_.key("then");
        writeStatement(s->variant.if_stmt.then_statement);
        if (s->variant.if_stmt.else_statement != nullptr) {
          json_.key("else");
          writeStatement(s->variant.if_stmt.else_statement);
        }
        break;
      case stmk_while:
      case stmk_end_test_while:
        writeExprField("cond", s->expr);
        json_.key("body");
        writeStatement(s->variant.loop_statement);
        break;
      case stmk_for: {
        a_for_loop_ptr loop = s->variant.for_loop.extra_info;
        if (loop != nullptr && loop->initialization != nullptr) {
          json_.key("init");
          writeStatement(loop->initialization);
        }
        writeExprField("cond", s->expr);
        if (loop != nullptr && loop->increment != nullptr) writeExprField("inc", loop->increment);
        json_.key("body");
        writeStatement(s->variant.for_loop.statement);
        break;
      }
      case stmk_range_based_for: {
        a_range_based_for_loop_ptr loop = s->variant.range_based_for_loop.extra_info;
        if (loop != nullptr) {
          if (loop->initialization != nullptr) {
            json_.key("init");
            writeStatement(loop->initialization);
          }
          // the variable the for-range-declaration declares, set to each element in turn
          if (loop->iterator != nullptr) {
            json_.key("variable");
            writeVariable(loop->iterator, false);
          }
          // what is iterated over: the initialiser of the front end's own range variable
          a_variable_ptr range = loop->range;
          if (range != nullptr && range->init_kind == initk_dynamic &&
              range->initializer.dynamic != nullptr) {
            json_.key("over");
            writeDynamicInit(range->initializer.dynamic);
          }
        }
        json_.key("body");
        writeStatement(s->variant.range_based_for_loop.statement);
        break;
      }
      case stmk_switch:
        writeExprField("cond", s->expr);
        json_.key("body");
        writeStatement(s->variant.switch_stmt.body_statement);
        break;
      case stmk_switch_case: {
        a_switch_case_entry_ptr entry = s->variant.switch_case.extra_info;
        if (entry == nullptr || entry->case_value == nullptr) {
          json_.fieldBool("default", true);
        } else {
          json_.key("value");
          writeConstantValue(entry->case_value);
          if (entry->range_end != nullptr) {
            json_.key("rangeEnd");
            writeConstantValue(entry->range_end);
          }
        }
        break;
      }
      case stmk_goto: {
        a_label_ptr label = s->variant.label.ptr;
        if (label != nullptr) {
          const char* jump = label->break_label || label->switch_break_label ? "break"
                             : label->continue_label                         ? "continue"
                                                                             : "goto";
          json_.field("jump", jump);
          if (label->source_corresp.name != nullptr)
            json_.field("label", label->source_corresp.name);
        }
        break;
      }
      case stmk_label: {
        a_label_ptr label = s->variant.label.ptr;
        if (label != nullptr && label->source_corresp.name != nullptr)
          json_.field("label", label->source_corresp.name);
        break;
      }
      case stmk_return:
        writeExprField("expr", s->expr);
        if (s->variant.return_dynamic_init != nullptr) {
          json_.key("init");
          writeDynamicInit(s->variant.return_dynamic_init);
        }
        break;
      case stmk_block: {
        json_.key("stmts");
        json_.beginArray();
        for (a_statement_ptr c = s->variant.block.statements; c != nullptr; c = c->next) {
          // a local's initialisation is written with its declaration, unless it runs later
          if (c->kind == stmk_init && c->variant.dynamic_init != nullptr &&
              !c->variant.dynamic_init->follows_an_exec_statement)
            continue;
          writeStatement(c);
        }
        json_.endArray();
        a_block_ptr block = s->variant.block.extra_info;
        if (block != nullptr && block->is_statement_expression)
          json_.fieldBool("statementExpression", true);
        break;
      }
      case stmk_decl: {
        json_.key("decls");
        json_.beginArray();
        for (an_il_entity_list_entry_ptr e = s->variant.decl.entities; e != nullptr; e = e->next) {
          if (e->entity.kind == iek_variable) {
            writeVariable(reinterpret_cast<a_variable_ptr>(e->entity.ptr), true);
          } else if (e->entity.kind == iek_type) {
            json_.beginObject();
            json_.field("typeDecl", typeId(reinterpret_cast<a_type_ptr>(e->entity.ptr)));
            json_.endObject();
          }
        }
        json_.endArray();
        break;
      }
      case stmk_init:
        if (s->variant.dynamic_init != nullptr) {
          if (a_variable_ptr v = s->variant.dynamic_init->variable)
            json_.field("var", variableId(v));
          json_.key("init");
          writeDynamicInit(s->variant.dynamic_init);
        }
        break;
      case stmk_try_block: {
        a_try_supplement_ptr t = s->variant.try_block;
        if (t != nullptr) {
          json_.key("body");
          writeStatement(t->statement);
          json_.key("handlers");
          json_.beginArray();
          for (a_handler_ptr h = t->handlers; h != nullptr; h = h->next) {
            json_.beginObject();
            if (h->parameter != nullptr) {
              json_.key("param");
              writeVariable(h->parameter, false);
            } else {
              json_.fieldBool("catchAll", true);
            }
            json_.key("body");
            writeStatement(h->statement);
            json_.endObject();
          }
          json_.endArray();
        }
        break;
      }
      case stmk_stmt_expr_result:
        writeExprField("expr", s->expr);
        break;
      case stmk_assigned_goto:
        writeExprField("expr", s->expr);
        break;
      default:
        if (s->expr != nullptr) writeExprField("expr", s->expr);
        break;
    }
    json_.endObject();
  }

  // ---- expressions ----------------------------------------------------------------------------

  void writeExprField(const char* key, an_expr_node_ptr e) {
    if (e == nullptr) return;
    json_.key(key);
    writeExpr(e);
  }

  void writeExprList(const char* key, an_expr_node_ptr first) {
    json_.key(key);
    json_.beginArray();
    for (an_expr_node_ptr e = first; e != nullptr; e = e->next) writeExpr(e);
    json_.endArray();
  }

  void writeExpr(an_expr_node_ptr e) {
    if (e == nullptr) {
      json_.null();
      return;
    }
    // a full-expression wrapper adds nothing a reader needs
    if (e->kind == enk_object_lifetime && e->variant.object_lifetime.expr != nullptr) {
      writeExpr(e->variant.object_lifetime.expr);
      return;
    }
    if (++depth_ > MaxDepth) {
      --depth_;
      json_.beginObject();
      json_.field("k", "tooDeep");
      json_.endObject();
      return;
    }
    json_.beginObject();
    json_.field("k", nodeKindName(static_cast<an_expr_node_kind>(e->kind)));
    if (e->type != nullptr) json_.field("t", typeId(e->type));
    position(e->position);
    if (e->expr_range.start.seq != 0) {
      json_.key("range");
      json_.beginArray();
      writePosition(e->expr_range.start.seq, e->expr_range.start.column);
      writePosition(e->expr_range.end.seq, e->expr_range.end.column);
      json_.endArray();
    }
    if (e->is_lvalue) json_.fieldBool("lv", true);
    if (e->compiler_generated) json_.fieldBool("implicit", true);
    switch (e->kind) {
      case enk_operation: {
        an_expr_operator_kind op = e->variant.operation.kind;
        json_.field("op", operatorName(op));
        if (e->variant.operation.pointer_operand_is_second) json_.fieldBool("pointerSecond", true);
        if (e->variant.operation.is_virtual_call) json_.fieldBool("virtual", true);
        if (e->variant.operation.call_uses_operator_syntax) json_.fieldBool("operatorSyntax", true);
        if (e->variant.operation.is_conversion_call) json_.fieldBool("conversion", true);
        if (op == eok_call || op == eok_dot_member_call || op == eok_points_to_member_call) {
          an_expr_node_ptr callee = e->variant.operation.operands;
          a_routine_ptr target = callee != nullptr ? routine_from_function_expr(callee) : nullptr;
          if (target != nullptr) {
            json_.field("callee", routineId(target));
            noteRoutine(target);
          }
        }
        writeExprList("ops", e->variant.operation.operands);
        break;
      }
      case enk_constant:
        if (e->variant.constant.ptr != nullptr) writeConstantBody(e->variant.constant.ptr);
        break;
      case enk_variable:
        if (a_variable_ptr v = e->variant.variable.ptr) {
          json_.field("var", variableId(v));
          json_.field("name", v->source_corresp.name != nullptr ? v->source_corresp.name : "");
          noteGlobal(v);
        }
        break;
      case enk_field:
        if (a_field_ptr f = e->variant.field.ptr) {
          json_.field("name", f->source_corresp.name != nullptr ? f->source_corresp.name : "");
          if (f->offset != 0) json_.field("offset", static_cast<long long>(f->offset));
        }
        break;
      case enk_routine:
        if (a_routine_ptr r = e->variant.routine.ptr) {
          json_.field("routine", routineId(r));
          json_.field("name", r->source_corresp.name != nullptr ? r->source_corresp.name : "");
          noteRoutine(r);
        }
        break;
      case enk_temp_init:
      case enk_lambda:
        if (e->variant.init.dynamic_init != nullptr) {
          json_.key("init");
          writeDynamicInit(e->variant.init.dynamic_init);
        }
        if (e->kind == enk_lambda && e->variant.init.source.lambda != nullptr) {
          a_lambda_ptr l = e->variant.init.source.lambda;
          if (l->lambda_routine != nullptr) {
            json_.field("routine", routineId(l->lambda_routine));
            noteRoutine(l->lambda_routine);
          }
          if (l->closure_class != nullptr) json_.field("closure", typeId(l->closure_class));
        }
        break;
      case enk_new_delete: {
        a_new_delete_supplement_ptr nd = e->variant.new_delete;
        if (nd != nullptr) {
          json_.fieldBool("new", nd->is_new);
          if (nd->array_delete) json_.fieldBool("array", true);
          if (nd->placement_new) json_.fieldBool("placement", true);
          if (nd->type != nullptr) json_.field("of", typeId(nd->type));
          if (nd->routine != nullptr) {
            json_.field("routine", routineId(nd->routine));
            noteRoutine(nd->routine);
          }
          if (nd->number_of_elements != nullptr) writeExprField("count", nd->number_of_elements);
          if (nd->arg != nullptr) writeExprList("args", nd->arg);
          if (nd->dynamic_init != nullptr) {
            json_.key("init");
            writeDynamicInit(nd->dynamic_init);
          }
        }
        break;
      }
      case enk_throw: {
        a_throw_supplement_ptr t = e->variant.throw_info;
        if (t == nullptr) {
          json_.fieldBool("rethrow", true);
        } else {
          if (t->type != nullptr) json_.field("of", typeId(t->type));
          if (t->dynamic_init != nullptr) {
            json_.key("init");
            writeDynamicInit(t->dynamic_init);
          }
        }
        break;
      }
      case enk_condition: {
        a_condition_supplement_ptr c = e->variant.condition;
        if (c != nullptr) {
          if (c->dynamic_init != nullptr && c->dynamic_init->variable != nullptr) {
            json_.key("decl");
            writeVariable(c->dynamic_init->variable, false);
            json_.key("init");
            writeDynamicInit(c->dynamic_init);
          }
          writeExprField("expr", c->expr);
        }
        break;
      }
      case enk_sizeof:
      case enk_alignof:
        if (e->variant.sizeof_info.is_type) {
          if (e->variant.sizeof_info.variant.type != nullptr)
            json_.field("of", typeId(e->variant.sizeof_info.variant.type));
        } else {
          writeExprField("expr", e->variant.sizeof_info.variant.expr);
        }
        break;
      case enk_statement:
        json_.key("stmt");
        writeStatement(e->variant.statement);
        break;
      case enk_type_operand:
        if (e->variant.type_operand.type != nullptr)
          json_.field("of", typeId(e->variant.type_operand.type));
        break;
      default:
        break;
    }
    json_.endObject();
    --depth_;
  }

  // ---- initialisers and constants -------------------------------------------------------------

  void writeDynamicInit(a_dynamic_init_ptr d) {
    if (d == nullptr) {
      json_.null();
      return;
    }
    json_.beginObject();
    json_.field("k", dynamicInitKindName(d->kind));
    if (d->variable != nullptr) json_.field("var", variableId(d->variable));
    switch (d->kind) {
      case dik_expression:
      case dik_class_result_via_ctor:
        writeExprField("expr", d->variant.expression);
        break;
      case dik_constant:
      case dik_nonconstant_aggregate:
      case dik_lambda:
        if (d->variant.constant.ptr != nullptr) {
          json_.key("const");
          writeConstantNode(d->variant.constant.ptr, nullptr);
        }
        break;
      case dik_constructor:
        if (d->variant.constructor.ptr != nullptr) {
          json_.field("routine", routineId(d->variant.constructor.ptr));
          noteRoutine(d->variant.constructor.ptr);
        }
        writeExprList("args", d->variant.constructor.args);
        break;
      case dik_bitwise_copy:
        writeExprField("expr", d->variant.bitwise_copy.source);
        break;
      default:
        break;
    }
    if (d->destructor != nullptr) {
      json_.field("destructor", routineId(d->destructor));
      noteRoutine(d->destructor);
    }
    json_.endObject();
  }

  // A constant as a node: its value, and the expression it was folded from.
  void writeConstantNode(a_constant_ptr c, a_type_ptr type) {
    json_.beginObject();
    json_.field("k", "constant");
    json_.field("t", typeId(type != nullptr ? type : c->type));
    writeConstantBody(c);
    json_.endObject();
  }

  void writeConstantBody(a_constant_ptr c) {
    json_.field("ck", constantKindName(c->kind));
    switch (c->kind) {
      case ck_integer:
        json_.key("value");
        writeConstantValue(c);
        break;
      case ck_string: {
        size_t length = static_cast<size_t>(c->variant.string.length);
        // without the terminating NUL the front end counts in
        while (length > 0 && c->variant.string.value[length - 1] == '\0') --length;
        json_.key("value");
        json_.string(c->variant.string.value, length);
        break;
      }
      case ck_float: {
        a_boolean pinf = FALSE, ninf = FALSE, nan = FALSE;
        a_type_ptr t = skip_typerefs(c->type);
        if (t != nullptr && t->kind == tk_float) {
          a_number_buffer text =
              fp_to_string(t->variant.float_kind, &c->variant.float_value, &pinf, &ninf, &nan);
          json_.field("value", text.as_temp_characters());
        }
        break;
      }
      case ck_address:
        json_.field("base", addressBaseName(c->variant.address.kind));
        if (c->variant.address.kind == abk_routine &&
            c->variant.address.variant.routine != nullptr) {
          json_.field("routine", routineId(c->variant.address.variant.routine));
          noteRoutine(c->variant.address.variant.routine);
        } else if (c->variant.address.kind == abk_variable &&
                   c->variant.address.variant.variable != nullptr) {
          json_.field("var", variableId(c->variant.address.variant.variable));
          noteGlobal(c->variant.address.variant.variable);
        } else if (c->variant.address.kind == abk_constant &&
                   c->variant.address.variant.constant != nullptr) {
          json_.key("of");
          writeConstantNode(c->variant.address.variant.constant, nullptr);
        }
        if (c->variant.address.offset != 0)
          json_.field("offset", static_cast<long long>(c->variant.address.offset));
        break;
      case ck_aggregate: {
        json_.key("elements");
        json_.beginArray();
        for (a_constant_ptr e = c->variant.aggregate.first_constant; e != nullptr; e = e->next)
          writeConstantNode(e, nullptr);
        json_.endArray();
        break;
      }
      case ck_dynamic_init:
        json_.key("init");
        writeDynamicInit(c->variant.dynamic_init.ptr);
        break;
      case ck_designator:
        // the element of an aggregate the next constant initialises
        if (!c->variant.designator.is_generic) {
          if (c->variant.designator.is_field_designator) {
            a_field_ptr f = c->variant.designator.variant.field;
            if (f != nullptr && f->source_corresp.name != nullptr)
              json_.field("field", f->source_corresp.name);
          } else {
            json_.field("index",
                        static_cast<long long>(c->variant.designator.variant.array_element));
          }
        }
        break;
      default:
        break;
    }
    // the expression a constant was folded from: `sizeof(p->data)` for 16, `N` for 4
    if (constant_should_be_put_out_as_expr(c)) {
      an_expr_node_ptr folded = expr_node_from_constant(c);
      if (folded != nullptr) writeExprField("expr", folded);
    }
  }

  void writeConstantValue(a_constant_ptr c) {
    if (c == nullptr || c->kind != ck_integer) {
      json_.null();
      return;
    }
    a_number_buffer text = decimal_str_for_integer_constant(c);
    json_.string(text.as_temp_characters());
  }

  // ---- types ----------------------------------------------------------------------------------

  void setUpTypeSpelling() {
    if (textBuffer_ == nullptr) textBuffer_ = alloc_text_buffer(1024);
    clear_il_to_str_output_control_block(&octl_);
    octl_.output_str = put_str_into_text_buffer;
    octl_.text_buffer = textBuffer_;
    octl_.for_diagnostics = TRUE;
  }

  std::string spell(a_type_ptr t) {
    reset_text_buffer(textBuffer_);
    form_type(t, &octl_);
    return std::string(textBuffer_->buffer, textBuffer_->size);
  }

  std::string qualifiedName(a_source_correspondence* scp, an_il_entry_kind kind) {
    reset_text_buffer(textBuffer_);
    form_name(scp, kind, &octl_);
    return std::string(textBuffer_->buffer, textBuffer_->size);
  }

  long typeId(a_type_ptr t) {
    if (t == nullptr) return -1;
    auto it = typeIds_.find(t);
    if (it != typeIds_.end()) return it->second;
    long id = static_cast<long>(types_.size());
    typeIds_[t] = id;
    types_.push_back(t);
    return id;
  }

  void writeTypes() {
    json_.beginArray();
    // writing a type can refer to more types: the list grows while it is written
    for (size_t i = 0; i < types_.size(); ++i) writeType(types_[i], static_cast<long>(i));
    json_.endArray();
  }

  void writeType(a_type_ptr t, long id) {
    json_.beginObject();
    json_.field("id", id);
    json_.field("kind", typeKindName(t->kind));
    json_.field("name", spell(t));
    if (t->size != 0) json_.field("size", static_cast<long long>(t->size));
    switch (t->kind) {
      case tk_integer:
        json_.fieldBool("signed", int_type_is_signed(t));
        if (t->variant.integer.enum_type) {
          json_.fieldBool("enum", true);
          if (t->source_corresp.name != nullptr) json_.field("tag", t->source_corresp.name);
        }
        if (t->variant.integer.bool_type) json_.fieldBool("bool", true);
        break;
      case tk_pointer:
        json_.field("to", typeId(t->variant.pointer.type));
        if (t->variant.pointer.is_reference) json_.fieldBool("reference", true);
        if (t->variant.pointer.is_rvalue_reference) json_.fieldBool("rvalueReference", true);
        break;
      case tk_array:
        json_.field("of", typeId(t->variant.array.element_type));
        if (!t->variant.array.is_variable_size_array &&
            !t->variant.array.is_template_dependent_size_array) {
          json_.field("count", static_cast<long long>(t->variant.array.variant.number_of_elements));
        } else {
          json_.fieldBool("variableSize", true);
        }
        break;
      case tk_routine: {
        json_.field("returns", typeId(t->variant.routine.return_type));
        a_routine_type_supplement_ptr extra = t->variant.routine.extra_info;
        if (extra != nullptr) {
          json_.key("params");
          json_.beginArray();
          for (a_param_type_ptr p = extra->param_type_list; p != nullptr; p = p->next)
            json_.integer(typeId(p->type));
          json_.endArray();
          if (extra->has_ellipsis) json_.fieldBool("variadic", true);
        }
        break;
      }
      case tk_class:
      case tk_struct:
      case tk_union: {
        if (t->source_corresp.name != nullptr) json_.field("tag", t->source_corresp.name);
        json_.field("qualifiedName", qualifiedName(&t->source_corresp, iek_type));
        position(t->source_corresp.decl_position);
        // the class-specifier, `struct S { ... }`: what is defined inside the braces
        if (t->source_corresp.decl_pos_info != nullptr)
          range(t->source_corresp.decl_pos_info->specifiers_range, "span");
        if (t->incomplete) json_.fieldBool("incomplete", true);
        json_.key("fields");
        json_.beginArray();
        for (a_field_ptr f = t->variant.class_struct_union.field_list; f != nullptr; f = f->next) {
          json_.beginObject();
          json_.field("name", f->source_corresp.name != nullptr ? f->source_corresp.name : "");
          json_.field("type", typeId(f->type));
          json_.field("offset", static_cast<long long>(f->offset));
          if (f->is_bit_field) json_.fieldBool("bitField", true);
          position(f->source_corresp.decl_position);
          json_.endObject();
        }
        json_.endArray();
        a_class_type_supplement_ptr supp = class_type_supp(t);
        if (supp != nullptr) {
          json_.key("bases");
          json_.beginArray();
          for (a_base_class_ptr b = supp->direct_base_classes; b != nullptr; b = b->next_direct) {
            json_.beginObject();
            json_.field("type", typeId(b->type));
            if (b->is_virtual) json_.fieldBool("virtual", true);
            json_.endObject();
          }
          json_.endArray();
          if (supp->is_lambda_closure_class) json_.fieldBool("closure", true);
          // an instance of a class template with real arguments: `Box<int>`
          if (t->variant.class_struct_union.is_template_class &&
              !t->variant.class_struct_union.is_nonreal_class)
            json_.fieldBool("templateInstance", true);
        }
        break;
      }
      case tk_typeref:
        json_.field("of", typeId(t->variant.typeref.type));
        if (t->source_corresp.name != nullptr) {
          json_.field("typedef", t->source_corresp.name);
          position(t->source_corresp.decl_position);
        }
        json_.field("refKind", typerefKindName(t->variant.typeref.kind));
        break;
      default:
        break;
    }
    json_.endObject();
  }

  // ---- ids ------------------------------------------------------------------------------------

  long routineId(a_routine_ptr r) {
    auto it = routineIds_.find(r);
    if (it != routineIds_.end()) return it->second;
    long id = static_cast<long>(routineIds_.size()) + 1;
    routineIds_[r] = id;
    return id;
  }

  long variableId(a_variable_ptr v) {
    auto it = variableIds_.find(v);
    if (it != variableIds_.end()) return it->second;
    long id = static_cast<long>(variableIds_.size()) + 1;
    variableIds_[v] = id;
    return id;
  }

  // A routine referred to but declared in a scope the walk does not reach (a block-scope
  // declaration) is written with the others.
  void noteRoutine(a_routine_ptr r) {
    if (r->is_prototype_instantiation) return;
    if (seenRoutines_.insert(r).second) routines_.push_back(r);
  }

  // A variable with static storage referred to but declared at block scope (`extern int g;`
  // inside a function) is written with the globals.
  void noteGlobal(a_variable_ptr v) { (void)v; }

  static const int MaxDepth = 2000;

  JsonWriter& json_;
  std::string root_;
  std::vector<a_source_file_ptr> files_;
  std::unordered_map<a_source_file_ptr, long> fileIds_;
  std::unordered_map<a_source_file_ptr, bool> inRootCache_;
  std::unordered_map<a_macro_ptr, long> macroIds_;
  std::vector<a_type_ptr> types_;
  std::unordered_map<a_type_ptr, long> typeIds_;
  std::unordered_map<a_routine_ptr, long> routineIds_;
  std::unordered_map<a_variable_ptr, long> variableIds_;
  std::vector<a_routine_ptr> routines_;
  std::unordered_set<a_routine_ptr> seenRoutines_;
  std::vector<a_variable_ptr> globals_;
  std::set<std::string> inlineNamespaces_;
  an_il_to_str_output_control_block octl_;
  a_text_buffer_ptr textBuffer_ = nullptr;
  int depth_ = 0;
};

// The document stays open after the back end: the diagnostics are added when the front end has
// finished writing them.
std::FILE* openOutput() {
  if (options().out == "-") return stdout;
  std::FILE* f = std::fopen(options().out.c_str(), "wb");
  if (f == nullptr) std::fprintf(stderr, "edga: cannot write %s\n", options().out.c_str());
  return f;
}

// The back end's document goes to a temporary file when the output is stdout, so a back end that
// stops part way never leaves half a document there.
std::FILE* openBackEndOutput() {
  if (options().out != "-") return openOutput();
  std::FILE* f = std::tmpfile();
  if (f == nullptr) std::fprintf(stderr, "edga: cannot create a temporary file\n");
  return f;
}

std::FILE* output = nullptr;
JsonWriter* writer = nullptr;

void writeTool(JsonWriter& json) {
  json.key("tool");
  json.beginObject();
  json.field("name", "edga");
  json.field("version", EDGA_VERSION);
  json.field("edgCommit", EDGA_EDG_COMMIT);
  json.field("config", EDGA_CONFIG_NAME "-" EDGA_CONFIG_FINGERPRINT);
  json.endObject();
}

void writeDiagnostics(JsonWriter& json, const std::vector<Diagnostic>& diagnostics) {
  json.key("diagnostics");
  json.beginArray();
  for (const Diagnostic& d : diagnostics) {
    json.beginObject();
    json.field("level", d.level);
    json.field("code", d.code);
    json.field("message", d.message);
    if (!d.file.empty()) {
      json.field("file", d.file);
      json.field("line", static_cast<long long>(d.line));
      json.field("column", static_cast<long long>(d.column));
    }
    json.endObject();
  }
  json.endArray();
}

}  // namespace

void finishUnit(const std::vector<Diagnostic>& diagnostics) {
  if (writer == nullptr) return;
  writeDiagnostics(*writer, diagnostics);
  writer->endObject();
  std::fputc('\n', output);
  delete writer;
  writer = nullptr;
  if (options().out == "-") {
    std::rewind(output);
    char buffer[1 << 16];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, output)) > 0;)
      std::fwrite(buffer, 1, n, stdout);
    std::fflush(stdout);
  }
  std::fclose(output);
  output = nullptr;
}

void writeFailedUnit(const char* sourceFile, const std::vector<Diagnostic>& diagnostics) {
  // A back end that stopped part way (an internal error in the front end's routines) leaves its
  // document open: drop it, so the failed document replaces it instead of being written over it.
  if (writer != nullptr) {
    delete writer;
    writer = nullptr;
  }
  if (output != nullptr) std::fclose(output);
  output = nullptr;
  std::FILE* f = openOutput();
  if (f == nullptr) return;
  JsonWriter json(f);
  json.beginObject();
  json.field("schema", "edga/1");
  writeTool(json);
  json.key("tu");
  json.beginObject();
  json.field("path", sourceFile);
  json.field("status", "failed");
  json.endObject();
  writeDiagnostics(json, diagnostics);
  json.endObject();
  std::fputc('\n', f);
  if (f != stdout) std::fclose(f);
}

}  // namespace edga

void back_end(void) {
  edga::output = edga::openBackEndOutput();
  if (edga::output == nullptr) return;
  edga::writer = new edga::JsonWriter(edga::output);
  edga::writer->beginObject();
  edga::writer->field("schema", "edga/1");
  edga::writeTool(*edga::writer);
  edga::Exporter(*edga::writer).run();
  std::fflush(edga::output);
  edga::backEndRan = true;
}
