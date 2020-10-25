#!/bin/bash -e
cd "$(dirname "$0")"
cd ..
docker run -ti -v `echo $PWD`:/liumos_host \
	-p 0.0.0.0:2222:2222/tcp \
	-p 0.0.0.0:5905:5905/tcp \
	-p 0.0.0.0:8888:8888/udp \
	-p 0.0.0.0:8889:8889/udp \
	-p 0.0.0.0:1235:1235/tcp \
	hikalium/liumos-builder:latest \
	/liumos_host/scripts/for_docker/run.sh
