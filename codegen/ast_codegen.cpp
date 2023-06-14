#include "ast.h"
#include "sem_error.h"
#include "type.h"
#include "codegen/codegen_utility.h"
#include <string>
#include <cassert>
namespace ast{

template <class... Ts> struct overloaded : Ts...{using Ts::operator()...;};
template<class...Ts> overloaded(Ts ...) -> overloaded<Ts...>;
namespace{

const auto assignment_op = std::map<token::TokenType,token::TokenType>{{
    {token::TokenType::PlusAssign, token::TokenType::Plus},
    {token::TokenType::MinusAssign, token::TokenType::Minus},
    {token::TokenType::MultAssign, token::TokenType::Star},
    {token::TokenType::DivAssign, token::TokenType::Div},
    {token::TokenType::ModAssign, token::TokenType::Mod},
    {token::TokenType::LSAssign, token::TokenType::LShift},
    {token::TokenType::RSAssign, token::TokenType::RShift},
    {token::TokenType::BAAssign, token::TokenType::Amp},
    {token::TokenType::BOAssign, token::TokenType::BitwiseOr},
    {token::TokenType::BXAssign, token::TokenType::BitwiseXor},
}};

value::Value* assignment_codegen(const ast::BinaryOp* node, std::ostream& output, context::Context& c){
    auto right_register = node->right->codegen(output, c);
    right_register = codegen_utility::convert(node->new_right_type, std::move(right_register), output, c);
    auto ast_variable = dynamic_cast<const ast::Variable*>(node->left.get());
    assert(ast_variable && "Other lvalues not yet implemented");
    auto var_value = c.get_value(ast_variable->variable_name);

    value::Value* result = nullptr;
    if(assignment_op.find(node->tok.type) != assignment_op.end()){
        auto loaded_value = codegen_utility::make_load(var_value, output, c);
        loaded_value = codegen_utility::convert(node->new_left_type,loaded_value, output, c);
        auto op_type = assignment_op.at(node->tok.type);
        result = codegen_utility::bin_op_codegen(loaded_value, right_register, op_type, node->type, output, c);
    }else{
        assert(node->tok.type == token::TokenType::Assign && "Unknown assignment op");
        result = codegen_utility::convert(node->type, right_register, output, c);
    }
    result = codegen_utility::convert(var_value->get_type(), result, output, c);
    codegen_utility::make_store(result, var_value, output, c);
    return result;
}
value::Value* short_circuit_codegen(const ast::BinaryOp* node, std::ostream& output, context::Context& c){
    int instruction_number = c.new_local_name(); 
    std::string no_sc_label = "logical_op_no_sc."+std::to_string(instruction_number);
    std::string end_label = "logical_op_end."+std::to_string(instruction_number);

    auto left_register = node->left->codegen(output, c);
    left_register = codegen_utility::convert(type::IType::Bool,left_register, output, c);

    auto new_tmp = codegen_utility::make_tmp_alloca(type::IType::Bool, output, c);
    codegen_utility::make_store(left_register, new_tmp, output, c);

    switch(node->tok.type){
        case token::TokenType::And:
            c.change_block(no_sc_label, output, 
                std::make_unique<basicblock::Cond_BR>(left_register, no_sc_label,end_label));
            break;
        case token::TokenType::Or:
            c.change_block(no_sc_label, output, 
                std::make_unique<basicblock::Cond_BR>(left_register, end_label,no_sc_label));
            break;
        default:
            assert(false && "Unknown binary assignment op during codegen");
    }
    auto right_register = node->right->codegen(output, c);
    right_register = codegen_utility::convert(type::IType::Bool,right_register, output, c);
    value::Value* no_sc_result = nullptr;
    switch(node->tok.type){
        case token::TokenType::And:
            no_sc_result = codegen_utility::make_command(type::from_str("_Bool"),"and",left_register,right_register,output,c);
            break;
        case token::TokenType::Or:
            no_sc_result = codegen_utility::make_command(type::from_str("_Bool"),"or",left_register,right_register,output,c);
            break;
        default:
            assert(false && "Unknown binary assignment op during codegen");
    }
    codegen_utility::make_store(no_sc_result, new_tmp, output, c);
    c.change_block(end_label,output,std::make_unique<basicblock::UCond_BR>(end_label));

    auto result = codegen_utility::make_load(new_tmp, output, c);
    result = codegen_utility::convert(node->type, result, output, c);
    return result;
}
value::Value* other_bin_op_codegen(const ast::BinaryOp* node, std::ostream& output, context::Context& c){
    auto left_register = node->left->codegen(output, c);
    left_register = codegen_utility::convert(node->new_left_type, std::move(left_register), output, c);
    auto right_register = node->right->codegen(output, c);
    right_register = codegen_utility::convert(node->new_right_type, std::move(right_register), output, c);
    return codegen_utility::bin_op_codegen(left_register, right_register, node->tok.type, node->type, output, c);
}
void global_basic_type_codegen(const std::string& name, type::BasicType t, value::Value* def, std::ostream& output, context::Context& c){
    output << name <<" = dso_local global "<<type::ir_type(t)<<" ";
    if(def){
        output << def->get_value() << std::endl;
    }else{
        output << std::visit(overloaded{
                [](type::IType){return "0";},
                [](type::FType){return "0.0";},
                }, t) << std::endl;
    }
}
void global_func_type_codegen(const std::string& name, const type::FuncType& t, std::ostream& output){
    output << "declare "<<type::ir_type(t.return_type())<<" "<<name<<"(";
    if(t.has_prototype()){
        auto pt_list = t.param_types();
        if(pt_list.size() > 0){
            for(int i=0; i<pt_list.size()-1; i++){
                output<< ir_type(pt_list.at(i))<<" noundef,";
            }
            output<< ir_type(pt_list.back())<<" noundef";
            if(t.is_variadic()){
                output<<",...";
            }
        }
    }else{
        output<<"...";
    }
    output<<")";
}


void global_decl_codegen(value::Value* value, std::ostream& output, context::Context& c, value::Value* def = nullptr){
    std::visit(type::make_visitor<void>(
        [&](const type::BasicType& bt){global_basic_type_codegen(value->get_value(),bt, def, output, c);}, 
        [](const type::VoidType& vt){assert(false && "Cannot have variable of void type");}, 
        [](const type::PointerType& pt){assert(false && "Have not yet implemented variables of pointer type");}, 
        [&value, &output](const type::FuncType& ft){global_func_type_codegen(value->get_value(), ft, output);}
    ), value->get_type());
}

} //namespace


value::Value* Program::codegen(std::ostream& output, context::Context& c)const {
    output<<R"(target triple = "x86_64-unknown-linux-gnu")"<<std::endl;
    for(const auto& decl : decls){
        decl->codegen(output, c);
    }
    auto undefined_symbols = c.undefined_globals();
    for(const auto& value : undefined_symbols){
        global_decl_codegen(value, output, c);
    }
    return nullptr;
}
value::Value* GotoStmt::codegen(std::ostream& output, context::Context& c)const {
    int instruction_number = c.new_local_name(); 
    std::string ir_label = ident_tok.value+".label";
    c.change_block("aftergoto."+std::to_string(instruction_number),output, 
        std::make_unique<basicblock::UCond_BR>(ir_label));
    return nullptr;
}
value::Value* LabeledStmt::codegen(std::ostream& output, context::Context& c)const {
    std::string ir_label = ident_tok.value+".label";
    c.change_block(ir_label, output, nullptr);
    stmt->codegen(output, c);
    return nullptr;
}
value::Value* NullStmt::codegen(std::ostream& output, context::Context& c)const {
    //Do nothing
    return nullptr;
}
value::Value* Conditional::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    auto condition = cond->codegen(output, c);
    condition = codegen_utility::convert(type::IType::Bool,condition, output, c);
    auto new_tmp = codegen_utility::make_tmp_alloca(this->type, output, c);

    int instruction_number = c.new_local_name(); 
    std::string true_label = "condtrue."+std::to_string(instruction_number);
    std::string false_label = "condfalse." + std::to_string(instruction_number);
    std::string end_label = "condend."+std::to_string(instruction_number);
    c.change_block(true_label, output, 
        std::make_unique<basicblock::Cond_BR>(condition, true_label,false_label));
    auto t_value = true_expr->codegen(output, c);
    codegen_utility::make_store(t_value,new_tmp, output, c);

    c.change_block(false_label, output,std::make_unique<basicblock::UCond_BR>(end_label));  
    auto f_value = false_expr->codegen(output, c);
    codegen_utility::make_store(f_value,new_tmp, output, c);

    c.change_block(end_label,output,std::make_unique<basicblock::UCond_BR>(end_label));
    return codegen_utility::make_load(new_tmp, output, c);
}

value::Value* IfStmt::codegen(std::ostream& output, context::Context& c)const {
    auto condition = if_condition->codegen(output, c);
    condition = codegen_utility::convert(type::IType::Bool,condition, output, c);
    int instruction_number = c.new_local_name(); 
    std::string true_label = "iftrue."+std::to_string(instruction_number);
    std::string end_label = "ifend."+std::to_string(instruction_number);
    std::string false_label;
    if(this->else_body.has_value()){
        false_label = "iffalse."+std::to_string(instruction_number);
    }else{
        false_label = "ifend."+std::to_string(instruction_number);
    }
    c.change_block(true_label, output, 
        std::make_unique<basicblock::Cond_BR>(condition, true_label,false_label));

    if_body->codegen(output, c);

    if(this->else_body.has_value()){
        c.change_block(false_label, output,std::make_unique<basicblock::UCond_BR>(end_label));
        else_body.value()->codegen(output, c);
    }
    c.change_block(end_label,output,std::make_unique<basicblock::UCond_BR>(end_label));
    return nullptr;
}
value::Value* CompoundStmt::codegen(std::ostream& output, context::Context& c)const {
    c.enter_scope();
    for(const auto& stmt : stmt_body){
        stmt->codegen(output, c);
    }
    c.exit_scope();
    return nullptr;
}
value::Value* FunctionDef::codegen(std::ostream& output, context::Context& c)const {
    auto f_type = std::get<type::DerivedType>(this->type).get<type::FuncType>();
    assert(!std::holds_alternative<type::DerivedType>(f_type.return_type()) && "Cannot yet return derived types");
    if(this->tok.value == "main"){
        assert(f_type.return_type() == type::CType(type::IType::Int));
    }
    auto func_value = c.add_global(this->name, this->type, true);
    AST::print_whitespace(c.depth(), output);
    output << "define dso_local "<<type::ir_type(f_type.return_type())<<" "<<func_value->get_value();

    auto param_types = std::vector<type::CType>{};
    for(const auto& p : params){
        param_types.push_back(p->type);
    }
    c.enter_function(f_type.return_type(), param_types, output); 
    for(int i = 0; i < params.size(); i++){
        auto memory_var = params.at(i)->codegen(output, c);
        auto passed_val = c.prev_temp(params.size()-1-i);
        assert(passed_val != nullptr && "Could not find temp variable for passed value");
        codegen_utility::make_store(passed_val,memory_var, output, c);
    }
    function_body->codegen(output, c);
    c.exit_function(output);
    //Ultimately return value with
    //full function signature type
    //Once we add function argument/function types
    return nullptr;
}

value::Value* ReturnStmt::codegen(std::ostream& output, context::Context& c)const {
    value::Value* return_value = nullptr;
    if(return_expr.has_value()){
        return_value = return_expr.value()->codegen(output, c);
        assert(type::is_type<type::BasicType>(c.return_type()) && "Can't return derived types yet");
        return_value = codegen_utility::convert(c.return_type(),std::move(return_value), output, c);
    }
    int instruction_number = c.new_local_name(); 
    c.change_block("afterret."+std::to_string(instruction_number),output, 
        std::make_unique<basicblock::RET>(return_value));
    return nullptr;
}

value::Value* Variable::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    auto var_value = c.get_value(variable_name);
    return codegen_utility::make_load(var_value,output,c);
}

value::Value* DoStmt::codegen(std::ostream& output, context::Context& c)const {
    int instruction_number = c.new_local_name(); 
    std::string control_label = "docontrol."+std::to_string(instruction_number);
    std::string body_label = "dobody."+std::to_string(instruction_number);
    std::string end_label = "doend."+std::to_string(instruction_number);
    c.continue_targets.push_back(control_label);
    c.break_targets.push_back(end_label);

    c.change_block(body_label,output,nullptr);
    body->codegen(output, c);
    c.change_block(control_label,output,nullptr);
    auto control_value = codegen_utility::convert(type::from_str("_Bool"),control_expr->codegen(output, c),output, c);
    c.change_block(end_label,output,std::make_unique<basicblock::Cond_BR>(control_value, body_label,end_label));
    c.continue_targets.pop_back();
    c.break_targets.pop_back();
    return nullptr;
}
value::Value* CaseStmt::codegen(std::ostream& output, context::Context& c)const {
    std::string case_val = std::to_string(std::stoull(this->label->literal));
    std::string case_label = "case."+std::to_string(c.switch_numbers.back())+"."+case_val;
    c.change_block(case_label, output, nullptr);
    stmt->codegen(output, c);
    return nullptr;
}
value::Value* DefaultStmt::codegen(std::ostream& output, context::Context& c)const {
    std::string case_label = "case."+std::to_string(c.switch_numbers.back())+".default";
    c.change_block(case_label, output, nullptr);
    stmt->codegen(output, c);
    return nullptr;
}
value::Value* SwitchStmt::codegen(std::ostream& output, context::Context& c)const {
    assert(case_table && "Switch statement not analyzed");
    auto control_value = codegen_utility::convert(control_type, control_expr->codegen(output, c), output, c);
    const auto instruction_number = c.new_local_name(); 
    std::string end_label = "switchend."+std::to_string(instruction_number);
    std::string case_label_head = "case."+std::to_string(instruction_number)+".";
    std::string default_label = "switchend."+std::to_string(instruction_number);
    c.switch_numbers.push_back(instruction_number);
    c.break_targets.push_back(end_label);
    if(case_table->find(std::nullopt) != case_table->end()){
        default_label = "case."+std::to_string(instruction_number)+".default";
    }

    AST::print_whitespace(c.depth(), output);
    output << "switch "<<type::ir_type(control_type)<<" "<<control_value->get_value()<<", label %";
    output<<default_label<<" [ "<<std::endl;
    for(const auto& case_val : *case_table){
        if(case_val.has_value()){
            AST::print_whitespace(c.depth()+5, output);
            output << type::ir_type(control_type)<<" "<<case_val.value()<<", label %";
            output << case_label_head <<case_val.value()<<std::endl;
        }
    }
    AST::print_whitespace(c.depth(), output);
    output<<" ] "<<std::endl;
    output<<"afterswitchcontrol."+std::to_string(instruction_number)<<":"<<std::endl;

    switch_body->codegen(output, c);
    c.change_block(end_label, output, nullptr);
    c.break_targets.pop_back();
    c.switch_numbers.pop_back();
    //To do
    return nullptr;
}
value::Value* WhileStmt::codegen(std::ostream& output, context::Context& c)const {
    int instruction_number = c.new_local_name(); 
    std::string control_label = "whilecontrol."+std::to_string(instruction_number);
    std::string body_label = "whilebody."+std::to_string(instruction_number);
    std::string end_label = "whileend."+std::to_string(instruction_number);
    c.continue_targets.push_back(control_label);
    c.break_targets.push_back(end_label);

    c.change_block(control_label,output,nullptr);
    auto control_value = codegen_utility::convert(type::from_str("_Bool"),control_expr->codegen(output, c),output, c);
    c.change_block(body_label,output,std::make_unique<basicblock::Cond_BR>(control_value, body_label,end_label));
    body->codegen(output, c);
    c.change_block(end_label,output,std::make_unique<basicblock::UCond_BR>(control_label));
    c.continue_targets.pop_back();
    c.break_targets.pop_back();
    return nullptr;
}
value::Value* BreakStmt::codegen(std::ostream& output, context::Context& c)const {
    int instruction_number = c.new_local_name(); 
    c.change_block("afterbreak."+std::to_string(instruction_number),output, 
        std::make_unique<basicblock::UCond_BR>(c.break_targets.back()));
    return nullptr;
}
value::Value* ContinueStmt::codegen(std::ostream& output, context::Context& c)const {
    int instruction_number = c.new_local_name(); 
    c.change_block("aftercont."+std::to_string(instruction_number),output, 
        std::make_unique<basicblock::UCond_BR>(c.continue_targets.back()));
    return nullptr;
}
value::Value* ForStmt::codegen(std::ostream& output, context::Context& c)const {
    //Initialize
    c.enter_scope();
    int instruction_number = c.new_local_name(); 
    std::string control_label = "forcontrol."+std::to_string(instruction_number);
    std::string body_label = "forbody."+std::to_string(instruction_number);
    std::string post_label = "forpost."+std::to_string(instruction_number);
    std::string end_label = "forend."+std::to_string(instruction_number);
    c.continue_targets.push_back(post_label);
    c.break_targets.push_back(end_label);

    //Generate code
    std::visit(overloaded{
        [&](std::monostate) -> void{},
        [&](const auto& ast_node) -> void{
            ast_node->codegen(output, c);
            },
    },this->init_clause);

    c.change_block(control_label,output,nullptr);
    auto control_value = codegen_utility::convert(type::from_str("_Bool"),control_expr->codegen(output, c),output, c);
    c.change_block(body_label,output,std::make_unique<basicblock::Cond_BR>(control_value, body_label,end_label));

    this->body->codegen(output, c);
    c.change_block(post_label,output,nullptr);
    if(this->post_expr.has_value()){
        this->post_expr.value()->codegen(output, c);
    }
    c.change_block(end_label,output,std::make_unique<basicblock::UCond_BR>(control_label));

    //Clean up
    c.continue_targets.pop_back();
    c.break_targets.pop_back();
    c.exit_scope();
    return nullptr;
}
value::Value* DeclList::codegen(std::ostream& output, context::Context& c)const {
    for(const auto& decl : decls){
        decl->codegen(output, c);
    }
    return nullptr;
}
value::Value* FunctionDecl::codegen(std::ostream& output, context::Context& c)const {
    c.add_global(this->name, this->type);
    return nullptr;
}
value::Value* VarDecl::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    if(c.in_function()){
        auto variable = c.add_local(name, type);
        AST::print_whitespace(c.depth(), output);
        output << variable->get_value() <<" = alloca "<<type::ir_type(variable->get_type()) <<std::endl;
        if(this->assignment.has_value()){
            this->assignment.value()->codegen(output, c);
        }
        return variable;
    }else{
        auto value = c.add_global(this->name, this->type, assignment.has_value());
        if(assignment.has_value()){
            auto const_value = dynamic_cast<ast::Constant*>(assignment.value()->right.get());
            assert(const_value && "Global var must be initalized by literal");
            global_decl_codegen(value, output, c, const_value->codegen(output,c));
        }
        return value;
    }
}

value::Value* Constant::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    return c.add_literal(this->literal, this->type);
}
value::Value* FuncCall::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    auto function = c.get_value(this->func_name);
    auto ft =std::get<type::DerivedType>(function->get_type()).get<type::FuncType>();

    auto arg_values = std::vector<value::Value*>{};
    for(auto& expr : this->args){
        arg_values.push_back(expr->codegen(output, c));
    }
    value::Value* return_val = nullptr;
    print_whitespace(c.depth(), output);
    if(ft.return_type() != type::CType(type::VoidType())){
        return_val = c.new_temp(ft.return_type());
        output << return_val->get_value() <<" = ";
    }
    output << "call "<<type::ir_type(ft.return_type());
    output <<" "<<function->get_value()<<"(";
    if(arg_values.size() > 0){
        for(int i=0; i<arg_values.size() - 1; i++){
            output<<type::ir_type(arg_values.at(i)->get_type())<<" noundef "<<arg_values.at(i)->get_value()<<", ";
        }
        output<<type::ir_type(arg_values.back()->get_type())<<" noundef "<<arg_values.back()->get_value();
    }
    output<<")"<<std::endl;
    return return_val;
}

value::Value* Postfix::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    auto operand = arg->codegen(output, c);
    std::string t = type::ir_type(this->type);
    std::string command = "";
    value::Value* new_temp = nullptr;
    switch(tok.type){
        case token::TokenType::Plusplus:
        {
            auto variable = dynamic_cast<const ast::Variable*>(arg.get());
            assert(variable && "Other lvalues not yet implemented");
            auto var_reg = c.get_value(variable->variable_name);
            new_temp = codegen_utility::make_load(var_reg, output, c);
            command = std::visit(overloaded{
                [](type::IType){return "add";},
                [](type::FType){return "fadd";},
                }, std::get<type::BasicType>(this->type));
            
            AST::print_whitespace(c.depth(), output);
            auto var_temp = c.new_temp(this->type);
            output << var_temp->get_value()<<" = "<<command<<" "<<t<<" "<<new_temp->get_value()<<std::visit(overloaded{
                [](type::IType){return ", 1";},
                [](type::FType){return ", 1.0";},
                }, std::get<type::BasicType>(this->type)) <<std::endl;
            codegen_utility::make_store(var_temp,var_reg, output, c);
        }
            return new_temp;
        case token::TokenType::Minusminus:
        {
            auto variable = dynamic_cast<const ast::Variable*>(arg.get());
            assert(variable && "Other lvalues not yet implemented");
            auto var_reg = c.get_value(variable->variable_name);
            new_temp = codegen_utility::make_load(var_reg, output, c);
            command = std::visit(overloaded{
                [](type::IType){return "sub";},
                [](type::FType){return "fsub";},
                }, std::get<type::BasicType>(this->type));
            
            AST::print_whitespace(c.depth(), output);
            auto var_temp = c.new_temp(this->type);
            output << var_temp->get_value()<<" = "<<command<<" "<<t<<" "<<new_temp->get_value()<<std::visit(overloaded{
                [](type::IType){return ", 1";},
                [](type::FType){return ", 1.0";},
                }, std::get<type::BasicType>(this->type)) <<std::endl;
            codegen_utility::make_store(var_temp,var_reg, output, c);
        }
            return new_temp;
        default:
            assert(false && "Operator Not Implemented");
    }
}
value::Value* UnaryOp::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    auto operand = arg->codegen(output, c);
    std::string t = type::ir_type(this->type);
    std::string command = "";
    value::Value* new_temp = nullptr;
    switch(tok.type){
        case token::TokenType::Plusplus:
        {
            auto variable = dynamic_cast<const ast::Variable*>(arg.get());
            assert(variable && "Other lvalues not yet implemented");
            auto var_reg = c.get_value(variable->variable_name);
            auto var_temp = codegen_utility::make_load(var_reg, output, c);
            var_temp =  codegen_utility::convert(this->type, var_temp, output, c);
            command = std::visit(type::make_visitor<std::string>(
                [](type::IType){return "add";},
                [](type::FType){return "fadd";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Have not implemented operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), var_temp->get_type());
            
            AST::print_whitespace(c.depth(), output);
            new_temp = c.new_temp(this->type);
            output << new_temp->get_value()<<" = "<<command<<" "<<t<<" "<<var_temp->get_value()<<std::visit(type::make_visitor<std::string>(
                [](type::IType){return ", 1";},
                [](type::FType){return ", 1.0";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Have not implemented operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), var_temp->get_type()) <<std::endl;
            codegen_utility::make_store(new_temp,var_reg, output, c);
        }
            return new_temp;
        case token::TokenType::Minusminus:
        {
            auto variable = dynamic_cast<const ast::Variable*>(arg.get());
            assert(variable && "Other lvalues not yet implemented");
            auto var_reg = c.get_value(variable->variable_name);
            auto var_temp = codegen_utility::make_load(var_reg, output, c);
            var_temp =  codegen_utility::convert(this->type, var_temp, output, c);
            command = std::visit(type::make_visitor<std::string>(
                [](type::IType){return "sub";},
                [](type::FType){return "fsub";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Have not implemented operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), var_temp->get_type());
            
            AST::print_whitespace(c.depth(), output);
            new_temp = c.new_temp(this->type);
            output << new_temp->get_value()<<" = "<<command<<" "<<t<<" "<<var_temp->get_value()<<std::visit(type::make_visitor<std::string>(
                [](type::IType){return ", 1";},
                [](type::FType){return ", 1.0";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Have not implemented operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), var_temp->get_type()) <<std::endl;
            codegen_utility::make_store(new_temp,var_reg, output, c);
        }
            return new_temp;
        case token::TokenType::Plus:
            return codegen_utility::convert(this->type, std::move(operand), output, c);
        case token::TokenType::Minus:
            operand =  codegen_utility::convert(this->type, std::move(operand), output, c);
            //sub or fsub
            command = std::visit(type::make_visitor<std::string>(
                [](type::IType){return "sub";},
                [](type::FType){return "fsub";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Cannot do operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), operand->get_type());
            
            AST::print_whitespace(c.depth(), output);
            new_temp = c.new_temp(this->type);
            output << new_temp->get_value()<<" = "<<command<<" "<<t<<std::visit(type::make_visitor<std::string>(
                [](type::IType){return " 0, ";},
                [](type::FType){return " 0.0, ";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Cannot do operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), operand->get_type()) <<operand->get_value() <<std::endl;
            return new_temp;
        case token::TokenType::BitwiseNot:
            operand =  codegen_utility::convert(this->type, std::move(operand), output, c);
            AST::print_whitespace(c.depth(), output);
            new_temp = c.new_temp(this->type);
            output << new_temp->get_value()<<" = xor "<<t<<" -1, " <<operand->get_value() <<std::endl;
            return new_temp;
        case token::TokenType::Not:
        {
            assert(t == "i32");
            //icmp or fcmp
            command = std::visit(type::make_visitor<std::string>(
                [](type::IType){return "icmp eq";},
                [](type::FType){return "fcmp oeq";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Have not implemented operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), operand->get_type());

            AST::print_whitespace(c.depth(), output);
            auto intermediate_bool = c.new_temp(type::IType::Bool);
            output << intermediate_bool->get_value() <<" = "<<command<<" "<<type::ir_type(operand->get_type());
            output << std::visit(type::make_visitor<std::string>(
                [](type::IType){return " 0, ";},
                [](type::FType){return " 0.0, ";},
                [](type::FuncType){throw std::runtime_error("Cannot do operation on function type");},
                [](type::PointerType){throw std::runtime_error("Have not implemented operation on pointer type");},
                [](type::VoidType){throw std::runtime_error("Cannot do operation on void type");}
                ), operand->get_type()) << operand->get_value() <<std::endl;

            new_temp = codegen_utility::convert(this->type, intermediate_bool, output, c);
        }
            return new_temp;
        default:
            assert(false && "Operator Not Implemented");
    }
}


value::Value* BinaryOp::codegen(std::ostream& output, context::Context& c)const {
    assert(this->analyzed && "This AST node has not had analysis run on it");
    switch(tok.type){
        case token::TokenType::PlusAssign:
        case token::TokenType::MinusAssign:
        case token::TokenType::DivAssign:
        case token::TokenType::MultAssign:
        case token::TokenType::ModAssign:
        case token::TokenType::BAAssign:
        case token::TokenType::BOAssign:
        case token::TokenType::BXAssign:
        case token::TokenType::LSAssign:
        case token::TokenType::RSAssign:
        case token::TokenType::Assign:
            return assignment_codegen(this, output, c);
        case token::TokenType::And:
        case token::TokenType::Or:
            return short_circuit_codegen(this, output, c);
        case token::TokenType::Minus:
        case token::TokenType::Plus:
        case token::TokenType::Div:
        case token::TokenType::Star:
        case token::TokenType::Mod:
        case token::TokenType::Equal:
        case token::TokenType::NEqual:
        case token::TokenType::Less:
        case token::TokenType::Greater:
        case token::TokenType::LEq:
        case token::TokenType::GEq:
        case token::TokenType::LShift:
        case token::TokenType::RShift:
        case token::TokenType::Amp:
        case token::TokenType::BitwiseOr:
        case token::TokenType::BitwiseXor:
        case token::TokenType::Comma:
            return other_bin_op_codegen(this, output, c);
        default:
            assert(false && "Unknown binary assignment op during codegen");
    }
}

} //namespace ast
