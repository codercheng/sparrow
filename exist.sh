while true
do
  pidof ./sparrow || ./sparrow
  sleep 10
done
