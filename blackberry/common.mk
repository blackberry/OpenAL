ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

define PINFO
PINFO DESCRIPTION=Open AL
endef

NAME=OpenAL

#===== EXTRA_INCVPATH - a space-separated list of directories to search for include files.
PUBLIC_INCVPATH=$(PRODUCT_ROOT)/include
EXTRA_INCVPATH+=$(PRODUCT_ROOT)/OpenAL32/Include

#===== EXTRA_SRCVPATH - a space-separated list of directories to search for source files.
EXTRA_SRCVPATH+=$(PRODUCT_ROOT)/Alc  \
	$(PRODUCT_ROOT)/OpenAL32

INSTALLDIR=$(firstword $(INSTALLDIR_$(OS)) usr/lib)
	
include $(MKFILES_ROOT)/qtargets.mk

OPTIMIZE_TYPE_g=none
OPTIMIZE_TYPE=$(OPTIMIZE_TYPE_$(filter g, $(VARIANTS)))
