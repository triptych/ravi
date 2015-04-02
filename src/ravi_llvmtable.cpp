/******************************************************************************
* Copyright (C) 2015 Dibyendu Majumdar
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/
#include "ravi_llvmcodegen.h"

namespace ravi {

// R(A+1) := R(B); R(A) := R(B)[RK(C)]
void RaviCodeGenerator::emit_SELF(RaviFunctionDef *def, llvm::Value *L_ci,
                                  llvm::Value *proto, int A, int B, int C) {
  // StkId rb = RB(i);
  // setobjs2s(L, ra + 1, rb);
  // Protect(luaV_gettable(L, rb, RKC(i), ra));
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *rb = emit_gep_ra(def, base_ptr, B);
  llvm::Value *ra1 = emit_gep_ra(def, base_ptr, A + 1);
  emit_assign(def, ra1, rb);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);
  def->builder->CreateCall4(def->luaV_gettableF, def->L, rb, rc, ra);
}

// R(A) := length of R(B)
void RaviCodeGenerator::emit_LEN(RaviFunctionDef *def, llvm::Value *L_ci,
                                 llvm::Value *proto, int A, int B) {
  // Protect(luaV_objlen(L, ra, RB(i)));
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rb = emit_gep_ra(def, base_ptr, B);
  def->builder->CreateCall3(def->luaV_objlenF, def->L, ra, rb);
}

// R(A)[RK(B)] := RK(C)
void RaviCodeGenerator::emit_SETTABLE(RaviFunctionDef *def, llvm::Value *L_ci,
                                      llvm::Value *proto, int A, int B, int C) {
  // Protect(luaV_settable(L, ra, RKB(i), RKC(i)));
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rb = emit_gep_rkb(def, base_ptr, B);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);
  def->builder->CreateCall4(def->luaV_settableF, def->L, ra, rb, rc);
}

// R(A) := R(B)[RK(C)]
void RaviCodeGenerator::emit_GETTABLE(RaviFunctionDef *def, llvm::Value *L_ci,
                                      llvm::Value *proto, int A, int B, int C) {
  // Protect(luaV_gettable(L, RB(i), RKC(i), ra));
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rb = emit_gep_ra(def, base_ptr, B);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);
  def->builder->CreateCall4(def->luaV_gettableF, def->L, rb, rc, ra);
}

void RaviCodeGenerator::emit_GETTABLE_AF(RaviFunctionDef *def,
                                         llvm::Value *L_ci, llvm::Value *proto,
                                         int A, int B, int C) {
  //#define raviH_get_float_inline(L, t, key, v) \
  //{ unsigned ukey = (unsigned)((key)-1); \
  //  lua_Number *data = (lua_Number *)t->ravi_array.data; \
  //  if (ukey < t->ravi_array.len) {\
  //    setfltvalue(v, data[ukey]); \
  //      }else \
  //    luaG_runerror(L, "array out of bounds"); \
  //}

  // TValue *rb = RB(i);
  // TValue *rc = RKC(i);
  // lua_Integer idx = ivalue(rc);
  // Table *t = hvalue(rb);
  // raviH_get_float_inline(L, t, idx, ra);

  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rb = emit_gep_ra(def, base_ptr, B);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);
  llvm::Instruction *key = emit_load_reg_i(def, rc);
  llvm::Instruction *t = emit_load_reg_h(def, rb);
  llvm::Instruction *data = emit_load_reg_h_floatarray(def, t);
  llvm::Instruction *len = emit_load_ravi_arraylength(def, t);
  llvm::Value *key_minus_1 =
      def->builder->CreateSub(key, def->types->kluaInteger[1]);
  llvm::Value *ukey =
      def->builder->CreateTrunc(key_minus_1, def->types->C_intT);

  llvm::Value *cmp = def->builder->CreateICmpULT(ukey, len);
  llvm::BasicBlock *then_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.in.range", def->f);
  llvm::BasicBlock *else_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.not.in.range");
  llvm::BasicBlock *end_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.end");
  def->builder->CreateCondBr(cmp, then_block, else_block);
  def->builder->SetInsertPoint(then_block);

  llvm::Value *ptr = def->builder->CreateGEP(data, ukey);
  llvm::Instruction *value = def->builder->CreateLoad(ptr);
  // TODO tbaa

  emit_store_reg_n(def, value, ra);
  emit_store_type(def, ra, LUA_TNUMFLT);
  def->builder->CreateBr(end_block);

  def->f->getBasicBlockList().push_back(else_block);
  def->builder->SetInsertPoint(else_block);

  llvm::Value *errmsg1 =
      def->builder->CreateGlobalString("array out of bounds");
  def->builder->CreateCall2(def->luaG_runerrorF, def->L,
                            emit_gep(def, "out_of_bounds_msg", errmsg1, 0, 0));
  def->builder->CreateBr(end_block);

  def->f->getBasicBlockList().push_back(end_block);
  def->builder->SetInsertPoint(end_block);
}

void RaviCodeGenerator::emit_GETTABLE_AI(RaviFunctionDef *def,
                                         llvm::Value *L_ci, llvm::Value *proto,
                                         int A, int B, int C) {

  //#define raviH_get_int_inline(L, t, key, v) \
  //{ unsigned ukey = (unsigned)((key)-1); \
  //  lua_Integer *data = (lua_Integer *)t->ravi_array.data; \
  //  if (ukey < t->ravi_array.len) {\
  //    setivalue(v, data[ukey]); \
  //      } else \
  //    luaG_runerror(L, "array out of bounds"); \
  //}

  // TValue *rb = RB(i);
  // TValue *rc = RKC(i);
  // lua_Integer idx = ivalue(rc);
  // Table *t = hvalue(rb);
  // raviH_get_int_inline(L, t, idx, ra);

  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rb = emit_gep_ra(def, base_ptr, B);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);
  llvm::Instruction *key = emit_load_reg_i(def, rc);
  llvm::Instruction *t = emit_load_reg_h(def, rb);
  llvm::Instruction *data = emit_load_reg_h_intarray(def, t);
  llvm::Instruction *len = emit_load_ravi_arraylength(def, t);
  llvm::Value *key_minus_1 =
      def->builder->CreateSub(key, def->types->kluaInteger[1]);
  llvm::Value *ukey =
      def->builder->CreateTrunc(key_minus_1, def->types->C_intT);

  llvm::Value *cmp = def->builder->CreateICmpULT(ukey, len);
  llvm::BasicBlock *then_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.in.range", def->f);
  llvm::BasicBlock *else_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.not.in.range");
  llvm::BasicBlock *end_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.end");
  def->builder->CreateCondBr(cmp, then_block, else_block);
  def->builder->SetInsertPoint(then_block);

  llvm::Value *ptr = def->builder->CreateGEP(data, ukey);
  llvm::Instruction *value = def->builder->CreateLoad(ptr);
  // TODO tbaa

  emit_store_reg_i(def, value, ra);
  emit_store_type(def, ra, LUA_TNUMINT);
  def->builder->CreateBr(end_block);

  def->f->getBasicBlockList().push_back(else_block);
  def->builder->SetInsertPoint(else_block);

  llvm::Value *errmsg1 =
      def->builder->CreateGlobalString("array out of bounds");
  def->builder->CreateCall2(def->luaG_runerrorF, def->L,
                            emit_gep(def, "out_of_bounds_msg", errmsg1, 0, 0));
  def->builder->CreateBr(end_block);

  def->f->getBasicBlockList().push_back(end_block);
  def->builder->SetInsertPoint(end_block);
}

void RaviCodeGenerator::emit_SETTABLE_AI(RaviFunctionDef *def,
                                         llvm::Value *L_ci, llvm::Value *proto,
                                         int A, int B, int C) {

  //#define raviH_set_int_inline(L, t, key, value) \
  //{ unsigned ukey = (unsigned)((key)-1); \
  //  lua_Integer *data = (lua_Integer *)t->ravi_array.data; \
  //  if (ukey < t->ravi_array.len) { \
  //    data[ukey] = value; \
  //      } else \
  //    raviH_set_int(L, t, ukey, value); \
  //}

  // Table *t = hvalue(ra);
  // TValue *rb = RKB(i);
  // TValue *rc = RKC(i);
  // lua_Integer idx = ivalue(rb);
  // lua_Integer value = ivalue(rc);
  // raviH_set_int_inline(L, t, idx, value);

  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rb = emit_gep_rkb(def, base_ptr, B);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);
  llvm::Instruction *key = emit_load_reg_i(def, rb);
  llvm::Instruction *value = emit_load_reg_i(def, rc);
  llvm::Instruction *t = emit_load_reg_h(def, ra);
  llvm::Instruction *data = emit_load_reg_h_intarray(def, t);
  llvm::Instruction *len = emit_load_ravi_arraylength(def, t);
  llvm::Value *key_minus_1 =
      def->builder->CreateSub(key, def->types->kluaInteger[1]);
  llvm::Value *ukey =
      def->builder->CreateTrunc(key_minus_1, def->types->C_intT);

  llvm::Value *cmp = def->builder->CreateICmpULT(ukey, len);
  llvm::BasicBlock *then_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.in.range", def->f);
  llvm::BasicBlock *else_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.not.in.range");
  llvm::BasicBlock *end_block =
      llvm::BasicBlock::Create(def->jitState->context(), "if.end");
  def->builder->CreateCondBr(cmp, then_block, else_block);
  def->builder->SetInsertPoint(then_block);

  llvm::Value *ptr = def->builder->CreateGEP(data, ukey);

  llvm::Instruction *ins = def->builder->CreateStore(value, ptr);
  // TODO tbaa
  def->builder->CreateBr(end_block);

  def->f->getBasicBlockList().push_back(else_block);
  def->builder->SetInsertPoint(else_block);

  llvm::Value *uukey = def->builder->CreateZExt(ukey, def->types->lua_UnsignedT);
  def->builder->CreateCall4(def->raviH_set_intF, def->L, t, uukey, value);
  def->builder->CreateBr(end_block);

  def->f->getBasicBlockList().push_back(end_block);
  def->builder->SetInsertPoint(end_block);
}

// R(A) := UpValue[B]
void RaviCodeGenerator::emit_GETUPVAL(RaviFunctionDef *def, llvm::Value *L_ci,
                                      llvm::Value *proto, int A, int B) {
  // int b = GETARG_B(i);
  // setobj2s(L, ra, cl->upvals[b]->v);
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *upval_ptr = emit_gep_upvals(def, def->p_LClosure, B);
  llvm::Instruction *upval = emit_load_pupval(def, upval_ptr);
  llvm::Value *v = emit_load_upval_v(def, upval);
  emit_assign(def, ra, v);
}

// UpValue[B] := R(A)
void RaviCodeGenerator::emit_SETUPVAL(RaviFunctionDef *def, llvm::Value *L_ci,
                                      llvm::Value *proto, int A, int B) {

  // UpVal *uv = cl->upvals[GETARG_B(i)];
  // setobj(L, uv->v, ra);
  // luaC_upvalbarrier(L, uv);

  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *upval_ptr = emit_gep_upvals(def, def->p_LClosure, B);
  llvm::Instruction *upval = emit_load_pupval(def, upval_ptr);
  llvm::Value *v = emit_load_upval_v(def, upval);
  emit_assign(def, v, ra);

  llvm::Value *type = emit_load_type(def, v);
  llvm::Value *is_collectible =
      def->builder->CreateAnd(type, def->types->kInt[BIT_ISCOLLECTABLE]);

  llvm::Value *value = emit_gep_upval_value(def, upval);
  llvm::Value *cmp = def->builder->CreateICmpNE(v, value, "v.ne.value");
  llvm::Value *tobool = def->builder->CreateICmpEQ(
      is_collectible, def->types->kInt[0], "not.collectible");
  llvm::Value *orcond =
      def->builder->CreateOr(cmp, tobool, "v.ne.value.or.not.collectible");

  llvm::BasicBlock *then =
      llvm::BasicBlock::Create(def->jitState->context(), "if.then", def->f);
  llvm::BasicBlock *end =
      llvm::BasicBlock::Create(def->jitState->context(), "if.end");

  def->builder->CreateCondBr(orcond, end, then);
  def->builder->SetInsertPoint(then);

  def->builder->CreateCall2(def->luaC_upvalbarrierF, def->L, upval);
  def->builder->CreateBr(end);

  def->f->getBasicBlockList().push_back(end);
  def->builder->SetInsertPoint(end);
}

// R(A) := UpValue[B][RK(C)]
void RaviCodeGenerator::emit_GETTABUP(RaviFunctionDef *def, llvm::Value *L_ci,
                                      llvm::Value *proto, int A, int B, int C) {
  // int b = GETARG_B(i);
  // Protect(luaV_gettable(L, cl->upvals[b]->v, RKC(i), ra));
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);

  llvm::Value *upval_ptr = emit_gep_upvals(def, def->p_LClosure, B);
  llvm::Instruction *upval = emit_load_pupval(def, upval_ptr);
  llvm::Value *v = emit_load_upval_v(def, upval);
  def->builder->CreateCall4(def->luaV_gettableF, def->L, v, rc, ra);
}

// UpValue[A][RK(B)] := RK(C)
void RaviCodeGenerator::emit_SETTABUP(RaviFunctionDef *def, llvm::Value *L_ci,
                                      llvm::Value *proto, int A, int B, int C) {

  // int a = GETARG_A(i);
  // Protect(luaV_settable(L, cl->upvals[a]->v, RKB(i), RKC(i)));

  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *rb = emit_gep_rkb(def, base_ptr, B);
  llvm::Value *rc = emit_gep_rkb(def, base_ptr, C);

  llvm::Value *upval_ptr = emit_gep_upvals(def, def->p_LClosure, A);
  llvm::Instruction *upval = emit_load_pupval(def, upval_ptr);
  llvm::Value *v = emit_load_upval_v(def, upval);
  def->builder->CreateCall4(def->luaV_settableF, def->L, v, rb, rc);
}

void RaviCodeGenerator::emit_NEWARRAYINT(RaviFunctionDef *def,
                                         llvm::Value *L_ci, llvm::Value *proto,
                                         int A) {
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  def->builder->CreateCall3(def->raviV_op_newarrayintF, def->L, def->ci_val, ra);
}

void RaviCodeGenerator::emit_NEWARRAYFLOAT(RaviFunctionDef *def,
                                           llvm::Value *L_ci,
                                           llvm::Value *proto, int A) {
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  def->builder->CreateCall3(def->raviV_op_newarrayfloatF, def->L, def->ci_val, ra);
}

void RaviCodeGenerator::emit_NEWTABLE(RaviFunctionDef *def, llvm::Value *L_ci,
                                      llvm::Value *proto, int A, int B, int C) {
  //  case OP_NEWTABLE: {
  //    int b = GETARG_B(i);
  //    int c = GETARG_C(i);
  //    Table *t = luaH_new(L);
  //    sethvalue(L, ra, t);
  //    if (b != 0 || c != 0)
  //      luaH_resize(L, t, luaO_fb2int(b), luaO_fb2int(c));
  //    checkGC(L, ra + 1);
  //  } break;

  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  def->builder->CreateCall5(def->raviV_op_newtableF, def->L, def->ci_val, ra,
                            def->types->kInt[B], def->types->kInt[C]);
}

void RaviCodeGenerator::emit_SETLIST(RaviFunctionDef *def, llvm::Value *L_ci,
                                     llvm::Value *proto, int A, int B, int C) {
  llvm::Instruction *base_ptr = emit_load_base(def);
  llvm::Value *ra = emit_gep_ra(def, base_ptr, A);
  def->builder->CreateCall5(def->raviV_op_setlistF, def->L, def->ci_val, ra,
                            def->types->kInt[B], def->types->kInt[C]);
}
}