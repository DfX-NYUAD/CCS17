New Information (for SFLL HD)
=============================

This a logic depcryption tool (aka SAT attack tool). It was originally developed by Pramod Subramanyan et al.

The aim of developing this modified version of the tool is to allow researchers to launch SAT and other attacks on our SFLL-HD netlist. 
SFLL refers to "Stripped functionality logic locking" and HD refers to "Hamming distance". 
SFLL-HD is a logic locking technqiue that thwarts all known variants of the SAT and removal attacks. For further details,  refer to our CCS'17 paper, titled "Provably Secure Logic Locking: From Theory to Practice".

The initial version SAT attack tool required two inputs: 1. a locked (encrypted) netlitst and 2. an original netlist. The original netlist was used as an oracle to generate correct I/O patterns. 
The modified tool also requires a locked netlist, same as its predecessor. We provide you with a locked netlist "dfx_sfll_k256_h32.bench" benchmarks/sfll_hd directory. This is an SFLL-HD netlist with 256 bit key (k) and the Hamming distance (h) set to 32. 
Unfortunately, we could not publicly share the original bench file (because of IP concerns). 
We have replaced the original netlist with an executable (named DfX) that can generate the desired I/O patterns. 


In a nutshell, we have modified only the interface to the idecryption tool; the rest of the tool isi,ore of less,in it's original form.
To launch the attack on the neltist provided, use the following command in /source/src directory:

./sld ../../benchmarks/sfll_hd/dfx_sfll_k256_h32.bench ../../benchmarks/original/empty.bench
 
Here empty.bench is an empty (or dummy) file with only one comment line. This file servers to keep the attack interface intact for user already familiar with it. 


Installation
==============
To install the SAT attack tool, follow the instructions provided in README-OLD.txt (by Pramod et al.)
We have provided attack executables (names starting with sld) in /bin directory for 64-bit (default) and 32-bit Ubuntu. 

The DfX  (oracle) executable is by default placed in /source/src directory. Executables compiled for different platforms are also placed in /bin directory. 
The DfX executable(s) has(have) been generated using Verilator tool from Veripool. We recommend to install the tool available at: http://www.veripool.org/wiki/verilator


Minor Modifications:
========================================
To make the above mentioned changes to the tool, we have made the following changes to the initial code. 
1. In solver.cpp,  we no longer use "simckt". Instead a new function  "queryOracle()" is used for simulating the oracle.
2. In sld.cpp and sim.cpp, a empty simckt constructor is used. Note that simckt is no more used during the attack. 
3. The modified tool does not support partial attack, as we have removed the parts requiring IBM CPLEX.  
4. We have removed all previous benchmarks. The updated benchmark folder only contains sfll_hd netlist.  

For further more information, look at the README files (authored by Pramod Subramanyan et al). 
