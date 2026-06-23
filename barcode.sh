#!/bin/bash

while read line; do
  echo $line
done < <(cat /dev/ttyUSB0 > /dev/etf3)
