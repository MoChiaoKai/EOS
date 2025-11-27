#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xd1614c70, "module_layout" },
	{ 0x37a0cba, "kfree" },
	{ 0xdcb764ad, "memset" },
	{ 0xf9a482f9, "msleep" },
	{ 0x84bc974b, "__arch_copy_from_user" },
	{ 0xd2b09ce5, "__kmalloc" },
	{ 0xdb7305a1, "__stack_chk_fail" },
	{ 0x4829a47e, "memcpy" },
	{ 0x8f678b07, "__stack_chk_guard" },
	{ 0x7d6ba1f, "gpiod_set_raw_value" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0xd1c90ddd, "class_destroy" },
	{ 0xfe990052, "gpio_free" },
	{ 0x20a08b59, "device_destroy" },
	{ 0x733eb4b9, "cdev_del" },
	{ 0x86030d9c, "gpiod_direction_output_raw" },
	{ 0x6621f6f, "gpio_to_desc" },
	{ 0x47229b5c, "gpio_request" },
	{ 0x3dc6510e, "device_create" },
	{ 0x25f39b1f, "cdev_add" },
	{ 0x1d1ce098, "cdev_init" },
	{ 0xba7e2088, "__class_create" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x7c32d0f0, "printk" },
	{ 0x1fdc7df2, "_mcount" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "D91F221CF937D5528D14C96");
