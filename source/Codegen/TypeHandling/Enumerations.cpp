// EnumerationCodegen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "codegen.h"
#include "llvm_all.h"

using namespace Ast;
using namespace Codegen;

namespace Codegen
{
	Result_t enumerationAccessCodegen(CodegenInstance* cgi, Expr* lhs, Expr* rhs)
	{
		VarRef* enumName = dynamic_cast<VarRef*>(lhs);
		VarRef* caseName = dynamic_cast<VarRef*>(rhs);
		iceAssert(enumName);

		if(!caseName)
			error(rhs, "Expected identifier after enumeration access");

		TypePair_t* tp = cgi->getType(enumName->name);
		iceAssert(tp);
		iceAssert(tp->second.second == TypeKind::Enum);

		Enumeration* enr = dynamic_cast<Enumeration*>(tp->second.first);
		iceAssert(enr);

		Result_t res(0, 0);
		bool found = false;
		for(auto p : enr->cases)
		{
			if(p.first == caseName->name)
			{
				res = p.second->codegen(cgi);
				found = true;
				break;
			}
		}


		if(!found)
			error(rhs, "Enum '%s' has no such case '%s'", enumName->name.c_str(), caseName->name.c_str());

		if(!enr->isStrong)
			return res;


		// strong enum.
		// create a temp alloca, then use GEP to set the value, then return.
		llvm::Value* alloca = cgi->allocateInstanceInBlock(tp->first);
		llvm::Value* gep = cgi->builder.CreateStructGEP(alloca, 0);

		cgi->builder.CreateStore(res.result.first, gep);
		return Result_t(cgi->builder.CreateLoad(alloca), alloca);
	}
}

Result_t Enumeration::codegen(CodegenInstance* cgi, llvm::Value* lhsPtr, llvm::Value* rhs)
{
	return Result_t(0, 0);
}

void Enumeration::createType(CodegenInstance* cgi)
{
	// make sure all types are the same
	// todo: remove this limitation maybe?

	if(cgi->isDuplicateType(this->name))
		GenError::duplicateSymbol(cgi, this, this->name, SymbolType::Type);


	llvm::Type* prev = 0;
	for(auto pair : this->cases)
	{
		if(!prev)
			prev = cgi->getLlvmType(pair.second);


		llvm::Type* t = cgi->getLlvmType(pair.second);
		if(t != prev)
			error(pair.second, "Enumeration values must have the same type, have %s: %s and %s", pair.first.c_str(), cgi->getReadableType(pair.second).c_str(), cgi->getReadableType(prev).c_str());

		prev = t;
	}

	llvm::StructType* wrapper = llvm::StructType::create(this->name, prev, NULL);

	// now that they're all the same type:
	cgi->addNewType(wrapper, this, TypeKind::Enum);
	this->didCreateType = true;
}
