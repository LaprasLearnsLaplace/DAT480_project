# IPython log file

client = vitis.create_client()
client.set_workspace("_x.xilinx_u55c_gen3x16_xdma_3_202210_1")
#[Out]# True
k= client.get_component("krnl_proj")
k.execute("SYNTHESIS")
k.execute("SYNTHESIS")
#[Out]# ''
k.execute("SYNTHESIS")
#[Out]# ''
