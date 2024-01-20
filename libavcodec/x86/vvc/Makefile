clean::
	$(RM) $(CLEANSUFFIXES:%=libavcodec/x86/vvc/%)

OBJS-$(CONFIG_VVC_DECODER)             += x86/vvc/vvcdsp_init.o \
                                          x86/h26x/h2656dsp.o
X86ASM-OBJS-$(CONFIG_VVC_DECODER)      += x86/vvc/vvc_mc.o       \
                                          x86/h26x/h2656_inter.o \
                                          x86/vvc/vvc_alf.o      \
                                          x86/vvc/vvc_sao.o      \
                                          x86/vvc/vvc_sao_10bit.o\
