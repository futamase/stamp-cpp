# ==============================================================================
#
# Defines.common.mk
#
# ==============================================================================


CFLAGS += -DLIST_NO_DUPLICATES
CFLAGS += -DMAP_USE_RBTREE

PROG := vacation

SRCS += \
	client.c \
	customer.c \
	manager.c \
	reservation.c \
	vacation.c \
	$(LIB)/list.c \
	$(LIB)/pair.c \
	$(LIB)/mt19937ar.c \
	$(LIB)/random.c \
	$(LIB)/thread.c 
SRCSCPP += \
	$(LIB)/stm.cpp \
	$(LIB)/dtm.cpp \ 
	$(LIB)/MurmurHash3.cpp \
	$(LIB)/rbtree.cpp \
#
OBJS := ${SRCS:.c=.o} ${SRCSCPP:.cpp=.o}


# ==============================================================================
#
# End of Defines.common.mk
#
# ==============================================================================
