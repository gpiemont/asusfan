obj-m := asus_fan.o
KMISC=/lib/modules/`uname -r`/kernel/drivers/acpi
default:
	$(MAKE) -C /lib/modules/`uname -r`/build M=`pwd` modules

install: default
	@install -d $(KMISC)
	@install -m 644 -c ./asus_fan.ko $(KMISC)
	@/sbin/depmod

uninstall:
	rm -rf $(KMISC)/asus_fan.ko
	rmdir $(KMISC)
	/sbin/depmod

clean:
	rm -rf *.o .*.cmd *.ko *.mod.c .tmp_versions Module.symvers Module.markers  modules.order

