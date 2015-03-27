# the following are some useful shell cmd to analyse the log or the network

echo "-------------------------------------\n"
# for log, find the different ip addresses and the numbers it visited the website
cat log/$(date +%F).log | awk 'BEGIN{pv=0;uv=0} /ip:/{++s[$10];}END {for(a in s) {print a, s[a];uv++; pv=s[a]+pv;} printf "\nUV:%d, PV:%d\n", uv, pv;}'

# for network
echo "------------------------------------\n"
netstat -n | awk '/^tcp/ {++s[$NF];} END {for(a in s) print a, s[a];}'
echo "------------------------------------\n"
