# Artifact Evaluation Submission for Aeolia [SOSP '25] 

This repository contains the artifact for reproducing our SOSP'25 paper "Aeolia: Fast and Secure Userspace Interrupt-Based Storage Stack". 

# Overview 
The experimental results of Aeolia mainly include three parts: motivation related experiment, AeoDriver related experiment and AeoFS related experiment.
We put these experiments in different folders independently for the corresponding tests.

### Structure:

```
|---- eval-motivation       (evaluations in motivation section)
|---- eval-driver           (evaluations about AeoDriver)
|---- eval-fs               (evaluations about AeoFS)
|---- dep.sh                (scripts to install dependency)
```

### Environment: 
Aeolia requires an Intel CPU with the UINTR feature (Intel Xeon 4th Gen or later).
The Linux kernel needs to be customized to support UINTR. Please refer to the eval-motivation README for instructions on replacing the kernel.
Additionally, since some experiments require temporary modifications to the kernel code, it is highly recommended to use a dedicated server.
The server used for the experiments in the paper is configured with 1000 GB of memory and an Optane SSD 5800X.


### Install the dependencies:
```
$ ./dep.sh 
```




