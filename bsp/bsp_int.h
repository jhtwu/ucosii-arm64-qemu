#ifndef BSP_INT_H
#define BSP_INT_H

#include <stdint.h>

/*
 * BSP interrupt function pointer type
 */
typedef void (*BSP_INT_FNCT_PTR)(uint32_t int_id);

/*
 * Function prototypes
 */
void BSP_IntVectSet(uint32_t int_id, uint32_t int_prio, uint32_t int_target, BSP_INT_FNCT_PTR int_fnct);
void BSP_IntSrcEn(uint32_t int_id);
void BSP_IntSrcDis(uint32_t int_id);
void BSP_IntHandler(uint32_t int_id);

#endif /* BSP_INT_H */