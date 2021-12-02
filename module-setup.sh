#!/bin/bash

check() {
    require_binaries blogd blogctl || return 1
}

depends() {
    return 0
}

install() {
    inst_multiple blogd blogctl
    inst_multiple -o \
	$systemdsystemunitdir/blog.service \
	$systemdsystemunitdir/blog-final.service \
	$systemdsystemunitdir/blog-quit.service \
	$systemdsystemunitdir/blog-switch-root.service \
	$systemdsystemunitdir/systemd-ask-password-blog.path \
	$systemdsystemunitdir/systemd-ask-password-blog.service \
	$systemdsystemunitdir/systemd-vconsole-setup.service
    for t in default sysinit basic local-fs-pre rescue shutdown emergency initrd-switch-root
    do
	test  -d "${initdir}${systemdsystemunitdir}/${t}.target.wants" && continue
	mkdir -p "${initdir}${systemdsystemunitdir}/${t}.target.wants"
    done
    for s in blog-quit.service
    do
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/rescue.target.wants/${s}"
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/emergency.target.wants/${s}"
    done
    for s in blog.service
    do
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/basic.target.wants/${s}"
    done
    for s in blog-final.service
    do
	ln_r "${systemdsystemunitdir}/${s}" "${systemdsystemunitdir}/shutdown.target.wants/${s}"
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
