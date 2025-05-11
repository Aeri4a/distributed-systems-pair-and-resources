# Manual deployment & run

## Build
```bash
docker build --build-arg SSH_PUBLIC_KEY="$(cat ~/.ssh/mpi-test.pub)" -t openmpi-ssh .
```

```bash
docker build --build-arg SSH_PUBLIC_KEY="SSH_PUB_KEY_HERE" -t openmpi-ssh .
```

## Run container
```bash
docker run -d --name ssh-server -p 12345:22 openmpi-ssh
```

## Run OpenMPI
```bash
mpirun -np 2 --hostfile hosts.txt --prefix /usr/local/openmpi /home/mpiuser/distributed
```


## Automatic with Kubernetes (in progress)
1. Setup master node and child nodes
2. Generate SSH Key
3. Run docker image on all nodes
4. Setup ssh/config with all ip addresses
5. Setup hosts.txt file with ssh hosts
6. Run