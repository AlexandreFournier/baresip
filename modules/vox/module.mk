#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

MOD		:= vox
$(MOD)_SRCS	+= vox.c
$(MOD)_LFLAGS	+= -lwiringPi

include mk/mod.mk
