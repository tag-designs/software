# List of all the board related files.
BOARDSRC = $(BOARDDIR)/BITTAG_BASE_V7/board.c

# Required include directories
BOARDINC = $(BOARDDIR)/BITTAG_BASE_V7

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
