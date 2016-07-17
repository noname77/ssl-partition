# ssl-partition

// initial steps

clone the repo and run:

```bash
./build-ssl-partition.sh -g
```

this will compile opensgx, and nginx with libressl
now go to

```bash
cd build/opensgx/user
```

copy the nginx private key somewhere where normal user can access and change the location in libressl-pipe.c

char priv_key_file[] = "/home/osboxes/Documents/scripts/buildsgx/opensgx/user/test/keys/nginx.pem";

run the opensgx side of the pipe
```bash
./test.sh test/openssl/libressl-pipe sgx_read sgx_write
```

in a different window/tab run nginx
```bash
sudo nginx
```

go to your browser and connect to 
https://localhost