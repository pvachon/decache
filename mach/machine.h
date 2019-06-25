#pragma once

typedef int	cpu_type_t;
typedef int	cpu_subtype_t;

#define CPU_TYPE_ANY		((cpu_type_t) -1)

#define CPU_TYPE_VAX		((cpu_type_t) 1)
#define CPU_TYPE_ROMP		((cpu_type_t) 2)
#define CPU_TYPE_NS32032	((cpu_type_t) 4)
#define CPU_TYPE_NS32332        ((cpu_type_t) 5)
#define	CPU_TYPE_MC680x0	((cpu_type_t) 6)
#define CPU_TYPE_I386		((cpu_type_t) 7)
#define CPU_TYPE_X86_64		((cpu_type_t) (CPU_TYPE_I386 | CPU_ARCH_ABI64))
#define CPU_TYPE_MIPS		((cpu_type_t) 8)
#define CPU_TYPE_NS32532        ((cpu_type_t) 9)
#define CPU_TYPE_HPPA           ((cpu_type_t) 11)
#define CPU_TYPE_ARM		((cpu_type_t) 12)
#define CPU_TYPE_MC88000	((cpu_type_t) 13)
#define CPU_TYPE_SPARC		((cpu_type_t) 14)
#define CPU_TYPE_I860		((cpu_type_t) 15) // big-endian
#define	CPU_TYPE_I860_LITTLE	((cpu_type_t) 16) // little-endian
#define CPU_TYPE_RS6000		((cpu_type_t) 17)
#define CPU_TYPE_MC98000	((cpu_type_t) 18)
#define CPU_TYPE_POWERPC	((cpu_type_t) 18)
#define CPU_ARCH_ABI64		 0x1000000
#define CPU_ARCH_ABI64_32	 0x2000000
#define CPU_TYPE_POWERPC64	((cpu_type_t)(CPU_TYPE_POWERPC | CPU_ARCH_ABI64))
#define CPU_TYPE_VEO		((cpu_type_t) 255)
#define CPU_TYPE_ARM64		((cpu_type_t)(CPU_TYPE_ARM | CPU_ARCH_ABI64))
#define CPU_TYPE_ARM64_32	((cpu_type_t)(CPU_TYPE_ARM | CPU_ARCH_ABI64_32))

/*
 * 	Acorn subtypes - Acorn Risc Machine port done by
 *		Olivetti System Software Laboratory
 */

#define	CPU_SUBTYPE_ARM_ALL		((cpu_subtype_t) 0)
#define CPU_SUBTYPE_ARM_A500_ARCH	((cpu_subtype_t) 1)
#define CPU_SUBTYPE_ARM_A500		((cpu_subtype_t) 2)
#define CPU_SUBTYPE_ARM_A440		((cpu_subtype_t) 3)
#define CPU_SUBTYPE_ARM_M4		((cpu_subtype_t) 4)
#define CPU_SUBTYPE_ARM_V4T		((cpu_subtype_t) 5)
#define CPU_SUBTYPE_ARM_V6		((cpu_subtype_t) 6)
#define CPU_SUBTYPE_ARM_V5TEJ		((cpu_subtype_t) 7)
#define CPU_SUBTYPE_ARM_XSCALE		((cpu_subtype_t) 8)
#define CPU_SUBTYPE_ARM_V7		((cpu_subtype_t) 9)
#define CPU_SUBTYPE_ARM_V7F		((cpu_subtype_t) 10) /* Cortex A9 */
#define CPU_SUBTYPE_ARM_V7S		((cpu_subtype_t) 11) /* Swift */
#define CPU_SUBTYPE_ARM_V7K		((cpu_subtype_t) 12) /* Kirkwood40 */
#define CPU_SUBTYPE_ARM_V6M		((cpu_subtype_t) 14) /* Not meant to be run under xnu */
#define CPU_SUBTYPE_ARM_V7M		((cpu_subtype_t) 15) /* Not meant to be run under xnu */
#define CPU_SUBTYPE_ARM_V7EM		((cpu_subtype_t) 16) /* Not meant to be run under xnu */
#define CPU_SUBTYPE_ARM_V8		((cpu_subtype_t) 13)

#define	CPU_SUBTYPE_ARM64_ALL		((cpu_subtype_t) 0)
#define	CPU_SUBTYPE_ARM64_V8		((cpu_subtype_t) 1)

#define	CPU_SUBTYPE_ARM64_32_V8		((cpu_subtype_t) 1)
#define	CPU_SUBTYPE_ARM64E		((cpu_subtype_t) 2)
