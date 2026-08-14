sudo sed -i '/blacklist cdc_acm/d' /etc/modprobe.d/blacklist-qcserial.conf
sudo modprobe cdc_acm
