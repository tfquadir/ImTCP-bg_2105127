#!/bin/bash
set -e

./ns3 build
rm -f results/summary.csv

./ns3 run "scratch/imtcpbg_eval --bgTcp=westwood --nFg=20 --fgBytes=300000 --stop=30"


./ns3 run "scratch/imtcpbg_eval --bgTcp=imtcpbg --gamma=0.62 --delta=1.4 --measureEveryRTT=1 --minCwndSegments=8 --EnableRttFallback=1 --nFg=20 --fgBytes=300000 --stop=30"


./ns3 run "scratch/imtcpbg_eval --bgTcp=imtcpbgimproved --gamma=0.125 --delta=1.8 --measureEveryRTT=1 --minCwndSegments=8 --EnableRttFallback=1 --EnableAdaptiveDelta=1 --DeltaMin=1.3 --DeltaMax=3.0 --DeltaStepUp=0.1 --DeltaStepDown=0.02 --AdaptWindow=16 --nFg=20 --fgBytes=300000 --stop=30"
