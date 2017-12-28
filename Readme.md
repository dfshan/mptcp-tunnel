# About
This program is used to aggregate the bandwidth of multiple heterogeneous access networks through MPTCP.

# Requirements
* libConfuse >= 2.7

# How to use
1. Build the project:

    make

2. Create your configuration file `p2s.cfg` and `s2p.cfg`.
See `p2s.cfg.example` and `s2p.cfg.example` as examples.

3. Turn off offload features of NIC:

    ethtool -K $dev0 tso off gso off gro off lro off
