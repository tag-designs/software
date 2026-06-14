# List of all the board related files.
BOARDSRC = $(BOARDDIR)/IMUTagv1/board.c

# Required include directories
BOARDINC = $(BOARDDIR)/IMUTagv1

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
