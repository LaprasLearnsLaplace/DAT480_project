# IPython log file

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
