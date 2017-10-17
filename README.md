SFLL Attack Framework
=============================
This is a logic decryption tool  developed as a wrapper code around the SAT attack tool that was originally developed by Pramod Subramanyan et al.
We developed this platform to enable you to attack and decrypt our latest logic locking solution (SFLL-HD). SFLL refers to "Stripped functionality logic locking" and HD refers to "Hamming distance". SFLL-HD is a logic locking technique that thwarts all known variants of the SAT and removal attacks. For further details, refer to our CCS'17 paper, titled "Provably Secure Logic Locking: From Theory to Practice.”

The initial version SAT attack tool developed by Subramanyan required two inputs: 1. a locked (encrypted) netlitst and 2. an original netlist. The original netlist was used as an oracle to generate correct I/O patterns. 
Our version also requires a locked netlist, same as its predecessor. We provide you with a locked netlist "dfx_sfll_k256_h32.bench" in benchmarks/sfll_hd directory. This is an SFLL-HD netlist with 256-bit key (k) and the Hamming distance (h) set to 32. Unfortunately, we could not publicly share the original bench file (because of IP concerns). As such, we have replaced the original netlist with an executable (named DfX) that can generate the desired I/O patterns.
In a nutshell, we have modified only the interface to the decryption tool wrt to Subramanyan’s version; the rest of the tool is, more or less, identical. 

To launch the attack on the SFLL-HD netlist, please use the following command in /source/src directory:
./sld ../../benchmarks/sfll_hd/dfx_sfll_k256_h32.bench ../../benchmarks/original/empty.bench
Here empty.bench is an empty (or dummy) file with only one comment line. This file serves to keep the attack interface intact for users already familiar with the SAT attack framework. 


Installation
==============
To install the SAT attack tool, follow the instructions provided in README-OLD.txt (authored by Pramod et al.)
We have provided attack executables (names starting with sld) in /bin directory for 64-bit (default) and 32-bit Ubuntu. 

The 32-bit DfX  (oracle) executable is by default placed in /source/src directory. Executables compiled for multiple linux platforms are also placed in /bin directory. 
The DfX executable(s) has(have) been generated using Verilator tool from Veripool. In case observe some errors during execution of DfX executable, it might be helpful to install the tool available at: http://www.veripool.org/wiki/verilator


Minor Modifications:
========================================
We have made the following changes to the initial code. 
1. In solver.cpp,  we no longer use "simckt". Instead a new function  "queryOracle()" is used for simulating the oracle.
2. In sld.cpp and sim.cpp, a empty simckt constructor is used. Note that simckt is no more used during the attack. 
3. The modified tool does not support partial attacks, as we have removed the parts of the code that require IBM CPLEX.  
4. We have removed all previous benchmarks. The updated benchmark folder only contains SFLL-HD netlist.  

