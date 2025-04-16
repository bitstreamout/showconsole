#!/bin/bash

check() {
    require_binaries blogd blogctl || return 1
}

depends() {
    echo systemd-initrd systemd-udevd
}

install() {
    inst_multiple blogd blogctl
    inst_multiple -o \
	$systemdsystemunitdir/blog.service \
	$systemdsystemunitdir/blog-halt.service \
	$systemdsystemunitdir/blog-kexec.service \
	$systemdsystemunitdir/blog-poweroff.service \
	$systemdsystemunitdir/blog-reboot.service \
	$systemdsystemunitdir/blog-quit.service \
	$systemdsystemunitdir/blog-store-messages.service \
	$systemdsystemunitdir/blog-switch-initramfs.service \
	$systemdsystemunitdir/blog-switch-root.service \
	$systemdsystemunitdir/blog-umount.service \
	$systemdsystemunitdir/systemd-ask-password-blog.path \
	$systemdsystemunitdir/systemd-ask-password-blog.service \
	$systemdsystemunitdir/systemd-vconsole-setup.service
    for t in sysinit rescue shutdown emergency initrd-switch-root halt kexec poweroff reboot
    do
	test  -d "${initdir}${systemdsystemunitdir}/${t}.target.wants" && continue
	mkdir -p "${initdir}${systemdsystemunitdir}/${t}.target.wants"
    done
    for s in blog-quit.service
    do
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/rescue.target.wants/${s}"
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/emergency.target.wants/${s}"
    done
    for s in blog.service systemd-ask-password-blog.path
    do
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/sysinit.target.wants/${s}"
    done
    for u in reboot poweroff kexec halt
    do
	ln_r "${systemdsystemunitdir}/blog-${u}.service" "${systemdsystemunitdir}/${u}.target.wants/blog-${u}.service"
	ln_r "${systemdsystemunitdir}/blog-switch-initramfs.service" "${systemdsystemunitdir}/${u}.target.wants/blog-switch-initramfs"
    done
    for t in systemd-ask-password-blog.service
    do
	test  -d "${initdir}${systemdsystemunitdir}/${t}.wants" && continue
	mkdir -p "${initdir}${systemdsystemunitdir}/${t}.wants"
    done
    for s in blog.service blog-switch-root.service
    do
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/initrd-switch-root.target.wants/${s}"
    done
    for s in systemd-vconsole-setup.service
    do
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/systemd-ask-password-blog.service.wants/${s}"
    done
}
