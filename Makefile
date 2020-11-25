default: src/BOOTX64.EFI

include common.mk

OVMF=ovmf/bios64.bin
QEMU=qemu-system-x86_64
#-soundhw adlib
OSNAME=${shell uname -s}

QEMU_ARGS_COMMON=\
		  -device qemu-xhci -device usb-mouse \
		  -bios $(OVMF) \
		  -machine q35,nvdimm -cpu qemu64 -smp 4 \
		  -monitor stdio \
		  -monitor telnet:0.0.0.0:$(PORT_MONITOR),server,nowait \
		  -m 2G,slots=2,maxmem=4G \
		  -drive format=raw,file=fat:rw:mnt -net none \
		  -serial tcp::1234,server,nowait \
		  -serial tcp::1235,server,nowait

QEMU_ARGS_NET_MACOS=\
		-nic user,id=u1,model=virtio,hostfwd=tcp::10023-:23 \
		-object filter-dump,id=f1,netdev=u1,file=dump.dat

QEMU_ARGS_NET_MACOS_RTL=\
		-nic user,id=u1,model=rtl8139,hostfwd=tcp::10023-:23 \
		-object filter-dump,id=f1,netdev=u1,file=dump.dat

QEMU_ARGS_NET_LINUX=\
		--enable-kvm \
		-nic tap,ifname=tap0,id=u1,model=virtio,script=no \
		-object filter-dump,id=f1,netdev=u1,file=dump.dat

# guest 10.0.2.1:8888 -> host 127.0.0.1:8888
# guest 10.0.2.1:8889 <- host 127.0.0.1:8889

QEMU_ARGS_USER_NET_LINUX=\
		-chardev udp,id=m8,host=0.0.0.0,port=8888 \
		-nic user,id=u1,model=virtio,guestfwd=::8888-chardev:m8,hostfwd=udp:0.0.0.0:8889-0.0.0.0:8889 \
		-object filter-dump,id=f1,netdev=u1,file=dump.dat

ifeq ($(OSNAME),Darwin)
QEMU_ARGS=\
					$(QEMU_ARGS_COMMON) \
					$(QEMU_ARGS_NET_MACOS)
else
QEMU_ARGS=\
					--enable-kvm \
					$(QEMU_ARGS_COMMON) \
					$(QEMU_ARGS_NET_LINUX)
endif

QEMU_ARGS_PMEM=\
					 $(QEMU_ARGS) \
					 -object memory-backend-file,id=mem1,share=on,mem-path=pmem.img,size=2G \
					 -device nvdimm,id=nvdimm1,memdev=mem1
VNC_PASSWORD=a
PORT_MONITOR=2222
# PORT_VNC=N => port 5900 + N
PORT_VNC=5

ifdef SSH_CONNECTION
QEMU_ARGS+= -vnc 0.0.0.0:$(PORT_VNC),password
endif
	
src/BOOTX64.EFI : .FORCE
	make -C src

src/LIUMOS.ELF : .FORCE
	make -C src LIUMOS.ELF

.FORCE :

tools : .FORCE
	make -C tools

pmem.img :
	qemu-img create $@ 2G

app/% : .FORCE
	make -C $(dir $@)

deploy_apps : .FORCE
	make -C app/ deploy

files : src/BOOTX64.EFI src/LIUMOS.ELF .FORCE
	mkdir -p mnt/
	-rm -rf mnt/*
	mkdir -p mnt/EFI/BOOT
	cp src/BOOTX64.EFI mnt/EFI/BOOT/
	cp dist/* mnt/
	make deploy_apps
	cp src/LIUMOS.ELF mnt/LIUMOS.ELF
	mkdir -p mnt/EFI/EFI/
	echo 'FS0:\\EFI\\BOOT\\BOOTX64.EFI' > mnt/startup.nsh
	make -C rust_kernel deploy_release

run_nopmem : files .FORCE
	$(QEMU) $(QEMU_ARGS)

LLDB_ARGS = -o 'settings set interpreter.prompt-on-quit false' \
			-o 'process launch' \
			-o 'process handle -s false SIGUSR1 SIGUSR2'

run_xhci_gdb : files .FORCE
	lldb $(LLDB_ARGS) -- $(QEMU) $(QEMU_ARGS_XHCI) $(QEMU_ARGS)
	
run_root : files pmem.img .FORCE
	$(QEMU) $(QEMU_ARGS_PMEM)

run_rtl : files pmem.img .FORCE
	$(QEMU) $(QEMU_ARGS_COMMON) $(QEMU_ARGS_NET_MACOS_RTL)

run_user : files pmem.img .FORCE
	$(QEMU) $(QEMU_ARGS_COMMON) $(QEMU_ARGS_USER_NET_LINUX)

run_for_e2e_test : files pmem.img .FORCE
	$(QEMU) $(QEMU_ARGS) -nographic

run_user_headless : files pmem.img .FORCE
	( echo 'change vnc password $(VNC_PASSWORD)' | while ! nc localhost $(PORT_MONITOR) ; do sleep 1 ; done ) &
	$(QEMU) $(QEMU_ARGS_COMMON) $(QEMU_ARGS_USER_NET_LINUX) -vnc 0.0.0.0:$(PORT_VNC),password 

run_gdb_root : files pmem.img .FORCE
	$(QEMU) $(QEMU_ARGS_PMEM) -gdb tcp::1192 -S || reset

run_gdb_nogui_root : files pmem.img .FORCE
	( echo 'change vnc password $(VNC_PASSWORD)' | while ! nc localhost $(PORT_MONITOR) ; do sleep 1 ; done ) &
	$(QEMU) $(QEMU_ARGS_PMEM) -gdb tcp::1192 -S -vnc :0,password || reset

run_vnc : files pmem.img .FORCE
	( echo 'change vnc password $(VNC_PASSWORD)' | while ! nc localhost $(PORT_MONITOR) ; do sleep 1 ; done ) &
	$(QEMU) $(QEMU_ARGS_PMEM) -vnc :0,password || reset

gdb_root : .FORCE
	gdb -ex 'target remote localhost:1192' src/LIUMOS.ELF


install : files .FORCE
	@read -p "Write LIUMOS to /Volumes/LIUMOS. Are you sure? [Enter to proceed, or Ctrl-C to abort] " && \
		cp -r mnt/* /Volumes/LIUMOS/ && diskutil eject /Volumes/LIUMOS/ && echo "install done."

run_vb_dbg : .FORCE
	- VBoxManage storageattach liumOS --storagectl SATA --port 0 --medium none
	- VBoxManage closemedium disk liumos.vdi --delete
	make liumos.vdi
	VBoxManage storageattach liumOS --storagectl SATA --port 0 --medium liumos.vdi --type hdd
	VirtualBoxVM --startvm liumOS --dbg

run_docker : .FORCE
	scripts/make_run_with_docker.sh

img : files .FORCE
	dd if=/dev/zero of=liumos.img bs=16384 count=1024
	/usr/local/Cellar/dosfstools/4.1/sbin/mkfs.vfat liumos.img || /usr/local/Cellar/dosfstools/4.1/sbin/mkfs.fat liumos.img
	mkdir -p mnt_img
	hdiutil attach -mountpoint mnt_img liumos.img
	cp -r mnt/* mnt_img/
	hdiutil detach mnt_img

liumos.vdi : .FORCE img
	-rm liumos.vdi
	vbox-img convert --srcfilename liumos.img --srcformat RAW --dstfilename liumos.vdi --dstformat VDI

serial :
	screen /dev/tty.usbserial-* 115200

vnc :
	open vnc://localhost:5900

test :
	make -C src test
	make -C app/liumlib test

e2etest :
	make -C e2e_test test

ci :
	circleci config validate
	circleci local execute

clean :
	-rm cc_cache.gen.mk
	make -C src/ clean
	make -C app/ clean

format :
	make -C src format

commit_root : format test e2etest
	git add .
	./scripts/ensure_objs_are_not_under_git_control.sh
	git diff HEAD --color=always | less -R
	git commit
