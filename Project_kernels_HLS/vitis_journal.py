# IPython log file

client = vitis.create_client()
client.set_workspace("_x.xilinx_u55c_gen3x16_xdma_3_202210_1")
fir = client.get_component("krnl_proj")
fir.execute("C_SIMULATION")
#[Out]# ''
fir.execute("SYNTHESIS")
#[Out]# ''
