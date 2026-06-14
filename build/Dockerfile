# Build environment for the SC1000 controller firmware (buildroot 2018.08.4).
# Ubuntu 18.04 matches that buildroot era. Runs as a non-root user because some
# buildroot package configure scripts refuse to run as root.
FROM ubuntu:18.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc g++ make file wget ca-certificates cpio unzip rsync bc \
        python python3 perl git patch sed gawk diffutils findutils bzip2 xz-utils \
        libncurses5-dev libssl-dev flex bison \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -u 1000 builder
USER builder
ENV HOME=/home/builder
WORKDIR /work
CMD ["bash"]
