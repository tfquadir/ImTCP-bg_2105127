# #!/bin/bash

./ns3 build
rm -f results/summary.csv

# Fixed parameters
nFg=20
fgBytes=300000
stop=30
measureEveryRTT=1
minCwndSegments=8
EnableRttFallback=1

# gamma $(seq 0.1 0.01 0.9)
# delta $(seq 1 0.5 3)

# Gamma: 0.61 to 0.63 (step 0.005)
for gamma in $(seq 0.55 0.01 0.72)
do
    # Delta: 1 to 3 (step 0.5)
    for delta in 1.4
    do
        echo "Running gamma=$gamma delta=$delta"

        ./ns3 run "scratch/imtcpbg_eval \
        --bgTcp=imtcpbg \
        --gamma=$gamma \
        --delta=$delta \
        --measureEveryRTT=$measureEveryRTT \
        --minCwndSegments=$minCwndSegments \
        --EnableRttFallback=$EnableRttFallback \
        --nFg=$nFg \
        --fgBytes=$fgBytes \
        --stop=$stop"
    done
done

echo "Sweep completed!"



#!/bin/bash

# set -e

# ./ns3 build
# rm -f results/summary.csv

# # Fixed parameters
# nFg=20
# fgBytes=300000
# stop=30
# measureEveryRTT=1
# minCwndSegments=8
# EnableRttFallback=1

# # Improved TCP params
# EnableAdaptiveDelta=1
# DeltaMin=1.15
# DeltaMax=2.0
# # DeltaStepUp=0.01
# DeltaStepDown=0.02
# AdaptWindow=8
# delta=2
# gamma=0.125

# # Gamma sweep
# for DeltaStepUp in $(seq 0.1 0.01 0.3)
# do
#     # Delta sweep
#     for DeltaStepDown in $(seq 0.005 0.005 0.03)
#     do
#         echo "Running IMPROVED gamma=$gamma delta=$delta"

#         ./ns3 run "scratch/imtcpbg_eval \
#         --bgTcp=imtcpbgimproved \
#         --gamma=$gamma \
#         --delta=$delta \
#         --measureEveryRTT=$measureEveryRTT \
#         --minCwndSegments=$minCwndSegments \
#         --EnableRttFallback=$EnableRttFallback \
#         --EnableAdaptiveDelta=$EnableAdaptiveDelta \
#         --DeltaMin=$DeltaMin \
#         --DeltaMax=$DeltaMax \
#         --DeltaStepUp=$DeltaStepUp \
#         --DeltaStepDown=$DeltaStepDown \
#         --AdaptWindow=$AdaptWindow \
#         --nFg=$nFg \
#         --fgBytes=$fgBytes \
#         --stop=$stop"
#     done
# done

# echo "Improved sweep completed!"