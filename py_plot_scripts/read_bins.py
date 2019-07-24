import h5py
import numpy as np

def get_bins(folder, bin_type):
    with open(folder+"bin_strs.txt","r") as f:
        lines = f.read().splitlines()
    lines = np.array(lines)

    line_num = np.where(lines == bin_type+"_bins_str:")[0][0] + 1
    bins_str = lines[line_num] 
    
    x = bins_str.replace(';',':')
    x = x.split(":")
    x = x[::2]
    left_edges = np.array([float(i) for i in x])
    centers = np.sqrt(left_edges[0:-1]*left_edges[1:])
    widths = left_edges[1:] - left_edges[0:-1]
    return (centers, widths)

# def get_bins(folder, bin_type):
#     with h5py.File(folder+"const.h5","r") as f:
#         if bin_type == "wet":
#             bins_str = np.array(f["out_wet_str"])
#         if bin_type == "dry":
#             bins_str = np.array(f["out_dry_str"])
    
#     #bins_str = "1e-09:1.584893192461111e-09|0,1,2,3,6;1.584893192461111e-09:2.511886431509582e-09|0,1,2,3,6;2.511886431509582e-09:3.981071705534969e-09|0,1,2,3,6;3.981071705534969e-09:6.309573444801943e-09|0,1,2,3,6;6.309573444801943e-09:1e-08|0,1,2,3,6;1e-08:1.5848931924611143e-08|0,1,2,3,6;1.5848931924611143e-08:2.511886431509582e-08|0,1,2,3,6;2.511886431509582e-08:3.981071705534969e-08|0,1,2,3,6;3.981071705534969e-08:6.30957344480193e-08|0,1,2,3,6;6.30957344480193e-08:1e-07|0,1,2,3,6;1e-07:1.584893192461114e-07|0,1,2,3,6;1.584893192461114e-07:2.5118864315095823e-07|0,1,2,3,6;2.5118864315095823e-07:3.981071705534969e-07|0,1,2,3,6;3.981071705534969e-07:6.309573444801942e-07|0,1,2,3,6;6.309573444801942e-07:1e-06|0,1,2,3,6;1e-06:1.584893192461114e-06|0,1,2,3,6;1.584893192461114e-06:2.5118864315095823e-06|0,1,2,3,6;2.5118864315095823e-06:3.981071705534969e-06|0,1,2,3,6;3.981071705534969e-06:6.309573444801943e-06|0,1,2,3,6;6.309573444801943e-06:1e-05|0,1,2,3,6;1e-05:1.584893192461114e-05|0,1,2,3,6;1.584893192461114e-05:2.5118864315095822e-05|0,1,2,3,6;2.5118864315095822e-05:3.9810717055349776e-05|0,1,2,3,6;3.9810717055349776e-05:6.309573444801943e-05|0,1,2,3,6;6.309573444801943e-05:0.0001|0,1,2,3,6"
#     x = bins_str.replace(';',':')
#     x = x.split(":")
#     x = x[::2]
#     left_edges = np.array([float(i) for i in x])
#     centers = np.sqrt(left_edges[0:-1]*left_edges[1:])
#     widths = left_edges[1:] - left_edges[0:-1]
#     return (centers, widths)