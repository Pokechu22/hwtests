#!/bin/sh

wiiload "$1"
# empiric value, no idea if this differs for large executables
sleep 1
echo 8
sleep 1
echo 7
sleep 1
echo 6
sleep 1
echo 5
sleep 1
echo 4
sleep 1
echo 3
sleep 1
echo 2
sleep 1
echo 1
sleep 1
netcat ${WIILOAD#tcp:} 16784
