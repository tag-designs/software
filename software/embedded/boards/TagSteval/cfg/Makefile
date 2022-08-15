$(BOARDDIR)/board.chcfg:  board.chcfg board.fmpp board.h.ftl board.mk.ftl board.c.ftl
	 fmpp --freemarker-links "{lib : $(CHIBIOS)/tools/ftl/libs}" -C board.fmpp -v -O $(BOARDDIR)
