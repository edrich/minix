.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=	3c90x
SUBDIR+=	atl2
SUBDIR+=	dec21140A
SUBDIR+=	dp8390
SUBDIR+=	dpeth
SUBDIR+=	e1000
SUBDIR+=	fxp
SUBDIR+=	ip1000
SUBDIR+=	lance
SUBDIR+=	rtl8139
SUBDIR+=	rtl8169
SUBDIR+=	virtio_net
SUBDIR+=	vt6105
.endif # ${MACHINE_ARCH} == "i386"

# The drivers for network cards QEMU emulates (ne2k_pci, e1000, pcnet,
# rtl8139, virtio-net-pci), which are tested with it.  (fxp does not work
# with QEMU's Intel 8255x models on i386 either.)
.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=	dp8390
SUBDIR+=	e1000
SUBDIR+=	lance
SUBDIR+=	rtl8139
SUBDIR+=	virtio_net
.endif # ${MACHINE_ARCH} == "x86_64"

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	lan8710a
.endif # ${MACHINE_ARCH} == "earm"

.include <bsd.subdir.mk>
