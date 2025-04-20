#!/bin/bash

IP="$1"

if [ -z "$IP" ]; then
  echo "Usage: $0 <device-ip>"
  exit 1
fi

while true; do
  echo ">>>>   Connecting to $IP:23...   <<<<"
  telnet "$IP"
  echo ">>>>   Disconnected. Reconnecting in 5 seconds...   <<<<"
  sleep 5
done
