#-----------------------------------------------------------------
# Vitis v2023.1 (64-bit)
# Start of session at: Wed Jan  7 21:42:54 2026
# Current directory: /home/m2_1/dat480_project_base/Project_kernels_HLS
# Command line: vitis -i
# Journal file: vitis_journal.py
# Batch mode: $XILINX_VITIS/bin/vitis -new -s /home/m2_1/dat480_project_base/Project_kernels_HLS/vitis_journal.py
#-----------------------------------------------------------------

#!/usr/bin/env python3
import vitis
client = vitis.create_client()
client.set_workspace("_x.xilinx_u55c_gen3x16_xdma_3_202210_1")
#[Out]# True
krnl.execute("C_SIMULATION")
krnl = client.get_component('krnl_proj')
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
#[Out]# ''
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
#[Out]# ''
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
#[Out]# ''
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
krnl.execute("SYNTHESIS")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
krnl.execute("C_SIMULATION")
#[Out]# ''
krnl.execute("SYNTHESIS")
krnl.execute("SYNTHESIS")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
krnl.execute("C_SIMULATION")
exit()
vitis.dispose()
