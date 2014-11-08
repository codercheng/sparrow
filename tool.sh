# the following are some useful shell cmd to analyse the log or the network


# for log, find the different ip addresses and the numbers it visited the website
cat 2014-11-01.log | awk '/ip:/ {++s[$10];}END {for(a in s) print a, s[a];}'

# for network

netstat -n | awk '/^tcp/ {++s[$NF];} END {for(a in s) print a, s[a];}'

