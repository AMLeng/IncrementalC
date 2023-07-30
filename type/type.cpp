#include "type/type_basic.h"
#include "type/type_func.h"
#include "type/type_pointer.h"
#include "type.h"
namespace type{

namespace{


} //namespace
DerivedType::DerivedType(const DerivedType& other){
    this->type = std::visit(overloaded{
        [](const auto& p)-> DerivedPointers {return p->copy();}
    }, other.type);
}
DerivedType& DerivedType::operator=(const DerivedType& other){
    this->type = std::visit(overloaded{
        [](const auto& p)-> DerivedPointers {return p->copy();}
    }, other.type);
    return *this;
}

bool DerivedType::operator ==(const DerivedType& other) const{
    return std::visit(overloaded{
        //Cases for non-array types are all the same
        [&type2 = std::as_const(other.type)](const auto& type1){
            if(!std::holds_alternative<std::decay_t<decltype(type1)>>(type2)){
                return false;
            }
            const auto& t2 = std::get<std::decay_t<decltype(type1)>>(type2);
            assert(type1 && t2 && "Invalid derived type containing nullptr");
            if constexpr(std::is_same_v<std::decay_t<decltype(type1)>,std::unique_ptr<PointerType>>){
                if(auto p = dynamic_cast<ArrayType*>(type1.get())){
                    return *p == *dynamic_cast<ArrayType*>(std::get<std::unique_ptr<PointerType>>(type2).get());
                }
            }
            return *type1 == *t2;
        },
    }, this->type);
}
bool DerivedType::operator !=(const DerivedType& other) const{
    return !this->operator==(other);
}

bool CType::operator ==(const CType& other) const{
    return this->type == other.type;
}
bool CType::operator !=(const CType& other) const{
    return this->type != other.type;
}
bool is_compatible(const CType& type1, const CType& type2){
    return visit(make_visitor<bool>(
        [&type2](VoidType){return is_type<VoidType>(type2);},
        [&type2](const FuncType& type1){return is_type<std::decay_t<decltype(type1)>>(type2)
                            && is_compatible(type1, get<std::decay_t<decltype(type1)>>(type2));},
        [&type2](const ArrayType& type1){return is_type<std::decay_t<decltype(type1)>>(type2)
                            && is_compatible(type1, get<std::decay_t<decltype(type1)>>(type2));},
        [&type2](const PointerType& type1){return is_type<std::decay_t<decltype(type1)>>(type2)
                            && is_compatible(type1, get<std::decay_t<decltype(type1)>>(type2));},
        [&type2](const auto& type1){return is_type<std::decay_t<decltype(type1)>>(type2)
                            && type1 == get<std::decay_t<decltype(type1)>>(type2);}
    ), type1);
}
bool can_cast(const CType& type1, const CType& type2){
    if(can_assign(type1,type2)){
        return true;
    }
    if(!is_type<PointerType>(type1)){
        //Must be an int for int to pointer conversion
        return is_type<IType>(type1) && is_type<PointerType>(type2);
    }
    if(!is_type<PointerType>(type2)){
        //Must be an int for pointer to int conversion
        return is_type<IType>(type2) && is_type<PointerType>(type1);
    }
    return is_type<FuncType>(type::get<PointerType>(type1).pointed_type()) 
            == is_type<FuncType>(type::get<PointerType>(type2).pointed_type());
}
bool can_assign(const CType& type1, const CType& type2){
    //Leaves out the case of assigning a nullptr constant,
    //Since that requires the actual value and not just the type
    return visit(make_visitor<bool>(
        [&type2](VoidType){return is_type<VoidType>(type2);},
        [&type2](BasicType type1){
            return is_type<BasicType>(type2);
            },
        [&type2](PointerType type1){return type2 == CType(IType::Bool)
            || is_type<PointerType>(type2) && can_assign(type1, type::get<PointerType>(type2));},
        [&type2](StructType type1){return is_type<StructType>(type2) && type1 == type::get<StructType>(type2);},
        [&type2](UnionType type1){return is_type<UnionType>(type2) && type1 == type::get<UnionType>(type2);},
        [&type2](FuncType type1){return is_type<FuncType>(type2);}
    ),type1);
}
std::string to_string(const CType& type){
    return visit(make_visitor<std::string>(
        [](VoidType v)->std::string{return "void";},
        [](IType t)->std::string{return to_string(t);},
        [](FType t)->std::string{return to_string(t);},
        [](const auto& t){return t.to_string();}
    ), type);
}

bool is_signed_int(CType type){
    return is_type<BasicType>(type) && is_signed_int(get<BasicType>(type));
}
bool is_unsigned_int(CType type){
    return is_type<BasicType>(type) && is_unsigned_int(get<BasicType>(type));
}
bool is_float(CType type){
    return type::is_type<type::FType>(type);
}
bool is_int(CType type){
    return type::is_type<type::IType>(type);
}
bool is_arith(CType type){
    return type::is_type<type::BasicType>(type);
}
bool is_scalar(CType type){
    return type::is_type<type::BasicType>(type) || type::is_type<type::PointerType>(type);
}
BasicType usual_arithmetic_conversions(CType type1, CType type2){
    try{
        auto itype1 = get<BasicType>(type1);
        auto itype2 = get<BasicType>(type2);
        return usual_arithmetic_conversions(itype1, itype2);
    }catch(std::exception& e){
        throw std::runtime_error("Failure to do integer promotions on types "+type::to_string(type1) + "and "+type::to_string(type2));
    }
}
BasicType integer_promotions(const CType& type){
    try{
        auto itype = get<BasicType>(type);
        return integer_promotions(itype);
    }catch(std::exception& e){
        throw std::runtime_error("Failure to do integer promotions on type "+type::to_string(type));
    }
}
std::string ir_type(const CType& type){
    return visit(make_visitor<std::string>(
        [](VoidType v)->std::string{return "void";},
        [](IType bt)->std::string{return ir_type(bt);},
        [](FType bt)->std::string{return ir_type(bt);},
        [](const auto& t){return t.ir_type();}
    ), type);
}
long long int size(const CType& type){
    return visit(make_visitor<int>(
        [](VoidType v){return 0;},
        [](BasicType bt){return byte_size(bt);},
        [](const FuncType& ft){throw std::runtime_error("Cannot take size of function type");},
        [](const PointerType& pt){return 8;},
        [&](const ArrayType& at){return at.size()*type::size(at.pointed_type());},
        [&](const StructType& st){return st.size(CType::tags);},
        [&](const UnionType& st){return st.size(CType::tags);}
    ), type);
}
long long int align(const CType& type){
    return visit(make_visitor<int>(
        [](VoidType v){return 0;},
        [](BasicType bt){return byte_size(bt);},
        [](const FuncType& ft){throw std::runtime_error("Cannot take alignment of function type");},
        [](const PointerType& pt){return 8;},
        [&](const ArrayType& at){return type::align(at.pointed_type());},
        [&](const StructType& st){return st.align(CType::tags);},
        [&](const UnionType& st){return st.align(CType::tags);}
    ), type);
}
bool is_complete(const CType& type){
    return visit(make_visitor<bool>(
        [](VoidType v){return false;},
        [](BasicType bt){return true;},
        [](const FuncType& ft){throw std::runtime_error("Complete or incomplete does not make sense for function type");},
        [](const PointerType& pt){return true;},
        [](const ArrayType& at){return at.is_complete();},
        [](const StructType& st){return st.is_complete();},
        [](const UnionType& st){return st.is_complete();}
    ), type);
}
bool CType::tag_declared(std::string tag){
    return CType::tags.find(tag) != CType::tags.end();
}
CType CType::get_tag(std::string mangled_tag){
    try{
        return CType::tags.at(mangled_tag);
    }catch(std::runtime_error& e){
        throw std::runtime_error("Unmangled tag "+mangled_tag+" not found in symbol table");
    }
}
void CType::add_tag(std::string tag, type::TagType type){
    std::visit(type::overloaded{
        [&](auto t)->void{
            if(CType::tags.find(tag) != CType::tags.end()){
                auto existing = CType::tags.at(tag);
                if(!type::is_type<decltype(t)>(existing)){
                    throw std::runtime_error("Tag "+tag+" already declared with different type");
                }
                if(t.is_complete()){
                    if(type::get<decltype(t)>(existing).is_complete()){
                        throw std::runtime_error("Tag "+tag+" already defined");
                    }else{
                        for(const auto& member : t.members){
                            if(!is_complete(member)){
                                throw std::runtime_error("Cannot use incomplete type in definition of type");
                            }
                        }
                        if constexpr(std::is_same_v<decltype(t), type::UnionType>){
                            t.compute_largest(CType::tags);
                        }
                        CType::tags[tag] = t;
                    }
                }
            }else{
                if(t.is_complete()){
                    for(const auto& member : t.members){
                        if(!is_complete(member)){
                            throw std::runtime_error("Cannot use incomplete type in definition of type");
                        }
                    }
                    if constexpr(std::is_same_v<decltype(t), type::UnionType>){
                        t.compute_largest(CType::tags);
                    }
                }
                CType::tags.emplace(tag, t);
            }
        }
    }, type);
}
void CType::tag_ir_types(std::ostream& output){
    for(const auto& name_type : CType::tags){
        output<<"%"<<name_type.first<<" = type "<<type::ir_type(name_type.second)<<std::endl;
    }
}

std::map<std::string, type::CType> CType::tags = {};
void CType::reset_tables() noexcept{
    CType::tags = std::map<std::string, type::CType>{};
}
bool is_specifier(const std::string& s){
    return is_type_specifier(s)
        || is_type_qualifier(s)
        || is_storage_specifier(s)
        || is_function_specifier(s);
}
bool is_type_qualifier(const std::string& s){
    return s == "const"
        || s == "restrict"
        || s == "volatile"
        || s == "_Atomic";
}
bool is_storage_specifier(const std::string& s){
    return s == "typedef"
        || s == "extern"
        || s == "static"
        || s == "_Thread_local"
        || s == "auto"
        || s == "register";
}
bool is_function_specifier(const std::string& s){
    return s == "inline"
        || s == "_Noreturn";
}
bool is_type_specifier(const std::string& s){
    return s == "void"
        || s == "char"
        || s == "short"
        || s == "int"
        || s == "long"
        || s == "float"
        || s == "double"
        || s == "signed"
        || s == "unsigned"
        || s == "_Bool"
        || s == "union"
        || s == "struct";
}

} //namespace type
