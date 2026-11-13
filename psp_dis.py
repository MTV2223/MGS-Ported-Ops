"""Tiny MIPS disassembler for the common instructions (enough to read startup code)."""
R = ['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7','s0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
def s16(x): return x - 0x10000 if x & 0x8000 else x
def dis(w, pc):
    op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sa=(w>>6)&31; fn=w&63; im=w&0xFFFF
    if w == 0: return "nop"
    if op == 0:
        n = {0x21:'addu',0x23:'subu',0x24:'and',0x25:'or',0x26:'xor',0x27:'nor',0x2a:'slt',0x2b:'sltu',0x0a:'movz',0x0b:'movn',0x2c:'max',0x2d:'min'}.get(fn)
        if fn == 0x08: return f"jr {R[rs]}"
        if fn == 0x09: return f"jalr {R[rd]}, {R[rs]}"
        if fn == 0x00: return f"sll {R[rd]}, {R[rt]}, {sa}"
        if fn == 0x02: return f"srl {R[rd]}, {R[rt]}, {sa}"
        if fn == 0x03: return f"sra {R[rd]}, {R[rt]}, {sa}"
        if fn in (0x18,0x19,0x1a,0x1b): return f"{['mult','multu','div','divu'][fn-0x18]} {R[rs]}, {R[rt]}"
        if fn in (0x10,0x12): return f"{'mfhi' if fn==0x10 else 'mflo'} {R[rd]}"
        if n: return f"{n} {R[rd]}, {R[rs]}, {R[rt]}"
    if op == 2: return f"j {((pc+4)&0xF0000000|(w&0x3FFFFFF)<<2):#x}"
    if op == 3: return f"jal {((pc+4)&0xF0000000|(w&0x3FFFFFF)<<2):#x}"
    if op in (4,5,6,7,0x14,0x15): return f"{ {4:'beq',5:'bne',6:'blez',7:'bgtz',0x14:'beql',0x15:'bnel'}[op] } {R[rs]}, {R[rt]}, {(pc+4+(s16(im)<<2)):#x}"
    if op == 1: return f"{ {0:'bltz',1:'bgez',2:'bltzl',3:'bgezl',0x10:'bltzal',0x11:'bgezal'}.get(rt,'regimm') } {R[rs]}, {(pc+4+(s16(im)<<2)):#x}"
    imm = {8:'addi',9:'addiu',0xa:'slti',0xb:'sltiu',0xc:'andi',0xd:'ori',0xe:'xori'}.get(op)
    if imm: return f"{imm} {R[rt]}, {R[rs]}, {s16(im) if op in (8,9,0xa,0xb) else hex(im)}"
    if op == 0xF: return f"lui {R[rt]}, {im:#x}"
    mem = {0x20:'lb',0x21:'lh',0x22:'lwl',0x23:'lw',0x24:'lbu',0x25:'lhu',0x26:'lwr',0x28:'sb',0x29:'sh',0x2a:'swl',0x2b:'sw',0x2e:'swr',0x31:'lwc1',0x39:'swc1'}.get(op)
    if mem: return f"{mem} {R[rt]}, {s16(im)}({R[rs]})"
    return f".word {w:#010x}  (op {op:#x})"
