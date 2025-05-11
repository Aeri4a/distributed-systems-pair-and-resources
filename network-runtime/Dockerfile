FROM ubuntu:latest

ARG MPI_USER=mpiuser
ARG MPI_USER_ID=1001
ENV HOME=/home/${MPI_USER}
ENV DEBIAN_FRONTEND=noninteractive

# Install OpenSSH server and OpenMPI
RUN apt update && apt install -y --no-install-recommends \
    openssh-server \
    build-essential \
    zlib1g-dev \
    apt-utils \
    wget \
    sudo \
    # openmpi-bin \
    # openmpi-common \
    # libopenmpi-dev \
    # libgtk2.0-dev \
    # remove cache
    && rm -rf /var/lib/apt/lists/* 

RUN ssh-keygen -A

# Set OpenMPI version
ENV OMPI_VERSION=4.1.8
ENV OMPI_PREFIX=/usr/local/openmpi

# OpenMPI build
RUN wget https://download.open-mpi.org/release/open-mpi/v4.1/openmpi-${OMPI_VERSION}.tar.gz --no-check-certificate && \
    tar xzf openmpi-${OMPI_VERSION}.tar.gz && \
    rm openmpi-${OMPI_VERSION}.tar.gz

RUN cd openmpi-${OMPI_VERSION} && \
    ./configure \
    --prefix=${OMPI_PREFIX} \
    # --enable-fortran \
    --enable-shared && \
    make -j$(nproc) && \
    make install && \
    cd .. && \
    rm -rf openmpi-${OMPI_VERSION}


# Configure SSH
RUN mkdir -p /run/sshd
RUN echo 'PermitRootLogin yes' >> /etc/ssh/sshd_config
RUN echo 'PubkeyAuthentication yes' >> /etc/ssh/sshd_config
RUN echo 'PasswordAuthentication no' >> /etc/ssh/sshd_config
RUN sed 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' -i /etc/pam.d/sshd

RUN groupadd -g ${MPI_USER_ID} ${MPI_USER}
RUN useradd -m -s /bin/bash -u ${MPI_USER_ID} -g ${MPI_USER_ID} ${MPI_USER}
RUN adduser ${MPI_USER} sudo
RUN echo "${MPI_USER} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${MPI_USER}
RUN chmod 0440 /etc/sudoers.d/${MPI_USER}

RUN mkdir -p /home/$MPI_USER/.ssh
RUN chown -R $MPI_USER:$MPI_USER /home/$MPI_USER/.ssh
RUN chmod 700 /home/$MPI_USER/.ssh

ARG SSH_PUBLIC_KEY
RUN echo "$SSH_PUBLIC_KEY" > /home/$MPI_USER/.ssh/authorized_keys
RUN chown $MPI_USER:$MPI_USER /home/$MPI_USER/.ssh/authorized_keys
RUN chmod 600 /home/$MPI_USER/.ssh/authorized_keys

# OpenMPI paths
# RUN echo 'PATH="$PATH:/usr/local/openmpi/bin"' >> /home/$MPI_USER/.bashrc
# RUN echo 'LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/local/openmpi/lib"' >> /home/$MPI_USER/.bashrc

RUN echo "export PATH=\"${OMPI_PREFIX}/bin:\$PATH\""   >> /etc/bash.bashrc && \
    echo "export LD_LIBRARY_PATH=\"${OMPI_PREFIX}/lib:\$LD_LIBRARY_PATH\"" >> /etc/bash.bashrc && \
    echo "export PATH=\"${OMPI_PREFIX}/bin:\$PATH\""   >> /root/.bashrc && \
    echo "export LD_LIBRARY_PATH=\"${OMPI_PREFIX}/lib:\$LD_LIBRARY_PATH\"" >> /root/.bashrc

USER root

RUN rm -fr ${HOME}/.openmpi && mkdir -p ${HOME}/.openmpi
ADD default-mca-params.conf ${HOME}/.openmpi/mca-params.conf
RUN chown -R ${MPI_USER}:${MPI_USER} ${HOME}/.openmpi


# Expose SSH port
EXPOSE 22

# Start SSH server
CMD ["/usr/sbin/sshd", "-D"]