#!/bin/bash

sudo insmod lake_gcm.ko cubin_path=$(readlink -f ./gcm_kernels.cubin)