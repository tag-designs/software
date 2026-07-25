
#ifndef POWER_MODES_H
#define POWER_MODES_H

/* This guard forces the assembler to ignore the C functions below */
#if !defined(__ASSEMBLY__) && !defined(__ASSEMBLER__)
#include "cmsis_compiler.h" 
#include "core_types.h"


static inline void idle_enter(void){}; 
static inline void idle_loop(void){ 
 
  __DSB();
  __WFI();   
                                                                                 \
}

static inline void idle_leave(void){}; 

#endif
#endif /* POWER_MODES_H */