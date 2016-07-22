#ifndef _SGXBRIDGE_H_
#define _SGXBRIDGE_H_

#define CMD_PREMASTER "premaster"
#define CMD_SRV_RAND "srvrand"
#define CMD_CLNT_RAND "clntrand"
#define CMD_MASTER_SEC "mastersec"
#define CMD_ALGO "algo"

#define NAME_BUF_SIZE 256

int sgxbridge_init();
int opensgx_pipe_init(int flag_dir);
int opensgx_pipe_open(char *unique_id, int is_write, int flag_dir);
void sgxbridge_pipe_read(int len, char* data);
void sgxbridge_pipe_write(char* cmd, int len, char* data);
void print_hex(unsigned char *buf, int len);
void sgxbridge_generate_server_random(void* buf, int nbytes);
int sgxbridge_get_master_secret(unsigned char* buf);

#endif /* _SGXBRIDGE_H_ */