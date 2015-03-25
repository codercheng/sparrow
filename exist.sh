while true
do
  pidof ./sparrow_main_r || ./sparrow_main_r &
  pidof ./sparrow || ./sparrow &
  sleep 300
done
