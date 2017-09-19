# Makefile for hev-socks5-server

PROJECT=hev-socks5-server

CROSS_PREFIX :=
PP=$(CROSS_PREFIX)cpp
CC=$(CROSS_PREFIX)gcc
STRIP=$(CROSS_PREFIX)strip
MARCH:=native
CCFLAGS=-march=$(MARCH) -O3 -Wall -Werror \
		-I$(THIRDPARTDIR)/ini-parser/src \
		-I$(THIRDPARTDIR)/hev-task-system/src
LDFLAGS=-L$(THIRDPARTDIR)/ini-parser/bin -lini-parser \
		-L$(THIRDPARTDIR)/hev-task-system/bin -lhev-task-system \
		-lpthread

SRCDIR=src
BINDIR=bin
BUILDDIR=build
THIRDPARTDIR=third-part

TARGET=$(BINDIR)/hev-socks5-server
THIRDPARTS=$(THIRDPARTDIR)/ini-parser $(THIRDPARTDIR)/hev-task-system

CCOBJS=$(wildcard $(SRCDIR)/*.c)
LDOBJS=$(patsubst $(SRCDIR)%.c,$(BUILDDIR)%.o,$(CCOBJS))
DEPEND=$(LDOBJS:.o=.dep)

BUILDMSG="\e[1;31mBUILD\e[0m $<"
LINKMSG="\e[1;34mLINK\e[0m  \e[1;32m$@\e[0m"
STRIPMSG="\e[1;34mSTRIP\e[0m \e[1;32m$@\e[0m"
CLEANMSG="\e[1;34mCLEAN\e[0m $(PROJECT)"

.PHONY: all clean tp-all tp-clean

all : tp-all $(TARGET)

tp-all : $(THIRDPARTS)
	@$(foreach dir,$^,make --no-print-directory -C $(dir);)

tp-clean : $(THIRDPARTS)
	@$(foreach dir,$^,make --no-print-directory -C $(dir) clean;)

clean : tp-clean
	@$(RM) $(BINDIR)/* $(BUILDDIR)/*
	@echo -e $(CLEANMSG)

$(TARGET) : $(LDOBJS)
	@$(CC) -o $@ $^ $(LDFLAGS)
	@echo -e $(LINKMSG)
	@$(STRIP) $@
	@echo -e $(STRIPMSG)

$(BUILDDIR)/%.dep : $(SRCDIR)/%.c
	@$(PP) $(CCFLAGS) -MM -MT $(@:.dep=.o) -o $@ $<

$(BUILDDIR)/%.o : $(SRCDIR)/%.c
	@$(CC) $(CCFLAGS) -c -o $@ $<
	@echo -e $(BUILDMSG)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPEND)
endif
