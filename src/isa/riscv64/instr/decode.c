/***************************************************************************************
* Copyright (c) 2014-2021 Zihao Yu, Nanjing University
* Copyright (c) 2020-2022 Institute of Computing Technology, Chinese Academy of Sciences
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include "../local-include/rtl.h"
#include "../local-include/trigger.h"
#include "../local-include/intr.h"
#include <cpu/cpu.h>
#include <cpu/ifetch.h>
#include <cpu/decode.h>
#include <isa-all-instr.h>


def_all_THelper();

__attribute__((always_inline))
static inline uint32_t get_instr(Decode *s) {
  return s->isa.instr.val;
}

// decode operand helper
#define def_DopHelper(name) \
  void concat(decode_op_, name) (Decode *s, Operand *op, word_t val, bool flag)

#include "rvi/decode.h"
#include "rvf/decode.h"
#include "rvm/decode.h"
#include "rva/decode.h"
#include "rvc/decode.h"
#include "rvd/decode.h"
#include "priv/decode.h"
#ifdef CONFIG_RVV
  #include "rvv/decode.h"
#endif // CONFIG_RVV

def_THelper(main) {
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 00000 ??", I     , load);
#ifndef CONFIG_FPU_NONE
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 00001 ??", fload , fload);
#endif // CONFIG_FPU_NONE
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 00011 ??", I     , mem_fence);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 00100 ??", I     , op_imm);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 00101 ??", auipc , auipc);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 00110 ??", I     , op_imm32);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 01000 ??", S     , store);
#ifndef CONFIG_FPU_NONE
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 01001 ??", fstore, fstore);
#endif // CONFIG_FPU_NONE
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 01011 ??", R     , atomic);
  def_INSTR_IDTAB("0000001 ????? ????? ??? ????? 01100 ??", R     , rvm);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 01100 ??", R     , op);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 01101 ??", U     , lui);
  def_INSTR_IDTAB("0000001 ????? ????? ??? ????? 01110 ??", R     , rvm32);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 01110 ??", R     , op32);
#ifndef CONFIG_FPU_NONE
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 10000 ??", R4    , fmadd_dispatch);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 10001 ??", R4    , fmadd_dispatch);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 10010 ??", R4    , fmadd_dispatch);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 10011 ??", R4    , fmadd_dispatch);
  def_INSTR_TAB  ("??????? ????? ????? ??? ????? 10100 ??",         op_fp);
#endif // CONFIG_FPU_NONE
#ifdef CONFIG_RVV
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 10101 ??", OP_V  , OP_V);
#endif // CONFIG_RVV
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 11000 ??", B     , branch);
  def_INSTR_IDTAB("??????? ????? ????? 000 ????? 11001 ??", I     , jalr_dispatch);
  def_INSTR_TAB  ("??????? ????? ????? ??? ????? 11010 ??",         nemu_trap);
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 11011 ??", J     , jal_dispatch);
#ifdef CONFIG_RVH
  def_INSTR_TAB  ("??????? ????? ????? ??? ????? 11100 ??",         system);
#else
  def_INSTR_IDTAB("??????? ????? ????? ??? ????? 11100 ??", csr   , system);
#endif
  return table_inv(s);
};

int isa_fetch_decode(Decode *s) {
  int idx = EXEC_ID_inv;

#ifdef CONFIG_RVSDTRIG
  trig_action_t action = TRIG_ACTION_NONE;
  if (cpu.TM->check_timings.bf) {
    action = tm_check_hit(cpu.TM, TRIG_OP_EXECUTE, s->snpc, TRIGGER_NO_VALUE);
  }
  trigger_handler(action);
#endif

  s->isa.instr.val = instr_fetch(&s->snpc, 2);
  if (s->isa.instr.r.opcode1_0 != 0x3) {
    // this is an RVC instruction
    idx = table_rvc(s);
  } else {
    // this is a 4-byte instruction, should fetch the MSB part
    // NOTE: The fetch here may cause IPF.
    // If it is the case, we should have mepc = xxxffe and mtval = yyy000.
    // Refer to `mtval` in the privileged manual for more details.
    uint32_t hi = instr_fetch(&s->snpc, 2);
    s->isa.instr.val |= (hi << 16);
    idx = table_main(s);
  }

#ifdef CONFIG_RVSDTRIG
  if (cpu.TM->check_timings.af) {
    action = tm_check_hit(cpu.TM, TRIG_OP_EXECUTE | TRIG_OP_TIMING, s->snpc, s->isa.instr.val);
  }
  trigger_handler(action);
#endif

  s->type = INSTR_TYPE_N;
  switch (idx) {
    case EXEC_ID_c_j: case EXEC_ID_p_jal: case EXEC_ID_jal:
      s->jnpc = id_src1->imm; s->type = INSTR_TYPE_J; break;

    case EXEC_ID_beq: case EXEC_ID_bne: case EXEC_ID_blt: case EXEC_ID_bge:
    case EXEC_ID_bltu: case EXEC_ID_bgeu:
    case EXEC_ID_c_beqz: case EXEC_ID_c_bnez:
    case EXEC_ID_p_bltz: case EXEC_ID_p_bgez: case EXEC_ID_p_blez: case EXEC_ID_p_bgtz:
      s->jnpc = id_dest->imm; s->type = INSTR_TYPE_B; break;

    case EXEC_ID_p_ret: case EXEC_ID_c_jr: case EXEC_ID_c_jalr: case EXEC_ID_jalr:
    IFDEF(CONFIG_DEBUG, case EXEC_ID_mret: case EXEC_ID_sret: case EXEC_ID_ecall:)
      s->type = INSTR_TYPE_I; break;

#ifndef CONFIG_DEBUG
#ifdef CONFIG_RVH
    case EXEC_ID_priv:
#else
    case EXEC_ID_system:
#endif
      if (s->isa.instr.i.funct3 == 0) {
        switch (s->isa.instr.csr.csr) {
          case 0:     // ecall
          case 0x102: // sret
          case 0x302: // mret
            s->type = INSTR_TYPE_I;
        }
      }
      break;
#endif
  }

  return idx;
}
