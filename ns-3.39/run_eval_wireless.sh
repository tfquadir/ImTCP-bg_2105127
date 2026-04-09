#!/bin/bash
set -e

./ns3 build
rm -f results/summary_wireless.csv

bgTcpName=(westwood imtcpbg imtcpbgimproved)

# number of nodes = 20 40 60 80 100
# number of flows = 10 20 30 40 50
# number of packets = 100 200 300 400 500
# area = 1 2 3 4 5

nodes_list=(20 40 60 80 100)
flows_list=(10)
rate_list=(100)
area_scale_list=(2)

for bgTcp in "${bgTcpName[@]}"
do
    for nNodes in "${nodes_list[@]}"
    do
        for nFlows in "${flows_list[@]}"
        do
            for pps in "${rate_list[@]}"
            do
                for areaScale in "${area_scale_list[@]}"
                do
                    echo "Running $bgTcp nodes=$nNodes flows=$nFlows pps=$pps areaScale=$areaScale"

                    ./ns3 run "scratch/wireless_eval \
                    --bgTcp=$bgTcp \
                    --nNodes=$nNodes \
                    --nFlows=$nFlows \
                    --pps=$pps \
                    --areaScale=$areaScale \
                    --txRange=10 \
                    --stop=20"
                done
            done
        done
    done
done