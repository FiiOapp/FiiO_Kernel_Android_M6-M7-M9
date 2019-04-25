#ifndef __ACPM_HELPERS_H__
#define __ACPM_HELPERS_H__

#define SET_FLAG	6

#define MASTER_ID_CP		0
#define MASTER_ID_GNSS		1
#define MASTER_ID_WLBT		2
#define FLAG_LOCK		1
#define FLAG_UNLOCK		0

#ifdef CONFIG_ACPM_DVFS
extern int exynos_acpm_set_flag(u32 master_id, bool is_lock);
#else
static inline int exynos_acpm_set_flag(u32 master_id, bool is_lock)
{
	return 0;
}
#endif

#endif
