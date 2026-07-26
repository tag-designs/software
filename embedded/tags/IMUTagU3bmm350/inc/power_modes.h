
#ifndef POWER_MODES_H
#define POWER_MODES_H

/* This guard forces the assembler to ignore the C functions below */
#if !defined(__ASSEMBLY__) && !defined(__ASSEMBLER__)


void idle_enter(void);
void idle_loop(void);
void idle_leave(void);




#endif
#endif /* POWER_MODES_H */