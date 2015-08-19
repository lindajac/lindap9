#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x2ab9dba5, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x2c7ad0a9, __VMLINUX_SYMBOL_STR(alloc_pages_current) },
	{ 0x8733c9e1, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0xc897c382, __VMLINUX_SYMBOL_STR(sg_init_table) },
	{ 0xedbc6f67, __VMLINUX_SYMBOL_STR(gnttab_end_foreign_access) },
	{ 0xe4b98961, __VMLINUX_SYMBOL_STR(xenbus_frontend_closed) },
	{ 0x754d539c, __VMLINUX_SYMBOL_STR(strlen) },
	{ 0xd109c4e8, __VMLINUX_SYMBOL_STR(p9_client_cb) },
	{ 0xbd077a25, __VMLINUX_SYMBOL_STR(v9fs_unregister_trans) },
	{ 0xfe727411, __VMLINUX_SYMBOL_STR(get_phys_to_machine) },
	{ 0x55526907, __VMLINUX_SYMBOL_STR(xen_features) },
	{ 0xb6230f1f, __VMLINUX_SYMBOL_STR(gnttab_grant_foreign_access) },
	{ 0x202165b1, __VMLINUX_SYMBOL_STR(v9fs_register_trans) },
	{ 0x8c06a108, __VMLINUX_SYMBOL_STR(xenbus_transaction_start) },
	{ 0x964add15, __VMLINUX_SYMBOL_STR(xenbus_scanf) },
	{ 0x1e12b70c, __VMLINUX_SYMBOL_STR(mutex_unlock) },
	{ 0x13cfdc77, __VMLINUX_SYMBOL_STR(p9_tag_lookup) },
	{ 0x86623fd7, __VMLINUX_SYMBOL_STR(notify_remote_via_irq) },
	{ 0xf432dd3d, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0xe41534ce, __VMLINUX_SYMBOL_STR(bind_evtchn_to_irqhandler) },
	{ 0x8f64aa4, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_irqrestore) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x4c9d28b0, __VMLINUX_SYMBOL_STR(phys_base) },
	{ 0x5d8987d9, __VMLINUX_SYMBOL_STR(xenbus_unregister_driver) },
	{ 0x5a921311, __VMLINUX_SYMBOL_STR(strncmp) },
	{ 0x77f4cc43, __VMLINUX_SYMBOL_STR(mutex_lock) },
	{ 0xca81ea9a, __VMLINUX_SYMBOL_STR(xenbus_transaction_end) },
	{ 0x9f8da6c6, __VMLINUX_SYMBOL_STR(xenbus_alloc_evtchn) },
	{ 0x78764f4e, __VMLINUX_SYMBOL_STR(pv_irq_ops) },
	{ 0x93fca811, __VMLINUX_SYMBOL_STR(__get_free_pages) },
	{ 0xa1ddddf5, __VMLINUX_SYMBOL_STR(xenbus_grant_ring) },
	{ 0x43261dca, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irq) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0x20705009, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0xfbf6264d, __VMLINUX_SYMBOL_STR(__dynamic_dev_dbg) },
	{ 0x9327f5ce, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irqsave) },
	{ 0x4302d0eb, __VMLINUX_SYMBOL_STR(free_pages) },
	{ 0x848361ce, __VMLINUX_SYMBOL_STR(xenbus_switch_state) },
	{ 0x73013896, __VMLINUX_SYMBOL_STR(xenbus_printf) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x69acdf38, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0x20ce25d3, __VMLINUX_SYMBOL_STR(xenbus_dev_fatal) },
	{ 0x18729f0, __VMLINUX_SYMBOL_STR(xenbus_register_frontend) },
	{ 0x7712771a, __VMLINUX_SYMBOL_STR(unbind_from_irqhandler) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=9pnet";

