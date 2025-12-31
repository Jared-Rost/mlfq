#!/bin/zsh

./mlfq 1 200 tasks.txt > output2.txt
./mlfq 1 800 tasks.txt >> output2.txt
./mlfq 1 1600 tasks.txt >> output2.txt
./mlfq 1 3200 tasks.txt >> output2.txt
./mlfq 2 200 tasks.txt >> output2.txt
./mlfq 2 800 tasks.txt >> output2.txt
./mlfq 2 1600 tasks.txt >> output2.txt
./mlfq 2 3200 tasks.txt >> output2.txt
./mlfq 8 200 tasks.txt >> output2.txt
./mlfq 8 800 tasks.txt >> output2.txt
./mlfq 8 1600 tasks.txt >> output2.txt
./mlfq 8 3200 tasks.txt >> output2.txt