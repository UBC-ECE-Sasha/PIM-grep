#!/bin/bash

search="unsigned"
where="../linux-stable"
options="MAX_FILES_PER_DPU=64"

tasklets_set="1 2 4 6 8 10 12 14 16 18 20 22"

# compile the different verions
for tasklets in $tasklets_set; do
	make NR_TASKLETS=$tasklets $options
done

for tasklets in $tasklets_set; do
	echo "Tasklets: $tasklets"
	total_time=$(time find $where | ./host-$tasklets -c -d -t "$search" - > output-T$tasklets.dpu)
	echo "Time: $total_time"
done
