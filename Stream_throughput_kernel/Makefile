DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
FREQ ?= 300000000
XSA := $(strip $(patsubst %.xpfm, % , $(shell basename $(DEVICE))))
VPP := $(XILINX_VITIS)/bin/v++
VIVADO := $(XILINX_VIVADO)/bin/vivado

build/frontend/frontend/hls/impl/ip/hdl/verilog: frontend.*
	mkdir -p build/frontend/frontend
	$(VPP) --compile --mode hls --work_dir build/frontend/frontend --config frontend.cfg --platform $(DEVICE) frontend.cpp
.PHONY: package-hls
package-hls: build/frontend/frontend/hls/impl/ip/hdl/verilog

.PHONY: package-rtl
package-rtl: build/stream_throughput.xo
build/stream_throughput.xo: build/frontend/frontend/hls/impl/ip/hdl/verilog rtl/* kernel.xml package_kernel.tcl
	# Prepare RTL packaging folder for v++ --package
	$(VIVADO) -mode batch -source package_kernel.tcl -notrace -tclargs $@ stream_throughput $(XSA) stream_throughput_top

clean:
	rm -rf build logs vivado*
