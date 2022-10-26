#!/usr/bin/env python3

import sys
import numpy as np
import statistics
import matplotlib.pyplot as plt
import os

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Need path to trace to parse")
    inter_arrivals = []

    read_sizes = []
    write_sizes = []
    reads = 0
    writes = 0
    last_io_time = -1

    #format: f"{total_time:.5f} 0 {int(aligned_offset)} {int(aligned_size)} {ops[i]}\n"
    with open(sys.argv[1]) as f:
        for line in f:
            if line == "\n": break
            tok = map(str.strip, line.split())
            tok_list = list(tok)

            timestamp = float(tok_list[0])
            offset = int(tok_list[2])
            size = int(tok_list[3])
            op = int(tok_list[4])

            if op == 0: # read
                read_sizes.append(size)
                reads += 1
            else:
                writes += 1
                write_sizes.append(size)
                
            if last_io_time != -1:
                inter = timestamp - last_io_time
                inter_arrivals.append(inter)

            last_io_time = timestamp

    print("==========Statistics==========")
    print(f"Total ops {(writes+reads)}")
    print(f"{reads} Reads {writes} Writes")
    print(f"IO inter arrival time average {statistics.mean(inter_arrivals):.2f}us")
    print(f"IO inter arrival stddev {statistics.pstdev(inter_arrivals):.2f}us")
    print(f"Min/Max inter arrival time  {min(inter_arrivals):.2f}, {max(inter_arrivals):.2f}")
    print(f"Read size avg: {statistics.mean(read_sizes)/1024:.2f} KB")
    print(f"Read size stddev: {statistics.pstdev(read_sizes)/1024:.2f} KB")
    print(f"Write size avg: {statistics.mean(write_sizes)/1024:.2f} KB")
    last_io_time_s = last_io_time/(1000*1000) #us to s
    print(f"Avg IOPS  {(reads+writes)/ last_io_time_s} ")
    print(f"==============================")

    # count, x = np.histogram(inters, bins=500)
    # print("Inter arrival histogram (ms):")
    # #for i in range(len(x)-1):
    # for i in range(30):
    #     print(f"{x[i]}: {count[i]}")

    # count, bins_count = np.histogram(np_read_latencies, bins=100)  
    # # finding the PDF of the histogram using count values
    # pdf = count / sum(count)
    # # using numpy np.cumsum to calculate the CDF
    # # We can also find using the PDF values by looping and adding
    # cdf = np.cumsum(pdf)
    # plt.plot(bins_count[1:], cdf, label="CDF")


    # # sort the data:
    # data_sorted = np.sort(np_read_latencies)
    # # calculate the proportional values of samples
    # p = 1. * np.arange(len(np_read_latencies)) / (len(np_read_latencies) - 1)
    # plt.plot(data_sorted, p)

    # #plt.legend()
    # plt.grid(visible=True)
    # plt.xlabel('Latency (us)')
    # plt.ylabel('CDF %')
    # #plt.ylim(bottom=0)
    # plt.title(sys.argv[1])
    # plt.xlim(right=np.percentile(np_read_latencies, 99.9), left=min(np_read_latencies))
    
    # plt.savefig(sys.argv[1]+"_cdf.pdf")