# Hardlog: Practical Tamper-Proof System Auditing Using a Novel Audit Device

Prototype source code for the [Hardlog research paper](https://www.microsoft.com/en-us/research/uploads/prod/2022/04/hardlog-sp22.pdf), presented at IEEE S&P (*Oakland*) 2022. If you find this repository useful, please cite our paper/repository.

    @inproceedings{ahmad2022hardlog,
        author = {Ahmad, Adil and Lee, Sangho and Peinado, Marcus},
        title = {HardLog: Practical Tamper-Proof System Auditing Using a Novel Audit Device},
        booktitle = {43rd IEEE Symposium on Security and Privacy (Oakland 2022)},
        year = {2022},
        month = {May},
        url = {https://www.microsoft.com/en-us/research/publication/hardlog-practical-tamper-proof-system-auditing-using-a-novel-audit-device/},
    }

    @misc{hardlog-github-repo,
        title={Hardlog: Practical and Effective System Auditing using a Novel Audit Device},
        howpublished={\url{https://github.com/microsoft/HardLog}}
    }

## 1. Machine Setup

Hardlog requires two machines---audit device and host---connected through a USB-3.0 interface. While there are numerous machine configurations that should work, we provide below the specifications for machines that we tested with.

### Audit Device 

We used a RockPro64 development board, equipped with an RK3399 ARM CPU. The board came with 2 GB of main memory. We attached a 250 GB SSD (tested with WD SN550 and Samsung 950 Pro) using its PCIe interface. On the software side, the board was running Armbian 21.05 (Buster) with Linux v4.4.213. The details about building the correct software toolchain (including OS) is provided below.

### Host

The host machine came with an Intel (c) Core i7-8700 x86 CPU with 16 GB of DDR4 memory. The host machine had a 512 GB SSD (Samsung 970 Pro), connected through PCIe 3. On the software side, the machine ran Elementary OS Hera 5.1.7 with Linux v5.4.97. We also partly tested with a vanilla Ubuntu 20.04 OS.


## 2. Directory Setup

This repository contains two main folders---host and device. The host directory contains scripts to build the kernel, module and library for the host machine. The device folder contains similar scripts for the audit device.

## 3. Build Directions

Please follow the steps outlined in the host and device folder READMEs, respectively. 
