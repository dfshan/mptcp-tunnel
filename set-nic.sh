#!/bin/bash

dev0=em1
dev1=p1p1
dev2=p1p2


ethtool -K $dev0 tso off gso off gro off lro off
ethtool -K $dev1 tso off gso off gro off lro off
ethtool -K $dev2 tso off gso off gro off lro off
