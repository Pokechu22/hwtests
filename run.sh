#!/bin/sh

wiiload "$1"
# empiric value, no idea if this differs for large executables
sleep 8
netcat ${WIILOAD#tcp:} 16784
