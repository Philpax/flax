// ExprCodeGen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "../include/ast.h"
#include "../include/codegen.h"
#include "../include/llvm_all.h"

using namespace Ast;
using namespace Codegen;


ValPtr_p Return::codeGen()
{
	auto ret = this->val->codeGen();
	return ValPtr_p(mainBuilder.CreateRet(ret.first), ret.second);
}

ValPtr_p UnaryOp::codeGen()
{
	assert(this->expr);
	switch(this->op)
	{
		case ArithmeticOp::LogicalNot:
			return ValPtr_p(mainBuilder.CreateNot(this->expr->codeGen().first), 0);

		case ArithmeticOp::Minus:
			return ValPtr_p(mainBuilder.CreateNeg(this->expr->codeGen().first), 0);

		case ArithmeticOp::Plus:
			return this->expr->codeGen();

		case ArithmeticOp::Deref:
		{
			ValPtr_p vp = this->expr->codeGen();
			return ValPtr_p(mainBuilder.CreateLoad(vp.first), vp.first);
		}

		case ArithmeticOp::AddrOf:
		{
			return ValPtr_p(this->expr->codeGen().second, 0);
		}

		default:
			error("(%s:%s:%d) -> Internal check failed: invalid unary operator", __FILE__, __PRETTY_FUNCTION__, __LINE__);
			return ValPtr_p(0, 0);
	}
}



ValPtr_p BinOp::codeGen()
{
	assert(this->left && this->right);
	this->right = autoCastType(this->left, this->right);

	ValPtr_p valptr = this->left->codeGen();

	llvm::Value* lhs;
	llvm::Value* rhs;

	if(this->op == ArithmeticOp::Assign)
	{
		lhs = valptr.first;
		rhs = this->right->codeGen().first;

		VarRef* v = nullptr;
		UnaryOp* uo = nullptr;
		ArrayIndex* ai = nullptr;
		if((v = dynamic_cast<VarRef*>(this->left)))
		{
			if(!rhs)
				error("(%s:%s:%d) -> Internal check failed: invalid RHS for assignment", __FILE__, __PRETTY_FUNCTION__, __LINE__);

			llvm::Value* var = getSymTab()[v->name].first;
			if(!var)
				error(this, "Unknown identifier (var) '%s'", v->name.c_str());

			if(lhs->getType() != rhs->getType())
			{
				if(lhs->getType()->isStructTy())
				{
					TypePair_t* tp = getType(lhs->getType()->getStructName());
					if(!tp)
						error(this, "Invalid type");

					return callOperatorOnStruct(tp, valptr.second, ArithmeticOp::Assign, rhs);
				}
				else
				{
					error(this, "Cannot assign different types '%s' and '%s'", getReadableType(lhs->getType()).c_str(), getReadableType(rhs->getType()).c_str());
				}
			}

			mainBuilder.CreateStore(rhs, var);
			return ValPtr_p(rhs, var);
		}
		else if((dynamic_cast<MemberAccess*>(this->left))
			|| ((uo = dynamic_cast<UnaryOp*>(this->left)) && uo->op == ArithmeticOp::Deref)
			|| ((ai = dynamic_cast<ArrayIndex*>(this->left))))
		{
			// we know that the ptr lives in the second element
			// so, use it

			llvm::Value* ptr = valptr.second;
			assert(ptr);
			assert(rhs);

			// make sure the left side is a pointer
			if(!ptr->getType()->isPointerTy())
				error(this, "Expression (type '%s' = '%s') is not assignable.", getReadableType(ptr->getType()).c_str(), getReadableType(rhs->getType()).c_str());

			// redo the number casting
			if(rhs->getType()->isIntegerTy() && lhs->getType()->isIntegerTy())
				rhs = mainBuilder.CreateIntCast(rhs, ptr->getType()->getPointerElementType(), false);

			else if(rhs->getType()->isIntegerTy() && lhs->getType()->isPointerTy())
				rhs = mainBuilder.CreateIntToPtr(rhs, lhs->getType());

			// assign it
			mainBuilder.CreateStore(rhs, ptr);
			return ValPtr_p(rhs, ptr);
		}
		else
		{
			error("Left-hand side of assignment must be assignable");
		}
	}
	else if(this->op == ArithmeticOp::Cast)
	{
		lhs = valptr.first;

		// right hand side probably got interpreted as a varref
		VarRef* vr = nullptr;
		assert(vr = dynamic_cast<VarRef*>(this->right));

		llvm::Type* rtype;
		VarType vt = Parser::determineVarType(vr->name);
		if(vt != VarType::UserDefined)
		{
			rtype = getLlvmTypeOfBuiltin(vt);
		}
		else
		{
			TypePair_t* tp = getType(vr->name);
			if(!tp)
				error(this, "Unknown type '%s'", vr->name.c_str());

			rtype = tp->first;
		}

		// todo: cleanup?
		assert(rtype);
		if(lhs->getType() == rtype)
			return ValPtr_p(lhs, 0);

		if(lhs->getType()->isIntegerTy() && rtype->isIntegerTy())
			return ValPtr_p(mainBuilder.CreateIntCast(lhs, rtype, isSignedType(this->left)), 0);

		else if(lhs->getType()->isFloatTy() && rtype->isFloatTy())
			return ValPtr_p(mainBuilder.CreateFPCast(lhs, rtype), 0);

		else if(lhs->getType()->isPointerTy() && rtype->isPointerTy())
			return ValPtr_p(mainBuilder.CreatePointerCast(lhs, rtype), 0);

		else if(lhs->getType()->isPointerTy() && rtype->isIntegerTy())
			return ValPtr_p(mainBuilder.CreatePtrToInt(lhs, rtype), 0);

		else if(lhs->getType()->isIntegerTy() && rtype->isPointerTy())
			return ValPtr_p(mainBuilder.CreateIntToPtr(lhs, rtype), 0);

		else
			return ValPtr_p(mainBuilder.CreateBitCast(lhs, rtype), 0);
	}




	lhs = valptr.first;
	llvm::Value* rhsptr = nullptr;
	auto r = this->right->codeGen();

	rhs = r.first;
	rhsptr = r.second;

	if(lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy())
	{
		switch(this->op)
		{
			case ArithmeticOp::Add:											return ValPtr_p(mainBuilder.CreateAdd(lhs, rhs), 0);
			case ArithmeticOp::Subtract:									return ValPtr_p(mainBuilder.CreateSub(lhs, rhs), 0);
			case ArithmeticOp::Multiply:									return ValPtr_p(mainBuilder.CreateMul(lhs, rhs), 0);
			case ArithmeticOp::ShiftLeft:									return ValPtr_p(mainBuilder.CreateShl(lhs, rhs), 0);
			case ArithmeticOp::Divide:
				if(isSignedType(this->left) || isSignedType(this->right))	return ValPtr_p(mainBuilder.CreateSDiv(lhs, rhs), 0);
				else 														return ValPtr_p(mainBuilder.CreateUDiv(lhs, rhs), 0);
			case ArithmeticOp::Modulo:
				if(isSignedType(this->left) || isSignedType(this->right))	return ValPtr_p(mainBuilder.CreateSRem(lhs, rhs), 0);
				else 														return ValPtr_p(mainBuilder.CreateURem(lhs, rhs), 0);
			case ArithmeticOp::ShiftRight:
				if(isSignedType(this->left))								return ValPtr_p(mainBuilder.CreateAShr(lhs, rhs), 0);
				else 														return ValPtr_p(mainBuilder.CreateLShr(lhs, rhs), 0);

			// comparisons
			case ArithmeticOp::CmpEq:										return ValPtr_p(mainBuilder.CreateICmpEQ(lhs, rhs), 0);
			case ArithmeticOp::CmpNEq:										return ValPtr_p(mainBuilder.CreateICmpNE(lhs, rhs), 0);
			case ArithmeticOp::CmpLT:
				if(isSignedType(this->left) || isSignedType(this->right))	return ValPtr_p(mainBuilder.CreateICmpSLT(lhs, rhs), 0);
				else 														return ValPtr_p(mainBuilder.CreateICmpULT(lhs, rhs), 0);
			case ArithmeticOp::CmpGT:
				if(isSignedType(this->left) || isSignedType(this->right))	return ValPtr_p(mainBuilder.CreateICmpSGT(lhs, rhs), 0);
				else 														return ValPtr_p(mainBuilder.CreateICmpUGT(lhs, rhs), 0);
			case ArithmeticOp::CmpLEq:
				if(isSignedType(this->left) || isSignedType(this->right))	return ValPtr_p(mainBuilder.CreateICmpSLE(lhs, rhs), 0);
				else 														return ValPtr_p(mainBuilder.CreateICmpULE(lhs, rhs), 0);
			case ArithmeticOp::CmpGEq:
				if(isSignedType(this->left) || isSignedType(this->right))	return ValPtr_p(mainBuilder.CreateICmpSGE(lhs, rhs), 0);
				else 														return ValPtr_p(mainBuilder.CreateICmpUGE(lhs, rhs), 0);



			case ArithmeticOp::BitwiseAnd:									return ValPtr_p(mainBuilder.CreateAnd(lhs, rhs), 0);
			case ArithmeticOp::BitwiseOr:									return ValPtr_p(mainBuilder.CreateOr(lhs, rhs), 0);


			case ArithmeticOp::LogicalOr:
			case ArithmeticOp::LogicalAnd:
			{
				int theOp = this->op == ArithmeticOp::LogicalOr ? 0 : 1;
				llvm::Value* trueval = llvm::ConstantInt::get(getContext(), llvm::APInt(1, 1, true));
				llvm::Value* falseval = llvm::ConstantInt::get(getContext(), llvm::APInt(1, 0, true));


				llvm::Function* func = mainBuilder.GetInsertBlock()->getParent();
				llvm::Value* res = mainBuilder.CreateTrunc(lhs, llvm::Type::getInt1Ty(getContext()));
				llvm::Value* ret = nullptr;

				llvm::BasicBlock* entry = mainBuilder.GetInsertBlock();
				llvm::BasicBlock* lb = llvm::BasicBlock::Create(getContext(), "leftbl", func);
				llvm::BasicBlock* rb = llvm::BasicBlock::Create(getContext(), "rightbl", func);
				mainBuilder.CreateCondBr(res, lb, rb);


				mainBuilder.SetInsertPoint(rb);
				// this kinda works recursively
				if(!this->phi)
					this->phi = mainBuilder.CreatePHI(llvm::Type::getInt1Ty(getContext()), 2);


				// if this is a logical-or
				if(theOp == 0)
				{
					// do the true case
					mainBuilder.SetInsertPoint(lb);
					this->phi->addIncoming(trueval, lb);

					// if it succeeded (aka res is true), go to the merge block.
					mainBuilder.CreateBr(rb);



					// do the false case
					mainBuilder.SetInsertPoint(rb);

					// do another compare.
					llvm::Value* rres = mainBuilder.CreateTrunc(rhs, llvm::Type::getInt1Ty(getContext()));
					this->phi->addIncoming(rres, entry);
				}
				else
				{
					// do the true case
					mainBuilder.SetInsertPoint(lb);
					llvm::Value* rres = mainBuilder.CreateTrunc(rhs, llvm::Type::getInt1Ty(getContext()));
					this->phi->addIncoming(rres, lb);

					mainBuilder.CreateBr(rb);


					// do the false case
					mainBuilder.SetInsertPoint(rb);
					phi->addIncoming(falseval, entry);
				}

				return ValPtr_p(this->phi, 0);
			}


			default:
				// should not be reached
				error("what?!");
				return ValPtr_p(0, 0);
		}
	}
	else if(isBuiltinType(this->left) && isBuiltinType(this->right))
	{
		// then they're floats.
		switch(this->op)
		{
			case ArithmeticOp::Add:			return ValPtr_p(mainBuilder.CreateFAdd(lhs, rhs), 0);
			case ArithmeticOp::Subtract:	return ValPtr_p(mainBuilder.CreateFSub(lhs, rhs), 0);
			case ArithmeticOp::Multiply:	return ValPtr_p(mainBuilder.CreateFMul(lhs, rhs), 0);
			case ArithmeticOp::Divide:		return ValPtr_p(mainBuilder.CreateFDiv(lhs, rhs), 0);

			// comparisons
			case ArithmeticOp::CmpEq:		return ValPtr_p(mainBuilder.CreateFCmpOEQ(lhs, rhs), 0);
			case ArithmeticOp::CmpNEq:		return ValPtr_p(mainBuilder.CreateFCmpONE(lhs, rhs), 0);
			case ArithmeticOp::CmpLT:		return ValPtr_p(mainBuilder.CreateFCmpOLT(lhs, rhs), 0);
			case ArithmeticOp::CmpGT:		return ValPtr_p(mainBuilder.CreateFCmpOGT(lhs, rhs), 0);
			case ArithmeticOp::CmpLEq:		return ValPtr_p(mainBuilder.CreateFCmpOLE(lhs, rhs), 0);
			case ArithmeticOp::CmpGEq:		return ValPtr_p(mainBuilder.CreateFCmpOGE(lhs, rhs), 0);

			default:						error("Unsupported operator."); return ValPtr_p(0, 0);
		}
	}
	else if(lhs->getType()->isStructTy())
	{
		TypePair_t* p = getType(lhs->getType()->getStructName());
		if(!p)
			error("Invalid type");

		return callOperatorOnStruct(p, valptr.second, op, rhsptr);
	}
	else
	{
		error("Unsupported operator on type");
		return ValPtr_p(0, 0);
	}
}





