while true
do
  pidof ./sparrow_main_d || ./sparrow_main_d &
  pidof ./sparrow || ./sparrow &
  sleep 300
done
