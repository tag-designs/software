# List of all the board related files.
BOARDSRC = $(BOARDDIR)/IMUTagNandv2/board.c

# Required include directories
BOARDINC = $(BOARDDIR)/IMUTagNandv2

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
