#
# Makefile for compiling console tools
#
# Author: Werner Fink,  <werner@suse.de>
#

LOG_BUFFER_SIZE	= 65536
TRANS_BUFFER	=  4096
BOOT_LOGFILE	= /var/log/boot.log
BOOT_OLDLOGFILE = /var/log/boot.old
BOOT_FIFO	= /dev/blog

#DEBUG	 =	-DDEBUG=1
#DESTDIR =	/tmp/root
DEBUG	 =
DESTDIR	 =
MAJOR	 :=	2
MINOR	 :=	34
VERSION	 :=	$(MAJOR).$(MINOR)
DATE	 =	$(shell date +'%d%b%y' | tr '[:lower:]' '[:upper:]')
COPTS    =

#
# Architecture
#
	   ARCH = $(shell uname -m | sed 's@\(i\)[34567]\(86\)@\13\2@')
	 CFLAGS = $(RPM_OPT_FLAGS) $(COPTS) $(DEBUG) -D_GNU_SOURCE \
		  -DLOG_BUFFER_SIZE=$(LOG_BUFFER_SIZE) \
		  -DTRANS_BUFFER_SIZE=$(TRANS_BUFFER) \
		  -DBOOT_LOGFILE=\"$(BOOT_LOGFILE)\" \
		  -DBOOT_OLDLOGFILE=\"$(BOOT_OLDLOGFILE)\" \
		  -D_PATH_BLOG_FIFO=\"$(BOOT_FIFO)\" \
		  -ffunction-sections -Wall -pipe
	  CLOOP = -funroll-loops
ifeq ($(BLOGGER),1)
	 CFLAGS += -DBLOGGER
endif
	SEDOPTS = s|@@BOOT_LOGFILE@@|$(BOOT_LOGFILE)|;s|@@BOOT_OLDLOGFILE@@|$(BOOT_OLDLOGFILE)|
	     CC ?= gcc -g3
	     RM = rm -f
	  MKDIR = mkdir -p
	  RMDIR = rm -rf
   INSTBINFLAGS = -s -m 0700
	INSTBIN = install $(INSTBINFLAGS)
   INSTDOCFLAGS = -c -m 0644
	INSTDOC = install $(INSTDOCFLAGS)
   INSTCONFLAGS = -c -m 0644
	INSTCON = install $(INSTDOCFLAGS)
   INSTSCRFLAGS = -c -m 0755
	INSTSCR = install $(INSTSCRFLAGS)
	   LINK = ln -sf
	     AR ?= ar
	     LD ?= ld -shared -O2
	    SED = sed -r
	     SO = echo .so man8/

#
	 PREFIX ?= /
	MANPATH = $(PREFIX)usr/share/man
	SDOCDIR = $(MANPATH)/man8
	SBINDIR = $(PREFIX)sbin
	CONFDIR = $(PREFIX)etc
	 LSBDIR = $(PREFIX)usr/lib/lsb
	 LIBDIR = $(PREFIX)usr/lib
	 INCDIR = $(PREFIX)usr/include
      DRACUTMOD = $(PREFIX)usr/lib/dracut/modules.d/99blog
      SYSDUNITS = $(PREFIX)usr/lib/systemd/system
#
#
#
TODO	=	showconsole blogd blogger blogctl isserial libblogger.so
TODO	+=	blog-store-messages.service blog-umount.service blogd.8 blogger.8
L	:=	libconsole/
CFLAGS	+=	-I ./ -I ./$(L)
libfiles := $(sort $(wildcard $(L)*.c))

all: $(TODO)

%.o:		%.c
	$(CC) $(CFLAGS) $(CLOOP) -D_REENTRANT -fPIC -o $@ -c $< -pthread

%:	%.in
	$(SED) '$(SEDOPTS)' $< > $@

$(libfiles):  $(L)libconsole.h listing.h
libconsole.a: $(patsubst %.c,%.o,$(libfiles))
	$(AR) rusv $@ $^

libblogger.o:	libconsole.a
libblogger.o:	libblogger.c libblogger.h
	$(CC) $(CFLAGS) $(CLOOP) -fPIC -c $<

libblogger.so:	libblogger.o libconsole/signals.o
	$(CC) -shared  -Wl,-O2 -Wl,-soname=$@.$(MAJOR) -o $@ $^

showconsole:	showconsole.c libconsole.a
	$(CC) $(CFLAGS) $(CLOOP) -o $@ $^ -Wl,-O2 -Wl,-gc-sections -L ./ -lconsole -Wl,--as-needed -lutil -lrt -pthread

blogd:		blogd.c libconsole.a
	$(CC) $(CFLAGS) $(CLOOP) -D_REENTRANT -o $@ $< -Wl,-O2 -Wl,-gc-sections -L ./ -lconsole -Wl,--as-needed -lutil -lrt -pthread

blogctl:	blogctl.c libconsole.a
	$(CC) $(CFLAGS) $(CLOOP) -D_REENTRANT -o $@ $< -Wl,-O2 -Wl,-gc-sections -L ./ -lconsole -Wl,--as-needed -lutil -lrt -pthread

blogger:    blogger.c libblogger.so
	$(CC) $(CFLAGS) $(CLOOP) -o $@ $< -Wl,-O2 -Wl,-gc-sections -Wl,-rpath-link=. -L . -lblogger

isserial:	isserial.c
	$(CC) $(CFLAGS) $(CLOOP) -o $@ $^

clean:
	$(RM) *.o *.a *.so* *~ libconsole/*.o libconsole/*~ showconsole blogctl blogd blogger isserial $(patsubst %.in,%,$(wildcard *.in))

install:	$(TODO)
	$(MKDIR)	$(DESTDIR)$(SBINDIR)
	$(MKDIR)	$(DESTDIR)$(SDOCDIR)
	$(MKDIR)	$(DESTDIR)$(CONFDIR)
	$(MKDIR)	$(DESTDIR)$(LSBDIR)
	$(MKDIR)	$(DESTDIR)$(LIBDIR)
	$(MKDIR)	$(DESTDIR)$(INCDIR)
	$(MKDIR)	$(DESTDIR)$(DRACUTMOD)
	$(MKDIR)	$(DESTDIR)$(SYSDUNITS)
	$(INSTBIN) showconsole		$(DESTDIR)$(SBINDIR)/
	$(LINK)    showconsole		$(DESTDIR)$(SBINDIR)/setconsole
	$(INSTBIN) blogger		$(DESTDIR)$(SBINDIR)/
	$(INSTDOC) blogger.8		$(DESTDIR)$(SDOCDIR)/
	$(INSTDOC) showconsole.8	$(DESTDIR)$(SDOCDIR)/
	$(SO)showconsole.8 >		$(DESTDIR)$(SDOCDIR)/setconsole.8
	$(INSTBIN) blogd		$(DESTDIR)$(SBINDIR)/
	$(INSTDOC) blogd.8		$(DESTDIR)$(SDOCDIR)/
	$(INSTBIN) blogctl		$(DESTDIR)$(SBINDIR)/
	$(INSTDOC) blogctl.8		$(DESTDIR)$(SDOCDIR)/
	$(INSTCON) libblogger.h		$(DESTDIR)$(INCDIR)/
	$(INSTCON) libblogger.so	$(DESTDIR)$(LIBDIR)/libblogger.so.$(MAJOR).$(MINOR)
	$(INSTBIN) isserial		$(DESTDIR)$(SBINDIR)/
	$(INSTDOC) isserial.8		$(DESTDIR)$(SDOCDIR)/
	$(INSTSCR) module-setup.sh	$(DESTDIR)$(DRACUTMOD)/
	$(LINK) libblogger.so.$(MAJOR).$(MINOR)	$(DESTDIR)$(LIBDIR)/libblogger.so.$(MAJOR)
	$(LINK) libblogger.so.$(MAJOR).$(MINOR)	$(DESTDIR)$(LIBDIR)/libblogger.so
	@set +x; \
	export initdir=$(DESTDIR); \
	export systemdsystemunitdir=$(SYSDUNITS); \
	inst_multiple () { \
	    if test $$1 = -o; then \
		 shift; \
		 for o; do \
		     test -e $${o##*/} && command install -vp -m 0644 $${o##*/} $${initdir}$${o} || : ; \
		 done; \
	    else \
		command install -vp -m 0755 $${1##*/} $${initdir}$(SBINDIR)/; \
	    fi; \
	}; \
	ln_r () { \
	    local rel="$$(realpath -m --relative-to=$${2%/*}/ $${1%/*}/)"; \
	    ln -sf $${rel}/$${1##*/} $${initdir}$${2}; }; \
	. ./module-setup.sh ; \
	set -xe; install
	$(MKDIR)	$(DESTDIR)$(SYSDUNITS)/local-fs-pre.target.wants
	$(MKDIR)	$(DESTDIR)$(SYSDUNITS)/default.target.wants
	$(LINK) ../blog-umount.service	$(DESTDIR)$(SYSDUNITS)/local-fs-pre.target.wants/
	$(LINK) ../blog-quit.service	$(DESTDIR)$(SYSDUNITS)/default.target.wants/

#
# Make distribution
#
FILES	= README	\
	  COPYING	\
	  OTHERS	\
	  Makefile	\
	  libblogger.c	\
	  libblogger.h	\
	  libconsole/*.c	\
	  libconsole/*.h	\
	  listing.h	\
	  showconsole.8	\
	  showconsole.c	\
	  blogd.c	\
	  blogd.8.in	\
	  blogger.c	\
	  blogger.8.in	\
	  blogctl.c	\
	  blogctl.8	\
	  isserial.c	\
	  isserial.8	\
	  blog.service				\
	  blog-halt.service			\
	  blog-kexec.service			\
	  blog-poweroff.service			\
	  blog-quit.service			\
	  blog-reboot.service			\
	  blog-store-messages.service.in	\
	  blog-switch-root.service		\
	  blog-switch-initramfs.service		\
	  blog-umount.service.in		\
	  systemd-ask-password-blog.path	\
	  systemd-ask-password-blog.service	\
	  module-setup.sh			\
	  showconsole-$(VERSION).lsm

dest:
	$(MKDIR) showconsole-$(VERSION)
	@echo -e "Begin3\n\
Title:		console tools for boot scripts\n\
Version:	$(VERSION)\n\
Entered-date:	$(DATE)\n\
Description:	Used for fetch the real device in boot scripts\n\
x 		running on /dev/console.\n\
Keywords:	boot control\n\
Author:		Werner Fink <werner@suse.de>\n\
Maintained-by:	Werner Fink <werner@suse.de>\n\
Primary-site:	sunsite.unc.edu /pub/Linux/system/daemons/init\n\
x		@UNKNOWN showconsole-$(VERSION).tar.gz\n\
Alternate-site:	ftp.suse.com /pub/projects/init\n\
Platforms:	Linux with System VR2 or higher boot scheme\n\
Copying-policy:	GPL\n\
End" | sed 's@^ @@g;s@^x@@g' > showconsole-$(VERSION).lsm
	tar cpf - $(FILES) | tar -xpf - -C showconsole-$(VERSION)
	tar -cp -jf  showconsole-$(VERSION).tar.bz2 showconsole-$(VERSION)/
	$(RMDIR)    showconsole-$(VERSION)
	set -- `find showconsole-$(VERSION).tar.bz2 -printf '%s'` ; \
	sed "s:@UNKNOWN:$$1:" < showconsole-$(VERSION).lsm > \
	showconsole-$(VERSION).lsm.tmp ; \
	mv showconsole-$(VERSION).lsm.tmp showconsole-$(VERSION).lsm
