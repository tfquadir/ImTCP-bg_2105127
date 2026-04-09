# python3 results/plot_wired.py --x areaScale --y throughputMbps
# python3 results/plot_wired.py --x areaScale --y avgDelay 
# python3 results/plot_wired.py --x areaScale --y deliveryRatio
# python3 results/plot_wired.py --x areaScale --y dropRatio
# python3 results/plot_wired.py --x areaScale --y energyConsumedJ

python3 results/plot_wired.py fgMeanFct
python3 results/plot_wired.py fgP95Fct
python3 results/plot_wired.py bgMbps
python3 results/plot_wired.py qAvgPkts
python3 results/plot_wired.py qP95Pkts
python3 results/plot_wired.py avgDelay
python3 results/plot_wired.py avgJitter
python3 results/plot_wired.py deliveryRatio
python3 results/plot_wired.py fairness