# ==============================================================================
#
# Defines.common.mk
#
# ==============================================================================


CC       := upcxx
CFLAGS   += -Wall -pthread 
CFLAGS   += -O3 
CFLAGS   += -I$(LIB)
CPP      := upcxx
#CPPFLAGS += $(CFLAGS)
LD       := upcxx
LIBS     += -lpthread

# Remove these files when doing clean
OUTPUT +=

LIB := ../lib

STM := ../../tl2

LOSTM := ../../OpenTM/lostm


# ==============================================================================
#
# End of Defines.common.mk
#
# ==============================================================================
