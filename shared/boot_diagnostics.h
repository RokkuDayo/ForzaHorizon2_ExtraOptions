#ifndef EO_BOOT_DIAGNOSTICS_H
#define EO_BOOT_DIAGNOSTICS_H
#include "shared/extra_options.h"
#ifdef EO_BOOT_DIAGNOSTICS
uint64_t boot_diag_now(void);
void boot_diag_start(const wchar_t *directory,unsigned title,uint64_t origin);
void boot_diag_end(const char *stage,const char *detail,uint64_t begin);
void boot_diag_summary(const char *milestone);
void boot_diag_end_w(const char *stage,const wchar_t *detail,uint64_t begin);
#else
#define boot_diag_now() ((uint64_t)0)
#define boot_diag_start(directory,title,origin) ((void)0)
#define boot_diag_end(stage,detail,begin) ((void)(begin))
#define boot_diag_summary(milestone) ((void)0)
#define boot_diag_end_w(stage,detail,begin) ((void)(begin))
#endif
#endif
