TARGET   = test-libc_aio
SRC_C    = aio.c
LIBS     = posix libc
INC_DIR += $(LIBC_DIR)/sys/sys
