#!/bin/bash

./ns3 build
rm -f results/summary_wired.csv

# bgTcp=$1
bgTcpName=(westwood imtcpbg imtcpbgimproved)

# number of nodes = 20 40 60 80 100
# number of flows = 10 20 30 40 50
# number of packets = 100 200 300 400 500

nodes_list=(20 40 60 80 100)
flows_list=(10)
rate_list=(100)

for bgTcp in "${bgTcpName[@]}"
do
    for nNodes in "${nodes_list[@]}"
    do
        for nFlows in "${flows_list[@]}"
        do
            for pps in "${rate_list[@]}"
            do
            echo "Running $bgTcp nodes=$nNodes flows=$nFlows pps=$pps"

            ./ns3 run "scratch/wired_eval \
            --bgTcp=$bgTcp \
            --nNodes=$nNodes \
            --nFlows=$nFlows \
            --pps=$pps \
            --stop=20"
            done
        done
    done
done
