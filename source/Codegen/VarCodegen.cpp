// VarCodegen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.


#include "../include/ast.h"
#include "../include/codegen.h"
#include "../include/llvm_all.h"

using namespace Ast;
using namespace Codegen;


ValPtr_p VarRef::codeGen()
{
	llvm::Value* val = getSymInst(this->name);
	if(!val)
		error(this, "Unknown variable name '%s'", this->name.c_str());

	return ValPtr_p(mainBuilder.CreateLoad(val, this->name), val);
}

ValPtr_p VarDecl::codeGen()
{
	if(isDuplicateSymbol(this->name))
		error(this, "Redefining duplicate symbol '%s'", this->name.c_str());

	llvm::Function* func = mainBuilder.GetInsertBlock()->getParent();
	llvm::Value* val = nullptr;

	llvm::AllocaInst* ai = allocateInstanceInBlock(func, this);
	getSymTab()[this->name] = std::pair<llvm::AllocaInst*, VarDecl*>(ai, this);


	TypePair_t* cmplxtype = getType(this->type);

	if(this->initVal && !cmplxtype)
	{
		this->initVal = autoCastType(this, this->initVal);
		val = this->initVal->codeGen().first;
	}
	else if(isBuiltinType(this) || isArrayType(this))
	{
		val = getDefaultValue(this);
	}
	else
	{
		// get our type
		if(!cmplxtype)
			error(this, "Invalid type");

		TypePair_t* pair = cmplxtype;
		if(pair->first->isStructTy())
		{
			Struct* str = nullptr;
			assert((str = dynamic_cast<Struct*>(pair->second.first)));

			assert(pair->second.second == ExprType::Struct);
			assert(pair->second.first);

			val = mainBuilder.CreateCall(str->initFunc, ai);

			if(this->initVal)
			{
				llvm::Value* ival = this->initVal->codeGen().first;

				if(ival->getType() == ai->getType()->getPointerElementType())
					return ValPtr_p(mainBuilder.CreateStore(ival, ai), ai);

				else
					return callOperatorOnStruct(pair, ai, ArithmeticOp::Assign, ival);
			}
			else
			{
				return ValPtr_p(val, ai);
			}
		}
		else
		{
			error(this, "Unknown type encountered");
		}
	}

	mainBuilder.CreateStore(val, ai);
	return ValPtr_p(val, ai);
}














