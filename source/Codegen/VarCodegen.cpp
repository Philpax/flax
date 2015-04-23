// VarCodegen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.


#include "../include/ast.h"
#include "../include/codegen.h"
#include "../include/llvm_all.h"

using namespace Ast;
using namespace Codegen;


Result_t VarRef::codegen(CodegenInstance* cgi, llvm::Value* lhsPtr, llvm::Value* rhs)
{
	llvm::Value* val = cgi->getSymInst(this, this->name);
	if(!val)
		GenError::unknownSymbol(cgi, this, this->name, SymbolType::Variable);

	return Result_t(cgi->mainBuilder.CreateLoad(val, this->name), val);
}









llvm::Value* VarDecl::doInitialValue(Codegen::CodegenInstance* cgi, TypePair_t* cmplxtype, llvm::Value* val, llvm::Value* valptr,
	llvm::Value* storage, bool shouldAddToSymtab)
{

	llvm::Value* ai = storage;
	bool didAddToSymtab = false;

	if(this->initVal && !cmplxtype && this->type.strType != "Inferred" && !cgi->isAnyType(val->getType()))
	{
		// ...
	}
	else if(!this->initVal && (cgi->isBuiltinType(this) || cgi->isArrayType(this) || cgi->isPtr(this)))
	{
		val = cgi->getDefaultValue(this);
		iceAssert(val);
	}
	else
	{
		if(this->inferredLType)
		{
			cmplxtype = cgi->getType(this->inferredLType);
		}
		else
		{
			if(this->type.strType.find("::") != std::string::npos)
				cmplxtype = cgi->getType(cgi->mangleRawNamespace(this->type.strType));

			else
				cmplxtype = cgi->getType(this->type.strType);
		}

		if(!ai)
		{
			iceAssert(cmplxtype);
			iceAssert((ai = cgi->allocateInstanceInBlock(cmplxtype->first)));
		}

		if(cmplxtype)
		{
			// automatically call the init() function
			if(!this->disableAutoInit && !this->initVal)
			{
				llvm::Value* unwrappedAi = cgi->lastMinuteUnwrapType(this, ai);
				if(unwrappedAi != ai)
				{
					cmplxtype = cgi->getType(unwrappedAi->getType()->getPointerElementType());
					iceAssert(cmplxtype);
				}

				if(!cgi->isEnum(ai->getType()->getPointerElementType()))
				{
					std::vector<llvm::Value*> args { unwrappedAi };

					llvm::Function* initfunc = cgi->getStructInitialiser(this, cmplxtype, args);
					val = cgi->mainBuilder.CreateCall(initfunc, args);
				}
			}
		}

		if(shouldAddToSymtab)
		{
			cgi->addSymbol(this->name, ai, this);
			didAddToSymtab = true;
		}


		if(this->initVal && (!cmplxtype || ((StructBase*) cmplxtype->second.first)->name == "Any" || cgi->isAnyType(val->getType())))
		{
			// this only works if we don't call a constructor

			// todo: make this work better, maybe as a parameter to doBinOpAssign
			// it currently doesn't know (and maybe shouldn't) if we're doing first assign or not
			// if we're an immutable var (ie. val or let), we first set immutable to false so we
			// can store the value, then set it back to whatever it was.

			bool wasImmut = this->immutable;
			this->immutable = false;
			auto res = cgi->doBinOpAssign(this, new VarRef(this->posinfo, this->name), this->initVal,
				ArithmeticOp::Assign, cgi->mainBuilder.CreateLoad(ai), ai, val, valptr);

			this->immutable = wasImmut;
			return res.result.first;
		}
		else if(!cmplxtype && !this->initVal)
		{
			if(!val)
				val = cgi->getDefaultValue(this);

			cgi->mainBuilder.CreateStore(val, ai);
			return val;
		}
		else if(cmplxtype && this->initVal)
		{
			if(ai->getType()->getPointerElementType() != val->getType())
				ai = cgi->lastMinuteUnwrapType(this, ai);


			if(ai->getType()->getPointerElementType() != val->getType())
				GenError::invalidAssignment(cgi, this, ai->getType()->getPointerElementType(), val->getType());

			cgi->mainBuilder.CreateStore(val, ai);
			return val;
		}
		else
		{
			if(valptr)
				val = cgi->mainBuilder.CreateLoad(valptr);

			else
				return val;
		}
	}

	if(!ai)
	{
		error(cgi, this, "ai is null");
	}

	if(!didAddToSymtab && shouldAddToSymtab)
		cgi->addSymbol(this->name, ai, this);

	if(val->getType() != ai->getType()->getPointerElementType())
	{
		Number* n = 0;
		if(val->getType()->isIntegerTy() && (n = dynamic_cast<Number*>(this->initVal)) && n->ival == 0)
		{
			val = llvm::Constant::getNullValue(ai->getType()->getPointerElementType());
		}
		else if(val->getType()->isIntegerTy() && ai->getType()->getPointerElementType()->isIntegerTy())
		{
			// Number* n = 0;
			// printf("assign num to var %s (%s)\n", this->name.c_str(), typeid(*this->initVal).name());
			// if((n = dynamic_cast<Number*>(this->initVal)))
			// {
			// 	uint64_t max = pow(2, val->getType()->getIntegerBitWidth());
			// 	if(max == 0) max = UINT64_MAX;

			// 	printf("assign %lld to var: max: %lld\n", n->ival, max);
			// if((uint64_t) n->ival < max)


			val = cgi->mainBuilder.CreateIntCast(val, ai->getType()->getPointerElementType(), false);


			// }
		}
		else
		{
			GenError::invalidAssignment(cgi, this, ai->getType()->getPointerElementType(), val->getType());
		}
	}

	cgi->mainBuilder.CreateStore(val, ai);
	return val;
}

Result_t VarDecl::codegen(CodegenInstance* cgi, llvm::Value* lhsPtr, llvm::Value* _rhs)
{
	if(cgi->isDuplicateSymbol(this->name))
		GenError::duplicateSymbol(cgi, this, this->name, SymbolType::Variable);

	llvm::Value* val = nullptr;
	llvm::Value* valptr = nullptr;

	llvm::Value* ai = nullptr;

	if(this->type.strType == "Inferred")
	{
		if(!this->initVal)
		{
			error(cgi, this, "Type inference requires an initial assignment to infer type");
		}

		ValPtr_t r;
		if(!this->isGlobal)
		{
			llvm::Type* vartype = cgi->getLlvmType(this->initVal);
			if(vartype == nullptr || vartype->isVoidTy())
				GenError::nullValue(cgi, this->initVal);



			if(cgi->isAnyType(vartype))
			{
				#if 0
				// printf("aitype for %s: %s\n", this->name.c_str(), cgi->getReadableType(vartype).c_str());

				// don't codegen with the allocainst, since we don't fucking have it
				r = this->initVal->codegen(cgi).result;
				iceAssert(r.first && cgi->isAnyType(r.first->getType()));
				iceAssert(r.second);

				llvm::Value* typegep = cgi->mainBuilder.CreateStructGEP(r.second, 0);
				llvm::Value* typ = cgi->mainBuilder.CreateLoad(typegep);


				size_t index = cint->getZExtValue();
				vartype = TypeInfo::getTypeForIndex(cgi, index);
				#endif


				// todo: fix this shit
				warn(cgi, this, "Assigning a value of type 'Any' using type inference will not unwrap the value");
			}

			#if 0
			else
			#endif
			{
				ai = cgi->allocateInstanceInBlock(vartype, this->name);
				r = this->initVal->codegen(cgi, ai).result;
			}
		}
		else
		{
			r = this->initVal->codegen(cgi, 0).result;
		}


		val = r.first;
		valptr = r.second;

		this->inferredLType = cgi->getLlvmType(this->initVal);

		if(cgi->isBuiltinType(this->initVal) && !this->inferredLType->isStructTy())
			this->type = cgi->getReadableType(this->initVal);
	}
	else if(this->initVal)
	{
		if(!this->isGlobal)
		{
			ai = cgi->allocateInstanceInBlock(this);
		}

		auto r = this->initVal->codegen(cgi, ai).result;

		val = r.first;
		valptr = r.second;

		this->inferredLType = cgi->getLlvmType(this);
	}
	else
	{
		this->inferredLType = cgi->getLlvmType(this);
		if(!this->isGlobal)
		{
			ai = cgi->allocateInstanceInBlock(this);
			iceAssert(ai->getType()->getPointerElementType() == this->inferredLType);
		}
	}


	// TODO: call global constructors
	if(this->isGlobal)
	{
		ai = new llvm::GlobalVariable(*cgi->mainModule, this->inferredLType, this->immutable,
			this->attribs & Attr_VisPublic ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage, llvm::Constant::getNullValue(this->inferredLType), this->name);

		if(this->initVal)
		{
			iceAssert(val);

			if(llvm::cast<llvm::Constant>(val))
			{
				cgi->autoCastType(ai->getType()->getPointerElementType(), val, valptr);
				llvm::cast<llvm::GlobalVariable>(ai)->setInitializer(llvm::cast<llvm::Constant>(val));
			}
			else
			{
				warn(this, "Global variables currently only support constant initialisers");
			}
		}


		cgi->addSymbol(this->name, ai, this);
	}
	else
	{
		TypePair_t* cmplxtype = 0;
		if(this->type.strType != "Inferred")
		{
			cmplxtype = cgi->getType(this->type.strType);
			if(!cmplxtype) cmplxtype = cgi->getType(cgi->mangleRawNamespace(this->type.strType));
		}

		this->doInitialValue(cgi, cmplxtype, val, valptr, ai, true);
	}
	return Result_t(val, ai);
}














