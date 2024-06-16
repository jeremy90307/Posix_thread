CFLAGS=-O0 -Wall -Wextra -I.. -MMD -DUSE_LINUX -fsanitize=thread
LDFLAGS= -lpthread -fsanitize=thread

BINARY = test_priority

CSRCS = $(shell find . -name '*.c')
_COBJ =  $(notdir $(CSRCS))
COBJS = $(_COBJ:%.c=%.o)
PWD := $(shell pwd)

all: $(BINARY)

# This should be run as root because we'll modify the schedule policy
run: $(BINARY)
	sudo taskset -c 1 $(PWD)/$(BINARY)
clean:
	$(RM) $(COBJS)
	$(RM) $(BINARY)
	$(RM) *.d

$(BINARY): $(COBJS)
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	@$(CC) -c $(CFLAGS) $< -o $@

-include *.d
