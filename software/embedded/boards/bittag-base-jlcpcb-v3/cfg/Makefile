OUTPUTS = board.chcfg board.fmpp board.h.ftl board.mk.ftl board.c.ftl
$(BOARDIR)/board.mk : $(OUTPUTS)
	fmpp --freemarker-links "{lib : $(CHIBIOS)/tools/ftl/libs}" -C board.fmpp -v -O $(BOARDDIR)
